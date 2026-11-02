#!/usr/bin/env python3
"""Generate delimited files and what reading them must give (docs/csv_design.md §9).

THE ORACLE IS THE GENERATOR'S OWN RECORD of what it wrote: every record's
fields, the line it starts on and its byte offset. Python's csv module is a
second, independent reader, and every file is checked against it HERE, before
gBASIC sees it, so a generator mistake fails this script rather than becoming
an expectation. Python is lenient after a closing quote, where gBASIC reports a
problem, so the generator never writes text after a closing quote.

Usage: gen_csv_diff.py OUTDIR SEED COUNT [big]
`big` writes files of thousands of records with long fields, so that a quote,
a doubled quote and a CRLF pair fall across the reader's 64 KiB buffer at
random places, which small files cannot reach.
Writes OUTDIR/case_N.csv, OUTDIR/expected (one line per record: case, line,
offset, fields as JSON) and OUTDIR/manifest (case, path, delimiter, quote).
"""
import csv
import io
import json
import random
import sys

DELIMS = {"comma": ",", "tab": "\t", "pipe": "|", "semi": ";"}
QUOTES = {"dq": '"', "sq": "'", "none": None}
BREAKS = ["\n", "\r\n", "\r"]


BIG = False


def make_field(rng, d, q):
    alphabet = ["a", "b", "Z", "0", "9", " ", "-", "é", "~", "\t" if d != "\t" else "x"]
    specials = [d, "\n", "\r\n", "\r"]
    if q:
        specials += [q, q + q]
    else:
        # no quoting: nothing that would need it, but a quote of either kind
        # is plain text
        specials = ['"', "'"]
    n = rng.choice([0, 0, 1, 2, 3, 5, 8, 20] + ([60, 200] if BIG else []))
    parts = []
    for _ in range(n):
        if rng.random() < 0.2:
            parts.append(rng.choice(specials))
        else:
            parts.append(rng.choice(alphabet))
    return "".join(parts)


def needs_quote(f, d, q):
    return any(c in f for c in (d, "\r", "\n")) or (q is not None and q in f)


def write_field(rng, f, d, q):
    if q is None:
        return f
    if needs_quote(f, d, q) or rng.random() < 0.3:
        return q + f.replace(q, q + q) + q
    return f


def make_case(rng):
    dname = rng.choice(list(DELIMS))
    qname = rng.choice(list(QUOTES))
    d, q = DELIMS[dname], QUOTES[qname]
    out = []          # text pieces
    records = []      # (line, offset, fields)
    line = 1
    offset = 0
    prev_break = None
    nrec = rng.choice([3000, 8000]) if BIG else rng.choice([0, 1, 2, 5, 12, 40])
    for i in range(nrec):
        # blank lines between records, never "\n" straight after a "\r", which
        # would be one break, not two
        while rng.random() < 0.15:
            choices = [b for b in BREAKS if not (prev_break == "\r" and b.startswith("\n"))]
            b = rng.choice(choices)
            out.append(b)
            offset += len(b.encode())
            line += 1
            prev_break = b
        nf = rng.choice([1, 1, 2, 3, 4, 7])
        fields = [make_field(rng, d, q) for _ in range(nf)]
        if nf == 1 and fields[0] == "":
            # one empty unquoted field is a blank line: quote it, or give it text
            if q is None:
                fields[0] = "x"
        text_fields = [write_field(rng, f, d, q) for f in fields]
        if nf == 1 and fields[0] == "" and q is not None:
            text_fields = [q + q]
        # a record may not begin with "\n" straight after a "\r"; it cannot,
        # since a line break in a field is quoted, so the record starts with a
        # quote or with text
        rec_text = d.join(text_fields)
        records.append((line, offset, fields))
        out.append(rec_text)
        offset += len(rec_text.encode())
        line += sum(f.count("\n") + f.count("\r") - f.count("\r\n") for f in fields)
        last = i == nrec - 1
        if last and rng.random() < 0.3:
            prev_break = None
        else:
            choices = [b for b in BREAKS]
            b = rng.choice(choices)
            out.append(b)
            offset += len(b.encode())
            line += 1
            prev_break = b
    return dname, qname, "".join(out), records


def python_reads(text, d, q):
    kw = {"delimiter": d, "strict": False}
    if q is None:
        kw["quoting"] = csv.QUOTE_NONE
        kw["quotechar"] = None
    else:
        kw["quotechar"] = q
        kw["doublequote"] = True
    return [r for r in csv.reader(io.StringIO(text, newline=""), **kw) if r != []]


def main():
    global BIG
    outdir, seed, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    BIG = len(sys.argv) > 4 and sys.argv[4] == "big"
    rng = random.Random(seed)
    manifest = []
    expected = []
    for n in range(count):
        dname, qname, text, records = make_case(rng)
        got = python_reads(text, DELIMS[dname], QUOTES[qname])
        want = [r[2] for r in records]
        if got != want:
            sys.stderr.write("generator and Python disagree on case %d (%s/%s):\n%r\npython %r\nwrote  %r\n"
                             % (n, dname, qname, text, got, want))
            sys.exit(2)
        path = "%s/case_%d.csv" % (outdir, n)
        with open(path, "wb") as f:
            f.write(text.encode())
        manifest.append("%d\t%s\t%s\t%s" % (n, path, dname, qname))
        for line, offset, fields in records:
            expected.append("%d %d %d %s" % (n, line, offset,
                                              json.dumps(fields, separators=(",", ":"), ensure_ascii=False)))
    with open(outdir + "/manifest", "w") as f:
        f.write("\n".join(manifest) + "\n")
    with open(outdir + "/expected", "w") as f:
        f.write("\n".join(expected) + ("\n" if expected else ""))
    print("%d cases, %d records" % (count, len(expected)))


main()
