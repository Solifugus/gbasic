#!/usr/bin/env python3
"""Write the camt.053 fixtures used by tests/run_finio_camt.sh.

AS WITH NACHA, THE ARITHMETIC IS COMPUTED HERE so that the file's own declared
balances are an oracle rather than a transcript of what `finio_camt` says. A
camt.053 statement carries an OPENING balance (OPBD) and a CLOSING one (CLBD),
and the entries between them must account for the difference -- the same
self-checking property NACHA's control records have, differently expressed.
Optional <TxsSummry> totals are written too, because an adapter that read the
entries correctly and summed them wrongly would agree with the balances and
disagree with the summary.

The content is fabricated.  No file here came from a bank, no IBAN belongs to
one, and the registry entry says so.
"""
import os, sys
from decimal import Decimal

NS8 = "urn:iso:std:iso:20022:tech:xsd:camt.053.001.08"


def money(d):
    return "%.2f" % d


def balance(code, amount, ccy, ind, date):
    return f"""      <Bal>
        <Tp><CdOrPrtry><Cd>{code}</Cd></CdOrPrtry></Tp>
        <Amt Ccy="{ccy}">{money(amount)}</Amt>
        <CdtDbtInd>{ind}</CdtDbtInd>
        <Dt><Dt>{date}</Dt></Dt>
      </Bal>"""


def entry(ref, amount, ccy, ind, booked, value, code, details=None):
    d = ""
    if details:
        d = f"""
        <NtryDtls>
          <TxDtls>
            <Refs><EndToEndId>{details}</EndToEndId></Refs>
            <RltdPties><Cdtr><Pty><Nm>{details} LTD</Nm></Pty></Cdtr></RltdPties>
          </TxDtls>
        </NtryDtls>"""
    return f"""      <Ntry>
        <NtryRef>{ref}</NtryRef>
        <Amt Ccy="{ccy}">{money(amount)}</Amt>
        <CdtDbtInd>{ind}</CdtDbtInd>
        <Sts><Cd>BOOK</Cd></Sts>
        <BookgDt><Dt>{booked}</Dt></BookgDt>
        <ValDt><Dt>{value}</Dt></ValDt>
        <BkTxCd><Domn><Cd>PMNT</Cd><Fmly><Cd>{code}</Cd><SubFmlyCd>ESCT</SubFmlyCd></Fmly></Domn></BkTxCd>{d}
      </Ntry>"""


def statement(stmt_id, iban, ccy, owner, opening, entries, start, end,
              summary=True, closing_override=None):
    """entries: list of (ref, amount, ind, booked, value, code, details)"""
    credits = sum(e[1] for e in entries if e[2] == "CRDT")
    debits = sum(e[1] for e in entries if e[2] == "DBIT")
    closing = opening + credits - debits
    if closing_override is not None:
        closing = closing_override
    body = []
    body.append(f"""    <Stmt>
      <Id>{stmt_id}</Id>
      <CreDtTm>{end}T23:59:00</CreDtTm>
      <FrToDt><FrDtTm>{start}T00:00:00</FrDtTm><ToDtTm>{end}T23:59:59</ToDtTm></FrToDt>
      <Acct>
        <Id><IBAN>{iban}</IBAN></Id>
        <Ccy>{ccy}</Ccy>
        <Ownr><Nm>{owner}</Nm></Ownr>
        <Svcr><FinInstnId><BICFI>TESTGB2LXXX</BICFI></FinInstnId></Svcr>
      </Acct>""")
    body.append(balance("OPBD", opening, ccy, "CRDT", start))
    body.append(balance("CLBD", closing, ccy, "CRDT", end))
    if summary:
        body.append(f"""      <TxsSummry>
        <TtlNtries><NbOfNtries>{len(entries)}</NbOfNtries></TtlNtries>
        <TtlCdtNtries><NbOfNtries>{sum(1 for e in entries if e[2] == 'CRDT')}</NbOfNtries><Sum>{money(credits)}</Sum></TtlCdtNtries>
        <TtlDbtNtries><NbOfNtries>{sum(1 for e in entries if e[2] == 'DBIT')}</NbOfNtries><Sum>{money(debits)}</Sum></TtlDbtNtries>
      </TxsSummry>""")
    for e in entries:
        body.append(entry(e[0], e[1], ccy, e[2], e[3], e[4], e[5], e[6]))
    body.append("    </Stmt>")
    return "\n".join(body)


def document(stmts, ns=NS8, msg_id="MSG-2026-09-14-001"):
    return (f"""<?xml version="1.0" encoding="UTF-8"?>
<Document xmlns="{ns}">
  <BkToCstmrStmt>
    <GrpHdr>
      <MsgId>{msg_id}</MsgId>
      <CreDtTm>2026-09-14T08:30:00</CreDtTm>
      <MsgRcpt><Nm>ACME WIDGETS INC</Nm></MsgRcpt>
    </GrpHdr>
"""
            + "\n".join(stmts)
            + """
  </BkToCstmrStmt>
</Document>
""")


D = Decimal

# One statement, mixed entries, one carrying the optional NtryDtls nesting and
# the others not -- the optionality is the shape a fixed-width format has no
# equivalent for.
ENTRIES = [
    ("NTRY-0001", D("1250.00"), "CRDT", "2026-09-02", "2026-09-02", "RCDT", "ORION"),
    ("NTRY-0002", D("87.45"),   "DBIT", "2026-09-03", "2026-09-03", "ICDT", None),
    ("NTRY-0003", D("4300.10"), "CRDT", "2026-09-05", "2026-09-05", "RCDT", None),
    ("NTRY-0004", D("2000.00"), "DBIT", "2026-09-08", "2026-09-09", "ICDT", "VENDOR"),
    ("NTRY-0005", D("15.99"),   "DBIT", "2026-09-11", "2026-09-11", "ICDT", None),
]
# A second statement on a different account and a DIFFERENT CURRENCY, so a
# reader that totals across statements rather than within them is caught -- and
# so `money`'s refusal to add different currencies is exercised where it lands.
ENTRIES_EUR = [
    ("EUR-0001", D("500.00"), "CRDT", "2026-09-04", "2026-09-04", "RCDT", None),
    ("EUR-0002", D("120.50"), "DBIT", "2026-09-06", "2026-09-06", "ICDT", None),
]


def write(path, text):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("%-44s %6d bytes" % (path, len(text.encode("utf-8"))))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/finio/camt"
    os.makedirs(out, exist_ok=True)

    s1 = statement("STMT-2026-09", "GB29TEST60161331926819", "USD",
                   "ACME WIDGETS INC", D("10000.00"), ENTRIES,
                   "2026-09-01", "2026-09-12")
    s2 = statement("STMT-2026-09-EUR", "DE89TEST70010001234567", "EUR",
                   "ACME WIDGETS INC", D("2500.00"), ENTRIES_EUR,
                   "2026-09-01", "2026-09-12")
    write(os.path.join(out, "statement.xml"), document([s1, s2]))

    # THE CORRUPTED ONE: the closing balance is wrong by 10.00 and nothing else
    # is touched.  The document is still valid camt -- only its own arithmetic
    # betrays it, exactly as with NACHA's bad_total.ach.
    bad = statement("STMT-2026-09", "GB29TEST60161331926819", "USD",
                    "ACME WIDGETS INC", D("10000.00"), ENTRIES,
                    "2026-09-01", "2026-09-12",
                    closing_override=D("10000.00") + D("1250.00") + D("4300.10")
                    - D("87.45") - D("2000.00") - D("15.99") + D("10.00"))
    write(os.path.join(out, "bad_balance.xml"), document([bad]))

    # A VERSION THIS ADAPTER DOES NOT IMPLEMENT.  The namespace carries it, so
    # unlike NACHA the revision IS determinable -- and a file declaring one the
    # adapter has never seen must be refused BY NAME rather than read under a
    # guess.
    write(os.path.join(out, "older_version.xml"),
          document([s1], ns="urn:iso:std:iso:20022:tech:xsd:camt.053.001.02"))

    # XML that is not camt at all.
    write(os.path.join(out, "not_camt.xml"),
          '<?xml version="1.0"?>\n<invoice><total>125.00</total></invoice>\n')

    # AN ENTRY IN A CURRENCY THE ACCOUNT IS NOT HELD IN.  A real defect, and one
    # `money` cannot be made to paper over: adding different currencies raises.
    mixed = statement("STMT-MIXED", "GB29TEST60161331926819", "USD",
                      "ACME WIDGETS INC", D("10000.00"), ENTRIES,
                      "2026-09-01", "2026-09-12", summary=False)
    mixed = mixed.replace('<Amt Ccy="USD">87.45</Amt>', '<Amt Ccy="GBP">87.45</Amt>', 1)
    write(os.path.join(out, "mixed_currency.xml"), document([mixed]))


main()
