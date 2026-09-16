#!/usr/bin/env python3
"""Write the pain.001 fixtures used by tests/run_finio_pain001.sh.

The foreign corpus supplies real SEPA credit transfers -- and every one of them
has a SINGLE payment-information block, so its group control sum is a copy of
the block's rather than a total.  A two-level checksum tested only where the
two levels are equal tests one level.

It also supplies nothing that violates the scheme, because no publisher ships
that on purpose: a creditor name past 70 characters, an amount in a currency
the scheme does not carry, a block mixing currencies so its control sum totals
two.  Those are what §16's write classification exists for.
"""
import os, sys
from decimal import Decimal

NS = "urn:iso:std:iso:20022:tech:xsd:pain.001.001.03"


def tx(e2e, amount, name, iban, bic, memo, ccy="EUR", instr=None):
    i = "        <InstrId>%s</InstrId>\n" % instr if instr else ""
    return f"""      <CdtTrfTxInf>
        <PmtId>
{i}          <EndToEndId>{e2e}</EndToEndId>
        </PmtId>
        <Amt>
          <InstdAmt Ccy="{ccy}">{amount}</InstdAmt>
        </Amt>
        <CdtrAgt><FinInstnId><BIC>{bic}</BIC></FinInstnId></CdtrAgt>
        <Cdtr><Nm>{name}</Nm></Cdtr>
        <CdtrAcct><Id><IBAN>{iban}</IBAN></Id></CdtrAcct>
        <RmtInf><Ustrd>{memo}</Ustrd></RmtInf>
      </CdtTrfTxInf>"""


def block(pid, debtor, iban, bic, date, txs, ctrl=None, count=None):
    total = ctrl if ctrl is not None else sum(Decimal(t[1]) for t in txs)
    n = count if count is not None else len(txs)
    body = "\n".join(tx(*t) for t in txs)
    return f"""    <PmtInf>
      <PmtInfId>{pid}</PmtInfId>
      <PmtMtd>TRF</PmtMtd>
      <BtchBookg>false</BtchBookg>
      <NbOfTxs>{n}</NbOfTxs>
      <CtrlSum>{total}</CtrlSum>
      <PmtTpInf><SvcLvl><Cd>SEPA</Cd></SvcLvl></PmtTpInf>
      <ReqdExctnDt>{date}</ReqdExctnDt>
      <Dbtr><Nm>{debtor}</Nm></Dbtr>
      <DbtrAcct><Id><IBAN>{iban}</IBAN></Id></DbtrAcct>
      <DbtrAgt><FinInstnId><BIC>{bic}</BIC></FinInstnId></DbtrAgt>
      <ChrgBr>SLEV</ChrgBr>
{body}
    </PmtInf>"""


def document(blocks, txs_total, ctrl_total, msg="MSG-20260915-001"):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Document xmlns="{NS}">
  <CstmrCdtTrfInitn>
    <GrpHdr>
      <MsgId>{msg}</MsgId>
      <CreDtTm>2026-09-15T10:30:00</CreDtTm>
      <NbOfTxs>{txs_total}</NbOfTxs>
      <CtrlSum>{ctrl_total}</CtrlSum>
      <InitgPty><Nm>Acme Widgets Ltd</Nm></InitgPty>
    </GrpHdr>
{chr(10).join(blocks)}
  </CstmrCdtTrfInitn>
</Document>
"""


B1 = [("E2E-0001", "1250.00", "Fornitore SpA", "IT60X0542811101000000123456", "UNCRITM1Z70", "INVOICE 4471"),
      ("E2E-0002", "87.45", "City Power", "DE89370400440532013000", "COBADEFFXXX", "ELECTRIC, SEPTEMBER")]
B2 = [("E2E-0003", "4300.10", "Orion Logistics", "FR1420041010050500013M02606", "PSSTFRPPXXX", "FREIGHT Q3"),
      ("E2E-0004", "2000.00", "Landlord BV", "NL91ABNA0417164300", "ABNANL2AXXX", "RENT OCTOBER")]


def write(path, text):
    with open(path, "w") as fh:
        fh.write(text)
    print("%-40s %6d bytes" % (path, len(text)))


def two_blocks(**kw):
    b1 = block("PAY-A", "Acme Widgets Ltd", "GB29NWBK60161331926819", "NWBKGB2L", "2026-09-20", B1, **kw)
    b2 = block("PAY-B", "Acme Widgets Ltd", "GB29NWBK60161331926819", "NWBKGB2L", "2026-09-22", B2)
    total = sum(Decimal(t[1]) for t in B1 + B2)
    return [b1, b2], len(B1) + len(B2), total


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/finio/pain"
    os.makedirs(out, exist_ok=True)

    blocks, n, total = two_blocks()
    write(os.path.join(out, "payments.xml"), document(blocks, n, total))

    # THE GROUP TOTAL WRONG BY 10, and nothing else -- the level the corpus
    # cannot exercise, since with one block the two levels are the same number.
    write(os.path.join(out, "bad_group_sum.xml"), document(blocks, n, total + 10))

    # A BLOCK's OWN control sum wrong, group right.
    # 1337.45 was the first value here and is EXACTLY the correct total for
    # this block -- a "corrupted" fixture that corrupted nothing and reported
    # clean.  Picked by hand, from the same arithmetic the block already had.
    bad_blocks, bn, btotal = two_blocks(ctrl=Decimal("1437.45"))
    write(os.path.join(out, "bad_block_sum.xml"), document(bad_blocks, bn, btotal))

    # LOSSY: a creditor name past the 70 the scheme carries.
    long_name = "Fornitore Internazionale di Componenti Elettronici e Meccanici SpA Milano"
    lb = [(e, a, long_name if i == 0 else n2, ib, bc, m)
          for i, (e, a, n2, ib, bc, m) in enumerate(B1)]
    b = block("PAY-A", "Acme Widgets Ltd", "GB29NWBK60161331926819", "NWBKGB2L", "2026-09-20", lb)
    write(os.path.join(out, "long_name.xml"),
          document([b], len(lb), sum(Decimal(t[1]) for t in lb)))

    # IMPOSSIBLE: an amount in a currency the SEPA scheme does not carry.
    ub = [(B1[0][0], B1[0][1], B1[0][2], B1[0][3], B1[0][4], B1[0][5], "USD")]
    b = block("PAY-A", "Acme Widgets Ltd", "GB29NWBK60161331926819", "NWBKGB2L", "2026-09-20", ub)
    write(os.path.join(out, "wrong_currency.xml"),
          document([b], 1, Decimal(B1[0][1])))

    # A BLOCK MIXING CURRENCIES, so its control sum totals two of them.
    mb = [B1[0], (B1[1][0], B1[1][1], B1[1][2], B1[1][3], B1[1][4], B1[1][5], "USD")]
    b = block("PAY-A", "Acme Widgets Ltd", "GB29NWBK60161331926819", "NWBKGB2L", "2026-09-20", mb)
    write(os.path.join(out, "mixed_currency.xml"),
          document([b], 2, sum(Decimal(t[1]) for t in mb)))


main()
