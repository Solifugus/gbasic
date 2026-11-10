#!/usr/bin/env python3
"""Messages for tests/run_mail_parse.sh to read, written byte by byte from a
seed, so every form the oracle compares is one this file put there.

    make_mail.py DIR COUNT SEED

writes DIR/NNNN.eml. Half are written here by hand-rolled rules (folding,
encoded words B and Q in three charsets, RFC 2231 filenames, quoted-printable
and base64 written by this file's own encoders, LF or CRLF, lower-case header
names, preambles, epilogues, transport padding, lines that start like the
boundary and are not it, nested multiparts, multipart/related with a start,
message/rfc822 inside, UTF-16). The other half are built by Python's email package
(EmailMessage, policy SMTP or default), a second writer with its own habits.
Nothing here decides what a message MEANS; tests/mail/check_mail.py asks
Python's parser that, from the bytes alone.
"""
import base64
import os
import random
import sys
from email.headerregistry import Address
from email.message import EmailMessage
from email import policy

WORDS = ["order", "invoice", "shipment", "pallet", "Monday", "please", "confirm",
         "the", "total", "is", "due", "net", "30", "days", "thanks", "regards",
         "report", "attached", "see", "below", "price", "€9", "Łódź", "Zakład",
         "İzmir", "café", "naïve", "Grüße", "Müller", "smörgåsbord", "Ærø", "façade",
         "a=b", "x?y", "_under_", "tab\there", "50%", "\"quoted\"", "semi;colon"]
LATIN1_WORDS = ["café", "naïve", "Grüße", "Müller", "smörgåsbord", "Ærø", "façade", "señor"]
NAMES = ["Anna Kowalska", "Zakład Mechaniczny", "Desk, Help", "O'Brien", "Jürgen Weiß",
         "Ali Yılmaz", "Help Desk", "J. R. Smith", "Ærø Bryggeri", "Ünal"]


def words(r, n, charset):
    pool = WORDS if charset in ("utf-8",) else (
        [w for w in WORDS if all(ord(c) < 128 for c in w)] + (LATIN1_WORDS if charset != "us-ascii" else []))
    if charset == "windows-1252":
        pool = pool + ["€9", "“curly”", "—dash"]
    return " ".join(r.choice(pool) for _ in range(n))


def encoded_words(r, text, charset):
    """text as one or more RFC 2047 encoded words, split at characters."""
    out = []
    chars = list(text)
    while chars:
        take = r.randint(1, max(1, min(len(chars), 12)))
        piece = "".join(chars[:take])
        chars = chars[take:]
        raw = piece.encode(charset)
        if r.random() < 0.5:
            out.append("=?%s?B?%s?=" % (r.choice([charset, charset.upper()]), base64.b64encode(raw).decode()))
        else:
            q = []
            for b in raw:
                c = chr(b)
                if c == " ":
                    q.append("_")
                elif c.isalnum() and b < 128:
                    q.append(c)
                else:
                    q.append("=%02X" % b if r.random() < 0.8 else "=%02x" % b)
            out.append("=?%s?%s?%s?=" % (charset, r.choice("Qq"), "".join(q)))
    return " ".join(out)


def header_text(r, text):
    """Unstructured text, as written: plain when ASCII, otherwise ASCII words
    left plain and the rest as encoded words in some charset."""
    if all(ord(c) < 128 for c in text) and r.random() < 0.8:
        return text
    charsets = ["utf-8"]
    try:
        text.encode("iso-8859-1")
        charsets.append("iso-8859-1")
    except UnicodeEncodeError:
        pass
    try:
        text.encode("windows-1252")
        charsets.append("windows-1252")
    except UnicodeEncodeError:
        pass
    cs = r.choice(charsets)
    pieces = text.split(" ")
    out = []
    run = []
    for w in pieces:
        if all(ord(c) < 128 for c in w) and "=?" not in w and r.random() < 0.6 and w:
            if run:
                out.append(encoded_words(r, " ".join(run), cs))
                run = []
            out.append(w)
        else:
            run.append(w)
    if run:
        out.append(encoded_words(r, " ".join(run), cs))
    return " ".join(out)


def fold(r, value, eol):
    """Fold a header value at some of its spaces."""
    if len(value) < 30 or r.random() < 0.5:
        return value
    parts = value.split(" ")
    out = parts[0]
    for p in parts[1:]:
        out += (eol + r.choice([" ", "\t", "  "]) if r.random() < 0.3 else " ") + p
    return out


def mailbox(r):
    user = r.choice(["anna", "desk", "biuro", "ops", "j.smith", "o_brien", "x+tag"])
    dom = r.choice(["example.com", "zaklad.pl", "mail.example.org", "EXAMPLE.net"])
    addr = user + "@" + dom
    name = r.choice(NAMES)
    form = r.randrange(5)
    if form == 0:
        return addr
    if form == 1:
        if all(ord(c) < 128 for c in name) and not any(c in name for c in ",.'\""):
            return "%s <%s>" % (name, addr)
        form = 2
    if form == 2:
        if all(ord(c) < 128 for c in name):
            return '"%s" <%s>' % (name.replace('"', '\\"'), addr)
        form = 3
    if form == 3:
        cs = "utf-8"
        return "%s <%s>" % (encoded_words(r, name, cs), addr)
    return "%s (%s)" % (addr, r.choice(["office", "the desk", "nested (comment) here"]))


def address_list(r, n):
    boxes = [mailbox(r) for _ in range(n)]
    if r.random() < 0.15:
        return "Team: %s;" % ", ".join(boxes)
    return ", ".join(boxes)


def date_header(r):
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    y = r.randint(1995, 2030)
    mo = r.randint(1, 12)
    d = r.randint(1, 28)
    h, mi, s = r.randint(0, 23), r.randint(0, 59), r.randint(0, 59)
    import datetime
    wd = days[datetime.date(y, mo, d).weekday()]
    zone = r.choice(["+0000", "-0500", "+0530", "-0800", "+0100", "+1245", "GMT", "UT", "EST", "PDT", "CDT"])
    year = str(y)
    if r.random() < 0.1:
        year = "%02d" % (y % 100) if y >= 2000 and y < 2050 or y >= 1950 and y < 2000 else str(y)
    day = str(d) if r.random() < 0.3 else "%02d" % d
    secs = ":%02d" % s if r.random() < 0.9 else ""
    prefix = wd + ", " if r.random() < 0.8 else ""
    return "%s%s %s %s %02d:%02d%s %s" % (prefix, day, months[mo - 1], year, h, mi, secs, zone)


def qp_encode(raw, r, eol):
    lines = raw.split(b"\n")
    out_lines = []
    for li, line in enumerate(lines):
        enc = []
        for i, b in enumerate(line):
            c = bytes([b])
            last = i == len(line) - 1
            if b == 61 or b > 126 or (b < 32 and b != 9) or (last and b in (32, 9)) or r.random() < 0.03:
                enc.append("=%02X" % b if r.random() < 0.9 else "=%02x" % b)
            else:
                enc.append(c.decode("ascii"))
        # soft breaks at 76
        s, cur = [], ""
        for tok in enc:
            if len(cur) + len(tok) > 75:
                s.append(cur + "=" + (" " if r.random() < 0.1 else ""))
                cur = ""
            cur += tok
        s.append(cur)
        out_lines.append(eol.join(s))
    return eol.join(out_lines)


def b64_encode(raw, r, eol):
    s = base64.b64encode(raw).decode()
    width = r.choice([76, 64, 72, 40])
    return eol.join(s[i:i + width] for i in range(0, len(s), width)) if s else ""


def text_leaf(r, eol, subtype="plain"):
    cs = r.choice(["utf-8", "utf-8", "iso-8859-1", "windows-1252", "us-ascii"])
    lines = []
    for _ in range(r.randint(1, 8)):
        lines.append(words(r, r.randint(0, 12), cs))
    if r.random() < 0.3:
        lines.append("--" + r.choice(["not-a-boundary", "B" * 3, ""]))
    if r.random() < 0.3:
        lines.append(">" + words(r, 3, cs))
    if subtype == "html":
        lines = ["<p>%s</p>" % l for l in lines]
    text = "\n".join(lines)
    if r.random() < 0.5:
        text += "\n"
    raw = text.encode(cs)
    ascii_only = all(b < 128 for b in raw)
    longest = max(len(l) for l in raw.split(b"\n"))
    choices = ["quoted-printable", "base64"]
    if ascii_only and longest < 900:
        choices += ["7bit", "7bit", None]
    if longest < 900:
        choices.append("8bit")
    cte = r.choice(choices)
    if cs == "utf-8" and r.random() < 0.1 and cte == "base64":
        cs = "utf-16"
        raw = text.encode("utf-16")
    if cte in ("7bit", "8bit", None):
        body = raw.decode("latin-1").replace("\n", eol)
    elif cte == "quoted-printable":
        body = qp_encode(raw, r, eol)
    else:
        body = b64_encode(raw, r, eol)
    hs = []
    params = "; charset=%s" % (r.choice([cs, '"%s"' % cs, cs.upper()]))
    if subtype == "plain" and r.random() < 0.1:
        params += "; format=fixed"
    hs.append(("Content-Type", "text/%s%s" % (subtype, params)))
    if cte:
        hs.append(("Content-Transfer-Encoding", r.choice([cte, cte.upper()])))
    if r.random() < 0.15:
        hs.append(("Content-Disposition", "inline"))
    return hs, body


def filename(r):
    base = r.choice(["report", "Łódź faktura", "scan 2027", "naïve plan", "a;b", "x" * 50 + "é"])
    return base + r.choice([".pdf", ".png", ".bin", ".txt"])


def attachment_leaf(r, eol):
    data = bytes(r.randrange(256) for _ in range(r.randint(0, 300)))
    mt = r.choice(["application/octet-stream", "application/pdf", "image/png"])
    name = filename(r)
    hs = []
    ascii_name = all(ord(c) < 128 for c in name)
    if ascii_name and r.random() < 0.6:
        disp = 'attachment; filename="%s"' % name
        ctp = '%s; name="%s"' % (mt, name)
    else:
        pct = "".join(chr(b) if (chr(b).isalnum() and b < 128) or chr(b) in ".-_" else "%%%02X" % b
                      for b in name.encode("utf-8"))
        if r.random() < 0.5 or len(pct) < 30:
            disp = "attachment; filename*=utf-8''%s" % pct
        else:
            cut = len(pct) // 2
            while pct[cut - 1] == "%" or pct[cut - 2] == "%":
                cut += 1
            disp = "attachment; filename*0*=utf-8''%s; filename*1*=%s" % (pct[:cut], pct[cut:])
        ctp = mt
    if r.random() < 0.2:
        disp = disp.replace("attachment", "inline", 1)
    hs.append(("Content-Type", ctp))
    hs.append(("Content-Transfer-Encoding", "base64"))
    hs.append(("Content-Disposition", disp))
    return hs, b64_encode(data, r, eol)


def boundary(r):
    if r.random() < 0.3:
        return "b" + "".join(r.choice("abcdefXYZ0123456789") for _ in range(r.randint(1, 12)))
    return r.choice(["", "=_", "----=_Part_"]) + "".join(r.choice("abcdefXYZ0123456789'()+_,-./:=?") for _ in range(r.randint(1, 30)))


def entity(r, eol, depth):
    """(headers, body) of a random entity."""
    kind = r.random()
    if depth >= 3 or kind < 0.35:
        return text_leaf(r, eol, "plain")
    if kind < 0.45:
        return text_leaf(r, eol, "html")
    if kind < 0.6:
        return attachment_leaf(r, eol)
    if kind < 0.67 and depth < 2:
        hs, body = message(r, eol, depth + 1)
        return [("Content-Type", "message/rfc822")], render_headers(hs, eol, r) + eol + body
    sub = r.choice(["mixed", "alternative", "related", "mixed"])
    b = boundary(r)
    children = [entity(r, eol, depth + 1) for _ in range(r.randint(1, 4))]
    params = '; boundary="%s"' % b if (r.random() < 0.5 or any(c in b for c in "'()+,/:=?")) else "; boundary=%s" % b
    if sub == "related" and r.random() < 0.5 and len(children) > 1:
        k = r.randrange(len(children))
        cid = "<part%d.%d@example.com>" % (depth, k)
        children[k] = (children[k][0] + [("Content-ID", cid)], children[k][1])
        params += '; start="%s"' % cid
    out = []
    if r.random() < 0.4:
        out.append("This is a multi-part message in MIME format." + eol)
    for hs, body in children:
        pad = r.choice(["", "", " ", "\t "])
        out.append("--" + b + pad + eol + render_headers(hs, eol, r) + eol + body + eol)
    out.append("--" + b + "--" + r.choice(["", " "]))
    if r.random() < 0.3:
        out.append(eol + "epilogue text" + eol)
    else:
        out.append(eol)
    return [("Content-Type", "multipart/%s%s" % (sub, params))], "".join(out)


def render_headers(hs, eol, r):
    lines = []
    for name, value in hs:
        if r.random() < 0.1:
            name = name.lower()
        lines.append(name + ":" + r.choice([" ", " ", "  ", "\t"]) + fold(r, value, eol) + eol)
    return "".join(lines)


def message(r, eol, depth=0):
    hs = [("From", address_list(r, 1)),
          ("To", address_list(r, r.randint(1, 3)))]
    if r.random() < 0.4:
        hs.append(("Cc", address_list(r, r.randint(1, 2))))
    if r.random() < 0.2:
        hs.append(("Reply-To", mailbox(r)))
    subject = words(r, r.randint(0, 10), "utf-8")
    hs.append(("Subject", header_text(r, subject)))
    hs.append(("Date", date_header(r)))
    hs.append(("Message-ID", "<%08x.%d@example.com>" % (r.getrandbits(32), depth)))
    if r.random() < 0.3:
        hs.append(("In-Reply-To", "<prev%d@example.com>" % r.randrange(100)))
        hs.append(("References", "<a%d@example.com> <prev%d@example.com>" % (r.randrange(9), r.randrange(9))))
    if r.random() < 0.2:
        hs.append(("X-Note", header_text(r, words(r, 4, "utf-8"))))
    hs.append(("MIME-Version", "1.0"))
    chs, body = entity(r, eol, depth)
    if r.random() < 0.2 and chs[0][1].startswith("text/plain"):
        chs = []   # no Content-Type at all: text/plain, us-ascii
        body = "plain words only" + eol
    hs = hs + chs
    return hs, body


def handwritten(r):
    eol = r.choice(["\n", "\r\n"])
    hs, body = message(r, eol)
    return (render_headers(hs, eol, r) + eol + body).encode("latin-1")


def python_built(r):
    m = EmailMessage()
    m["From"] = Address(r.choice(NAMES), addr_spec="anna@example.com")
    m["To"] = [Address(r.choice(NAMES), addr_spec="u%d@example.org" % i) for i in range(r.randint(1, 3))]
    m["Subject"] = words(r, r.randint(1, 15), "utf-8")
    m["Date"] = date_header(r)
    m["Message-ID"] = "<%08x@example.com>" % r.getrandbits(32)
    cs = r.choice(["utf-8", "iso-8859-1"])
    text = "\n".join(words(r, r.randint(0, 14), cs) for _ in range(r.randint(1, 6))) + "\n"
    m.set_content(text, cte=r.choice(["quoted-printable", "base64", "8bit"]), charset=cs)
    if r.random() < 0.6:
        m.add_alternative("<p>%s</p>\n" % words(r, 6, "utf-8"), subtype="html")
    for _ in range(r.randint(0, 2)):
        m.add_attachment(bytes(r.randrange(256) for _ in range(r.randint(0, 200))),
                         maintype="application", subtype="octet-stream", filename=filename(r))
    pol = r.choice([policy.SMTP, policy.default])
    return m.as_bytes(policy=pol)


# The forms the docstring claims, each a pattern that shows it in the bytes.
# A corpus missing one fails here, where it is made, rather than passing a
# check that never met the form.
PREMISES = {
    "CRLF line ends": rb"\r\n",
    "LF line ends": rb"[^\r]\n",
    "a folded header": rb"\n[ \t]+\S",
    "a lower-case header name": rb"(?m)^(subject|to|from|date|content-type):",
    "a B encoded word": rb"=\?[^?]+\?[Bb]\?",
    "a Q encoded word": rb"=\?[^?]+\?[Qq]\?",
    "an encoded word in Latin-1": rb"=\?(?i:iso-8859-1)\?",
    "an encoded word in windows-1252": rb"=\?(?i:windows-1252)\?",
    "adjacent encoded words": rb"\?= +=\?",
    "an encoded display name": rb"=\?[^ ]*\?= <",
    "a quoted display name with a comma": rb'"Desk, Help"',
    "a group": rb"Team: ",
    "a comment after an address": rb"@[A-Za-z.]+ \(",
    "a two-digit year": rb"Date:.* [0-9]{1,2} [A-Z][a-z]{2} [0-9]{2} ",
    "a zone name": rb"Date:.*:[0-9]{2} (GMT|UT|EST|PDT|CDT)\r?$",
    "a date with no weekday": rb"Date:[ \t]+[0-9]",
    "a date with no seconds": rb"Date:.* [0-9]{2}:[0-9]{2} [+A-Z-]",
    "In-Reply-To and References": rb"References: <",
    "quoted-printable": rb"(?i)quoted-printable",
    "lower-case hex in quoted-printable": rb"=[0-9a-f][a-f]",
    "a soft break with white space after it": rb"= \r?\n",
    "base64": rb"(?i)transfer-encoding:[ \t]*base64",
    "8bit": rb"(?i)transfer-encoding:[ \t]*8bit",
    "UTF-16": rb"charset=\"?utf-16",
    "iso-8859-1 text": rb"(?i)text/plain; charset=\"?iso-8859-1",
    "windows-1252 text": rb"(?i)charset=\"?windows-1252",
    "multipart/alternative": rb"multipart/alternative",
    "multipart/related with a start": rb"multipart/related[^\n]*start=",
    "message/rfc822 inside": rb"message/rfc822",
    "a preamble": rb"MIME format\.",
    "an epilogue": rb"epilogue text",
    "transport padding after a delimiter": rb"\n--[^\n]*[^-\s][ \t]+\r?\n",
    "a line that begins like a boundary": rb"\n--(not-a-boundary|BBB)\r?\n",
    "an unquoted boundary": rb"boundary=[^\"]",
    "an RFC 2231 filename": rb"filename\*=utf-8''",
    "an RFC 2231 continuation": rb"filename\*1\*=",
    "an inline part with a filename": rb"inline; filename",
    "a part with no Content-Type": rb"MIME-Version: 1.0\r?\n\r?\nplain words only",
    "a multipart Python's email package wrote": rb'boundary="={15}',
    "a header Python's email package encoded": rb"Subject: =\?utf-8\?q\?",
}


def coverage(out):
    import re
    data = [open(os.path.join(out, f), "rb").read() for f in sorted(os.listdir(out)) if f.endswith(".eml")]
    missing = []
    for label, pattern in PREMISES.items():
        n = sum(1 for d in data if re.search(pattern, d, re.M))
        if n == 0:
            missing.append(label)
    if missing:
        sys.exit("make_mail: the corpus has no " + "; no ".join(missing))
    print("make_mail: %d messages, all %d forms present" % (len(data), len(PREMISES)))


def main():
    out, n, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    r = random.Random(seed)
    for i in range(n):
        data = handwritten(r) if i % 2 == 0 else python_built(r)
        with open(os.path.join(out, "%04d.eml" % i), "wb") as f:
            f.write(data)
    coverage(out)


if __name__ == "__main__":
    main()
