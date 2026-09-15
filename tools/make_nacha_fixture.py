#!/usr/bin/env python3
"""Write the NACHA fixtures used by tests/run_finio_nacha.sh.

THE POINT OF GENERATING THEM HERE RATHER THAN IN gBASIC is that the control
records -- the entry counts, the routing-number hash and the debit and credit
totals -- are computed by an implementation that is not the one under test.  A
fixture whose totals came from `finio_nacha` would agree with `finio_nacha`
however wrong both were, and the suite's load-bearing tier would be a
transcript.  These are written from the field layout alone.

The content is fabricated.  No file here was produced by a bank, no routing
number belongs to one (021000021 is a widely published example value and the
account numbers are sequential), and §9's registry entry says so.
"""
import os, sys

W = 94
CREDIT = {"22", "23", "24", "32", "33", "34", "42", "43", "44", "52", "53", "54"}
DEBIT  = {"27", "28", "29", "37", "38", "39", "47", "48", "49", "55", "56"}


def L(s, n):   # left-justified, space filled -- alphameric fields
    b = s.encode("utf-8")
    if len(b) > n:
        raise SystemExit("field overflows %d bytes: %r" % (n, s))
    return b + b" " * (n - len(b))


def R(v, n):   # right-justified, zero filled -- numeric fields
    s = str(v)
    if len(s) > n:
        raise SystemExit("numeric field overflows %d: %r" % (n, s))
    return s.encode("ascii").rjust(n, b"0")


def file_header():
    return (b"1" + b"01" + L(" 021000021", 10) + L("1234567890", 10)
            + b"260914" + b"0830" + b"A" + b"094" + b"10" + b"1"
            + L("FIRST NATIONAL BANK", 23) + L("ACME WIDGETS INC", 23) + L("", 8))


def batch_header(service_class, company, entry_desc, eff, odfi, number):
    return (b"5" + R(service_class, 3) + L(company, 16) + L("", 20)
            + L("1234567890", 10) + b"PPD" + L(entry_desc, 10) + L("", 6)
            + eff.encode() + L("", 3) + b"1" + odfi.encode() + R(number, 7))


def entry(txn, rdfi, check, account, cents, indiv_id, name, addenda, trace):
    return (b"6" + txn.encode() + rdfi.encode() + check.encode() + L(account, 17)
            + R(cents, 10) + L(indiv_id, 15) + L(name, 22) + L("", 2)
            + str(addenda).encode() + trace.encode())


def addenda_rec(info, seq, entry_seq):
    return (b"7" + b"05" + L(info, 80) + R(seq, 4) + R(entry_seq, 7))


def batch_control(service_class, count, hash10, debit, credit, odfi, number):
    return (b"8" + R(service_class, 3) + R(count, 6) + R(hash10, 10)
            + R(debit, 12) + R(credit, 12) + L("1234567890", 10) + L("", 19)
            + L("", 6) + odfi.encode() + R(number, 7))


def file_control(batches, blocks, count, hash10, debit, credit):
    return (b"9" + R(batches, 6) + R(blocks, 6) + R(count, 8) + R(hash10, 10)
            + R(debit, 12) + R(credit, 12) + L("", 39))


def totals(entries):
    """The arithmetic every control record states, computed here."""
    n = 0
    h = 0
    d = 0
    c = 0
    for rec in entries:
        if rec[:1] == b"6":
            n += 1
            h += int(rec[3:11])
            txn = rec[1:3].decode()
            amt = int(rec[29:39])
            if txn in DEBIT:
                d += amt
            elif txn in CREDIT:
                c += amt
        elif rec[:1] == b"7":
            n += 1
    return n, h, d, c


def build(account_suffix=""):
    """One logical file: two batches, an addenda, credits and debits."""
    b1 = []
    b1.append(entry("22", "02100002", "1", "CHK00000000001" + account_suffix,
                    125000, "EMP0001", "ALICE MERCER", 0, "021000021000001"))
    b1.append(entry("22", "02100002", "1", "CHK00000000002",
                    98750, "EMP0002", "BRENDAN OKAFOR", 1, "021000021000002"))
    b1.append(addenda_rec("PAYROLL 2026-09-16", 1, 2))
    b1.append(entry("22", "31100004", "7", "CHK00000000003",
                    210000, "EMP0003", "CHIARA ROSSI", 0, "021000021000003"))
    n1, h1, d1, c1 = totals(b1)

    b2 = []
    b2.append(entry("27", "02100002", "1", "CHK00000000004",
                    45000, "LOAN001", "DIEGO SANTOS", 0, "021000021000004"))
    b2.append(entry("27", "26100053", "2", "CHK00000000005",
                    45000, "LOAN002", "EVA LINDQVIST", 0, "021000021000005"))
    n2, h2, d2, c2 = totals(b2)

    # A THIRD BATCH BIG ENOUGH THAT THE HASH WRAPS.  The entry hash is the sum
    # of the eight-digit routing numbers with only the RIGHTMOST TEN DIGITS
    # kept, and with two small batches that truncation never fires -- so the
    # code implementing it would be dead and the suite would assert nothing
    # about it.  120 entries at 99999999 sum past 10^10, which is the smallest
    # arrangement that makes the rule observable.
    b3 = []
    for i in range(120):
        b3.append(entry("22", "99999999", "8", "CHK1%08d" % i, 100 + i,
                        "BULK%04d" % i, "BULK PAYEE %04d" % i, 0,
                        "0210000211%05d" % i))
    n3, h3, d3, c3 = totals(b3)
    assert h3 > 10**10, h3

    recs = [file_header()]
    recs.append(batch_header(220, "ACME WIDGETS", "PAYROLL", "260916", "02100002", 1))
    recs.extend(b1)
    recs.append(batch_control(220, n1, h1 % 10**10, d1, c1, "02100002", 1))
    recs.append(batch_header(225, "ACME WIDGETS", "LOAN PMT", "260916", "02100002", 2))
    recs.extend(b2)
    recs.append(batch_control(225, n2, h2 % 10**10, d2, c2, "02100002", 2))
    recs.append(batch_header(220, "ACME WIDGETS", "BULK PAY", "260916", "02100002", 3))
    recs.extend(b3)
    recs.append(batch_control(220, n3, h3 % 10**10, d3, c3, "02100002", 3))

    n, h, d, c = n1 + n2 + n3, h1 + h2 + h3, d1 + d2 + d3, c1 + c2 + c3
    physical = len(recs) + 1
    blocks = (physical + 9) // 10
    padding = blocks * 10 - physical
    recs.append(file_control(3, blocks, n, h % 10**10, d, c))
    recs.extend([b"9" * W] * padding)
    for r in recs:
        assert len(r) == W, (len(r), r)
    return recs


def write(path, data):
    with open(path, "wb") as fh:
        fh.write(data)
    print("%-42s %6d bytes" % (path, len(data)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/finio/nacha"
    os.makedirs(out, exist_ok=True)
    recs = build()

    # (1) the ordinary case: one record per line.
    write(os.path.join(out, "payroll.ach"), b"\n".join(recs) + b"\n")

    # (2) THE SAME LOGICAL FILE with no record separator at all -- how a great
    #     many real ACH files arrive off a mainframe.  A newline-assuming
    #     reader sees one 1880-byte record.
    write(os.path.join(out, "blocked.ach"), b"".join(recs))

    # (3) the same file again with CRLF, because a Windows-produced file is
    #     ordinary and byte fidelity means reproducing the separator it had.
    write(os.path.join(out, "crlf.ach"), b"\r\n".join(recs) + b"\r\n")

    # (4) ONE CONTROL TOTAL CORRUPTED, and nothing else.  The file is still a
    #     perfectly well-formed ACH file: every record is 94 bytes, the type
    #     sequence is valid, and only its own arithmetic betrays it.
    bad = list(recs)
    for i, r in enumerate(bad):
        if r[:1] == b"8" and r[1:4] == b"220":
            bad[i] = r[:32] + R(int(r[32:44]) + 1000, 12) + r[44:]
            break
    write(os.path.join(out, "bad_total.ach"), b"\n".join(bad) + b"\n")

    # (5) A NON-ASCII BYTE IN A FIELD THAT PRECEDES THE AMOUNT.  The record is
    #     still 94 BYTES; it is 93 codepoints.  A reader slicing by codepoint
    #     reads every later field one place left, so the amount comes back as
    #     an ordinary-looking digit string that is not the amount -- and the
    #     file's own totals stop reconciling for a reason nothing names.
    latin = build(account_suffix="")
    for i, r in enumerate(latin):
        if r[:1] == b"6" and b"ALICE" in r:
            acct = "CHKÉ000000001"          # E-acute: two bytes in UTF-8
            latin[i] = r[:12] + L(acct, 17) + r[29:]
            assert len(latin[i]) == W
            break
    write(os.path.join(out, "accented.ach"), b"\n".join(latin) + b"\n")

    # (6) not an ACH file at all.
    write(os.path.join(out, "not_ach.txt"),
          b"date,amount,payee\n2026-09-14,125.00,ALICE MERCER\n")

    # (7) 94-byte records that are NOT ACH -- the near miss.  Recognition must
    #     not answer `exact` merely because the widths line up.
    near = [L("this file has ninety-four byte records and is not an ACH file", W)
            for _ in range(20)]
    write(os.path.join(out, "near_miss.dat"), b"\n".join(near) + b"\n")

    # (8) THE SHARPER NEAR MISS: a real file whose records run in an IMPOSSIBLE
    #     ORDER.  (7) is rejected on its first byte and so exercises nothing;
    #     this one has the right widths, the right header and real ACH records,
    #     and an entry detail sitting outside any batch.  Recognition must say
    #     `possible` rather than `exact`, and validation -- which asks the
    #     different question -- must name the sequence.
    jumbled = list(recs)
    jumbled[1], jumbled[2] = jumbled[2], jumbled[1]   # entry before its batch header
    write(os.path.join(out, "out_of_order.ach"), b"\n".join(jumbled) + b"\n")


main()
