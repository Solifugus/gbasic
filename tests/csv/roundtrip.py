#!/usr/bin/env python3
"""Writing, checked against a reader that is not ours (docs/csv_design.md §9).

  write   csv.line writes generated records; Python's csv.reader reads them back.
  read    Python's csv.writer writes generated records (minimal quoting and
          quoting everything, LF and CRLF); csv.records reads them back.

Each direction compares FIELDS, the thing a round trip promises. Bytes are not
compared: two correct writers may quote differently.

Usage: roundtrip.py GBASIC WORKDIR SEED
"""
import csv
import io
import json
import random
import subprocess
import sys

DELIMS = {"comma": ",", "tab": "\t", "pipe": "|", "semi": ";"}


def field(rng, d):
    alphabet = ["a", "Q", "7", " ", "é", "-", '"', d, "\n", "\r", "\r\n", "''", ""]
    return "".join(rng.choice(alphabet) for _ in range(rng.choice([0, 1, 2, 4, 9])))


def records(rng, d, n):
    return [[field(rng, d) for _ in range(rng.choice([1, 1, 2, 3, 6]))] for _ in range(n)]


def gbasic(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit("gbasic failed: %s" % " ".join(args))
    return r.stdout


def main():
    gb, work, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
    rng = random.Random(seed)
    failed = 0

    # --- write: csv.line, read by Python ---
    checked = 0
    for case in range(60):
        dname = rng.choice(list(DELIMS))
        d = DELIMS[dname]
        ending = rng.choice(["lf", "crlf"])
        recs = records(rng, d, rng.choice([1, 3, 10, 30]))
        src = "%s/w%d.jsonl" % (work, case)
        out = "%s/w%d.csv" % (work, case)
        with open(src, "w") as f:
            for r in recs:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
        gbasic([gb, "tests/csv/write_lines.bas", src, out, dname, ending])
        with open(out, newline="", encoding="utf-8") as f:
            back = [r for r in csv.reader(f, delimiter=d, strict=True)]
        if back != recs:
            failed += 1
            sys.stderr.write("write case %d (%s, %s): Python read back something else\n" % (case, dname, ending))
            for a, b in zip(recs, back):
                if a != b:
                    sys.stderr.write("  wrote %r\n  read  %r\n" % (a, b))
                    break
        checked += len(recs)
    print("write: %d records written by csv.line, read back by Python's csv.reader" % checked)

    # --- read: Python's writer, read by csv.records ---
    manifest = []
    want = []
    for case in range(60):
        dname = rng.choice(list(DELIMS))
        d = DELIMS[dname]
        quoting = rng.choice([csv.QUOTE_MINIMAL, csv.QUOTE_ALL])
        term = rng.choice(["\n", "\r\n"])
        recs = records(rng, d, rng.choice([1, 3, 10, 30]))
        buf = io.StringIO(newline="")
        csv.writer(buf, delimiter=d, quoting=quoting, lineterminator=term).writerows(recs)
        path = "%s/r%d.csv" % (work, case)
        with open(path, "wb") as f:
            f.write(buf.getvalue().encode())
        manifest.append("%d\t%s\t%s\tdq" % (case, path, dname))
        for r in recs:
            want.append("%d %s" % (case, json.dumps(r, separators=(",", ":"), ensure_ascii=False)))
    with open(work + "/manifest", "w") as f:
        f.write("\n".join(manifest) + "\n")
    got = gbasic([gb, "tests/csv/diff.bas", work + "/manifest"]).splitlines()
    # diff.bas prints case, line, offset, fields: keep case and fields
    got = ["%s %s" % (g.split(" ", 3)[0], g.split(" ", 3)[3]) for g in got]
    if got != want:
        failed += 1
        for a, b in zip(want, got):
            if a != b:
                sys.stderr.write("read: Python wrote %s\n      csv.records read %s\n" % (a, b))
                break
        if len(got) != len(want):
            sys.stderr.write("read: %d records written, %d read\n" % (len(want), len(got)))
    print("read: %d records written by Python's csv.writer, read back by csv.records" % len(want))
    sys.exit(1 if failed else 0)


main()
