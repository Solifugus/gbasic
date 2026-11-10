#!/usr/bin/env python3
"""Holds source_comments to the files themselves, over a corpus.

    check_corpus.py GOT_JSONL

For every file: each reported comment line is exactly the bytes at its line
and column, starting with a quote; blanking every reported comment leaves
source_outline's answer unchanged (so nothing reported was code); and every
line whose first non-blank character is a quote, outside an `sql` block's body,
is reported (so no leading comment was missed). Only files that parse are
held to the last two, since a parse stops at its first error.
"""
import json
import re
import sys

checks = bad = 0


def check(where, label, ok, detail=""):
    global checks, bad
    checks += 1
    if not ok:
        bad += 1
        if bad <= 30:
            print("MISMATCH %s %s %s" % (where, label, detail))


files = comments = 0
for line in open(sys.argv[1], encoding="utf-8"):
    g = json.loads(line)
    files += 1
    data = open(g["file"], "rb").read()
    rows = data.split(b"\n")
    seen = set()
    for c in g["comments"]:
        comments += 1
        for k, piece in enumerate(c["raw"].encode("utf-8").split(b"\n")):
            ln = c["start_line"] + k
            row = rows[ln - 1].rstrip(b"\r")
            col = c["start_column"] - 1
            if ln == 1 and data.startswith(b"\xef\xbb\xbf"):
                col += 3   # the lexer skips a byte-order mark; line 1 counts from after it
            check(g["file"], "comment bytes at %d:%d" % (ln, c["start_column"]),
                  row[col:col + len(piece)] == piece and piece.startswith(b"'") and col + len(piece) == len(row),
                  repr(piece[:40]))
            seen.add(ln)
    check(g["file"], "blanking keeps the length", g["still_has_bytes"])
    if not g["outline_ok"]:
        continue
    check(g["file"], "blanking every comment leaves the outline as it was", g["same_outline"])
    in_sql = False
    for n, row in enumerate(rows, 1):
        text = row.decode("utf-8", "replace").rstrip("\r")
        if re.match(r"^\s*sql\b(?!\s*=)", text) and not in_sql and re.search(r"\bon\b|^\s*sql\s*$", text):
            in_sql = True
            continue
        if in_sql:
            if re.match(r"^\s*end\s+sql\b", text):
                in_sql = False
            continue
        if re.match(r"^\s*'", text):
            check(g["file"], "line %d, a comment line, is reported" % n, n in seen, text[:50])
print("files: %d, comment runs: %d" % (files, comments))
print("checks: %d\nmismatches: %d" % (checks, bad))
sys.exit(1 if bad else 0)
