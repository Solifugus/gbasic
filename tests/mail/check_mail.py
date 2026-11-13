#!/usr/bin/env python3
"""Holds mail.parse to Python's email package, which reads each message from
its bytes; nothing gBASIC wrote is used to decide what a message means.

    check_mail.py DIR GOT_JSONL

DIR holds the .eml files; GOT_JSONL is tests/mail/mail_emit.bas's output, a
line per file in sorted order. Each message is parsed here with
email.policy.default, and for the message (and any message inside it):
  - the header names, in order, and every unstructured header's value
    (Subject, X-*) decoded;
  - From's first mailbox, and To, Cc and Reply-To as (display name, address)
    with groups opened;
  - Date as the UTC instant and the writer's offset (parsedate_to_datetime);
  - Message-ID, In-Reply-To and References;
  - every leaf part by path: content type, charset, disposition, filename
    (RFC 2231 and encoded words), transfer encoding, the decoded body bytes,
    and a text part's text (get_content, CRLF read as LF);
  - which part get_body(('plain',)) and get_body(('html',)) choose, and the
    attachments (disposition attachment, or a filename);
  - and that parse reported no problems, since every message here is
    well formed.
Prints `checks: N` and `mismatches: M`; exits nonzero on any mismatch.
"""
import binascii
import datetime
import json
import os
import re
import sys
from email import policy
from email.parser import BytesParser

checks = 0
bad = 0
shown = 0


def check(where, label, got, want):
    global checks, bad, shown
    checks += 1
    if got != want:
        bad += 1
        if shown < 40:
            shown += 1
            print("MISMATCH %s %s: got %r, want %r" % (where, label, got, want))


def leaves(part, path, out):
    if part.get_content_maintype() == "multipart":
        for i, c in enumerate(part.iter_parts()):
            leaves(c, str(i + 1) if path == "" else path + "." + str(i + 1), out)
    else:
        out.append((path or "1", part))
    return out


def mailboxes(h):
    if h is None:
        return []
    return [[a.display_name, a.addr_spec] for a in h.addresses]


def ids(h):
    if h is None:
        return []
    return str(h).replace("\r", "").replace("\n", "").split()


def body_of(part):
    """The decoded bytes, with RFC 2045's quoted-printable rule (below)."""
    body = part.get_payload(decode=True)
    if str(part.get("content-transfer-encoding") or "").strip().lower() == "quoted-printable":
        # RFC 2045 6.7: a decoder deletes trailing white space (the
        # transport may add it), so "= " ending a line is a soft break.
        # binascii.a2b_qp keeps it; the RFC's rule is applied first.
        raw = part.get_payload(decode=False).encode("latin-1", "surrogateescape")
        body = binascii.a2b_qp(re.sub(rb"[ \t]+(?=\r?\n|$)", b"", raw))
    return body


def text_of(part):
    """get_content's text, from body_of's bytes; CRLF read as LF."""
    if str(part.get("content-transfer-encoding") or "").strip().lower() != "quoted-printable":
        return part.get_content().replace("\r\n", "\n")
    return body_of(part).decode(part.get_content_charset("us-ascii"), "replace").replace("\r\n", "\n")


def names_alike(got, want, raw):
    """Python's address parser puts a space between adjacent encoded words in
    a display name, which RFC 2047 6.2 says to drop ("Anna Kowalsk a"). Where
    the header holds encoded words, the names are compared with their white
    space removed; the joining rule itself is tier 1's, by hand."""
    if "=?" not in raw:
        return got, want
    strip = lambda l: [["".join((p[0] or "").split()), p[1]] for p in l] if l is not None else None
    return strip(got), strip(want)


def compare(where, m, got):
    names = [k for k in m.keys()]
    check(where, "header names", [h[0] for h in got["headers"]], names)
    for (name, value), (gname, gvalue) in zip(m.items(), got["headers"]):
        lname = name.lower()
        if lname == "subject" or lname.startswith("x-"):
            want = str(value)
            # A header whose first line is empty ("Subject:" then a folded
            # line): Python 3.12 keeps the fold's leading space in the value,
            # 3.14 does not, and neither does gBASIC (RFC 5322 unfolding; the
            # space begins the fold, not the subject). Measured on the riscv64
            # VM's 3.12.3 against 3.14.4, 2026-11-11. Only that case is let by.
            if want != gvalue and want[:1] in (" ", "\t") and gvalue == want.lstrip(" \t"):
                want = gvalue
            check(where, "header " + name, gvalue, want)
    raw = {}
    for name, value in m.raw_items():
        raw.setdefault(name.lower(), "")
        raw[name.lower()] += str(value)
    g, w = names_alike([got["from"]] if got["from"] else [], mailboxes(m["from"])[:1], raw.get("from", ""))
    check(where, "from", g, w)
    for field, header in (("to", "to"), ("cc", "cc"), ("reply_to", "reply-to")):
        want = []
        for h in m.get_all(header) or []:
            want += [[a.display_name, a.addr_spec] for a in h.addresses]
        g, w = names_alike(got[field], want, raw.get(header, ""))
        check(where, field, g, w)
    d = m["date"].datetime if m["date"] is not None else None
    want_date = None
    if d is not None and d.tzinfo is not None:
        want_date = d.astimezone(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    check(where, "date", got["date"], want_date)
    check(where, "date offset", got["date_offset"],
          int(d.utcoffset().total_seconds() // 60) if want_date is not None else None)
    check(where, "message_id", got["message_id"], str(m["message-id"]) if m["message-id"] is not None else None)
    check(where, "in_reply_to", got["in_reply_to"], ids(m["in-reply-to"]))
    check(where, "references", got["references"], ids(m["references"]))

    ls = leaves(m, "", [])
    check(where, "part paths", [p["path"] for p in got["parts"]], [p for p, _ in ls])
    by_id = {}
    for (path, part), gp in zip(ls, got["parts"]):
        by_id[id(part)] = path
        w = where + " part " + path
        check(w, "type", gp["type"], part.get_content_type())
        cs = part.get_param("charset")
        check(w, "charset", gp["charset"], str(cs).lower() if cs is not None else None)
        check(w, "disposition", gp["disposition"], part.get_content_disposition() or "")
        check(w, "filename", gp["filename"], part.get_filename())
        cte = part.get("content-transfer-encoding")
        check(w, "encoding", gp["encoding"], str(cte).strip().lower() if cte is not None else "7bit")
        if part.get_content_maintype() == "message":
            inner = part.get_payload()[0]
            if gp.get("message") is None:
                check(w, "the message inside", None, "a message")
            else:
                compare(w + " (inner)", inner, gp["message"])
            continue
        want_body = body_of(part)
        check(w, "body", gp["body"], want_body.hex())
        if part.get_content_maintype() == "text":
            check(w, "text", gp["text"], text_of(part))
    plain = m.get_body(("plain",))
    html = m.get_body(("html",))
    check(where, "text part", got["text_part"], by_id.get(id(plain)) if plain is not None else None)
    check(where, "html part", got["html_part"], by_id.get(id(html)) if html is not None else None)
    if plain is not None:
        check(where, "text", got["text"], text_of(plain))
    want_att = [path for path, part in ls
                if part.get_content_disposition() == "attachment" or part.get_filename() is not None]
    check(where, "attachments", got["attachments"], want_att)
    check(where, "problems", got["problems"], [])


def main():
    directory, got_path = sys.argv[1:3]
    files = sorted(f for f in os.listdir(directory) if f.endswith(".eml"))
    got = [json.loads(line) for line in open(got_path, encoding="utf-8") if line.strip()]
    check("corpus", "messages read", len(got), len(files))
    parts = 0
    for name, g in zip(files, got):
        check(name, "file", g["file"], name)
        data = open(os.path.join(directory, name), "rb").read()
        m = BytesParser(policy=policy.default).parsebytes(data)
        compare(name, m, g)
        parts += len(g["parts"])
    print("messages: %d, parts: %d" % (len(files), parts))
    print("checks: %d\nmismatches: %d" % (checks, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
