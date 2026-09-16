#!/usr/bin/env python3
"""Write the BAI2 fixtures used by tests/run_finio_bai2.sh.

AS WITH NACHA AND camt, THE ARITHMETIC IS COMPUTED HERE so the file's own
three-level control totals are an oracle rather than a transcript of what
`finio_bai2` says.  BAI2's is the strongest of the three: each account trailer
sums its own account, each group trailer sums its accounts, and the file
trailer sums its groups -- and every level also states a record count.

The foreign corpus (tests/finio/foreign_bai2) supplies the awkward real cases.
What it does NOT supply is a file whose correct answer is known in advance, or
a deliberately corrupted one, which is what these are for.
"""
import os, sys

def rec(*fields):
    return ",".join(str(f) for f in fields)

def account(number, currency, summaries, transactions):
    """summaries: [(type, amount, item_count, funds_type)]
       transactions: [(type, amount, funds_type, bank_ref, cust_ref, text)]"""
    f = ["03", number, currency]
    for t, a, ic, ft in summaries:
        f += [t, str(a), ic, ft]
    lines = [",".join(f) + "/"]
    for t, a, ft, br, cr, tx in transactions:
        lines.append(rec("16", t, a, ft, br, cr, tx) + "/")
    total = sum(a for _, a, _, _ in summaries) + sum(a for _, a, _, _, _, _ in transactions)
    lines.append(rec("49", total, len(lines) + 1) + "/")
    return lines, total


def build(corrupt=False):
    a1, t1 = account("0123456789", "USD",
                     [("010", 4350000, "", ""), ("040", 2830000, "", "")],
                     [("115", 450000, "", "REF001", "CUS001", "INCOMING WIRE"),
                      ("475", -125000, "", "REF002", "", "CHECK PAID, SERIAL 10231")])
    a2, t2 = account("9876543210", "USD",
                     [("010", -500000, "", ""), ("100", 1000000, "3", "")],
                     [("165", 1500000, "", "REF003", "", "DEALER PAYMENTS"),
                      # A SLASH INSIDE THE TEXT, which is the format's crux
                      # trap: the slash is also the record terminator, so a
                      # reader that splits on every one shatters this record
                      # into fragments.  Real files carry it constantly --
                      # moov-io's sample5 is a bug report about exactly this.
                      ("108", 275000, "", "REF004", "", "FX USD/EUR SETTLEMENT")])
    group = [rec("02", "031001234", "122099999", 1, "260915", "2359", "USD", 2) + "/"]
    group += a1 + a2
    gtotal = t1 + t2
    if corrupt:
        gtotal = gtotal + 10000
    group.append(rec("98", gtotal, 2, len(group) + 1) + "/")
    out = [rec("01", "122099999", "123456789", "260915", "0200", 1, "", "", 2) + "/"]
    out += group
    out.append(rec("99", t1 + t2, 1, len(out) + 1) + "/")
    return out


def write(path, text):
    with open(path, "w") as fh:
        fh.write(text)
    print("%-40s %6d bytes" % (path, len(text)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/finio/bai2"
    os.makedirs(out, exist_ok=True)
    recs = build()

    # (1) the ordinary case: one record per line.
    write(os.path.join(out, "statement.bai"), "\n".join(recs) + "\n")

    # (2) THE SAME LOGICAL FILE with records PACKED TWO TO A LINE, which real
    #     producers do and which a newline-per-record reader silently merges.
    packed = []
    i = 0
    while i < len(recs):
        if i + 1 < len(recs):
            packed.append(recs[i] + "   " + recs[i + 1])
            i += 2
        else:
            packed.append(recs[i])
            i += 1
    write(os.path.join(out, "packed.bai"), "\n".join(packed) + "\n")

    # (3) THE SAME FILE AGAIN with the account summaries moved onto CONTINUATION
    #     records, which is the format's defining feature: an 88 extends the
    #     previous record's FIELD LIST, so the same account reads the same way
    #     whether its summary arrived on one record or three.
    cont = []
    for r in recs:
        if r.startswith("03,"):
            f = r.rstrip("/").split(",")
            head, rest = f[:3], f[3:]
            cont.append(",".join(head + rest[:4]) + "/")
            if rest[4:]:
                cont.append("88," + ",".join(rest[4:]) + "/")
        else:
            cont.append(r)
    # the record counts change because there are more records now
    cont = renumber(cont)
    write(os.path.join(out, "continued.bai"), "\n".join(cont) + "\n")

    # (4) ONE CONTROL TOTAL CORRUPTED and nothing else.
    write(os.path.join(out, "bad_total.bai"), "\n".join(build(corrupt=True)) + "\n")


def renumber(lines):
    """Recompute the record counts after continuations changed them."""
    out = list(lines)
    # account trailers
    start = None
    for i, l in enumerate(out):
        if l.startswith("03,"):
            start = i
        if l.startswith("49,") and start is not None:
            f = l.rstrip("/").split(",")
            f[2] = str(i - start + 1)
            out[i] = ",".join(f) + "/"
            start = None
    # group trailer
    gstart = next(i for i, l in enumerate(out) if l.startswith("02,"))
    gi = next(i for i, l in enumerate(out) if l.startswith("98,"))
    f = out[gi].rstrip("/").split(","); f[3] = str(gi - gstart + 1)
    out[gi] = ",".join(f) + "/"
    # file trailer
    fi = next(i for i, l in enumerate(out) if l.startswith("99,"))
    f = out[fi].rstrip("/").split(","); f[3] = str(len(out))
    out[fi] = ",".join(f) + "/"
    return out


main()
