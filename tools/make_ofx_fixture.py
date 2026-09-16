#!/usr/bin/env python3
"""Write the OFX fixtures used by tests/run_finio_ofx.sh.

The foreign corpus (tests/finio/foreign_ofx) supplies the real dialects and
real bank output.  What it does NOT supply is THE SAME LOGICAL STATEMENT IN
BOTH DIALECTS -- every real file is one or the other -- and that is the one
comparison §20's schema-drift case actually needs: a reader that handles both
must produce ONE answer from them.  Nor does it supply a duplicate FITID, which
is the defect OFX consumers are most exposed to and which no publisher ships on
purpose.
"""
import os, sys

TXNS = [("CREDIT", "20260902120000.000", "1250.00", "FIT0001", "ACME PAYROLL", "SALARY, SEPTEMBER"),
        ("DEBIT",  "20260903120000.000", "-87.45",  "FIT0002", "CITY POWER", "ELECTRIC BILL"),
        ("CHECK",  "20260905120000.000", "-2000.00","FIT0003", "CHECK 1041", "RENT")]


def sgml(txns, dup=False):
    out = ["OFXHEADER:100", "DATA:OFXSGML", "VERSION:102", "SECURITY:NONE",
           "ENCODING:USASCII", "CHARSET:1252", "COMPRESSION:NONE",
           "OLDFILEUID:NONE", "NEWFILEUID:NONE", "", "<OFX>",
           "\t<SIGNONMSGSRSV1>", "\t\t<SONRS>", "\t\t\t<STATUS>",
           "\t\t\t\t<CODE>0", "\t\t\t\t<SEVERITY>INFO", "\t\t\t</STATUS>",
           "\t\t\t<DTSERVER>20260915120000.000", "\t\t\t<LANGUAGE>ENG",
           "\t\t</SONRS>", "\t</SIGNONMSGSRSV1>", "\t<BANKMSGSRSV1>",
           "\t\t<STMTTRNRS>", "\t\t\t<TRNUID>1", "\t\t\t<STATUS>",
           "\t\t\t\t<CODE>0", "\t\t\t\t<SEVERITY>INFO", "\t\t\t</STATUS>",
           "\t\t\t<STMTRS>", "\t\t\t\t<CURDEF>USD", "\t\t\t\t<BANKACCTFROM>",
           "\t\t\t\t\t<BANKID>021000021", "\t\t\t\t\t<ACCTID>0123456789",
           "\t\t\t\t\t<ACCTTYPE>CHECKING", "\t\t\t\t</BANKACCTFROM>",
           "\t\t\t\t<BANKTRANLIST>", "\t\t\t\t\t<DTSTART>20260901000000.000",
           "\t\t\t\t\t<DTEND>20260930000000.000"]
    for t, d, a, fid, nm, mm in txns:
        out += ["\t\t\t\t\t<STMTTRN>", "\t\t\t\t\t\t<TRNTYPE>" + t,
                "\t\t\t\t\t\t<DTPOSTED>" + d, "\t\t\t\t\t\t<TRNAMT>" + a,
                "\t\t\t\t\t\t<FITID>" + (fid if not dup else "FIT0001"),
                "\t\t\t\t\t\t<NAME>" + nm, "\t\t\t\t\t\t<MEMO>" + mm,
                "\t\t\t\t\t</STMTTRN>"]
    out += ["\t\t\t\t</BANKTRANLIST>", "\t\t\t\t<LEDGERBAL>",
            "\t\t\t\t\t<BALAMT>9162.55", "\t\t\t\t\t<DTASOF>20260930000000.000",
            "\t\t\t\t</LEDGERBAL>", "\t\t\t</STMTRS>", "\t\t</STMTTRNRS>",
            "\t</BANKMSGSRSV1>", "</OFX>"]
    return "\n".join(out) + "\n"


def xml(txns):
    """THE SAME LOGICAL STATEMENT, serialized the 2.x way: every leaf closed."""
    lines = sgml(txns).split("\n")
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<?OFX OFXHEADER="200" VERSION="211" SECURITY="NONE" OLDFILEUID="NONE" NEWFILEUID="NONE"?>']
    for ln in lines:
        s = ln.strip()
        if not s or ":" in s and "<" not in s:
            continue
        if s.startswith("</") or s.endswith(">") and "<" in s and ">" == s[-1] and s.count("<") == 1 and not s[s.index(">")+1:]:
            out.append(ln)           # an aggregate open or close, unchanged
        elif s.startswith("<"):
            tag = s[1:s.index(">")]
            val = s[s.index(">") + 1:]
            out.append(ln + "</" + tag + ">")
        else:
            out.append(ln)
    return "\n".join(out) + "\n"


def write(path, text):
    with open(path, "w") as fh:
        fh.write(text)
    print("%-38s %6d bytes" % (path, len(text)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/finio/ofx"
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "statement_v1.ofx"), sgml(TXNS))
    write(os.path.join(out, "statement_v2.ofx"), xml(TXNS))
    write(os.path.join(out, "duplicate_fitid.ofx"), sgml(TXNS, dup=True))


main()
