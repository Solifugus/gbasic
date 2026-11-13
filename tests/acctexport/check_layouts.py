#!/usr/bin/env python3
"""Holds acctexport's files to the documented layouts, from outside.

    check_layouts.py DIR [IIF_KIT_DIR]

DIR is what tests/acctexport/emit.bas wrote. Nothing the library computed is
taken on trust: every amount below is the fixture's, worked out by hand in
tests/acctexport/axfixture.bas's header; every layout rule is the vendor's, as
docs/accounting_export_design.md §3 records it with its source. Files are read
with Python's csv module, amounts with decimal.Decimal.

With IIF_KIT_DIR (the folder of Intuit's "QB 2007 & Enterprise 7.0 and later"
IIF examples, from iif_import_kit.zip), the IIF headers are also held to the
kit's own example files. Without it, to the column names written down from
them.

This checks LOCAL FORMAT only. Nothing here imports a file into QuickBooks or
Xero.
"""
import csv
import hashlib
import io
import json
import os
import re
import sys
from decimal import Decimal

checks = bad = 0


def check(label, ok, detail=""):
    global checks, bad
    checks += 1
    if not ok:
        bad += 1
        print("MISMATCH %s %s" % (label, detail))


def rows_of(path):
    raw = open(path, "rb").read()
    text = raw.decode("utf-8")
    return raw, list(csv.reader(io.StringIO(text, newline="")))


def crlf_only(raw):
    # every line break outside quotes is CRLF; inside a quoted field a bare LF is content
    return b"\r\n" in raw and raw.endswith(b"\r\n")


def manifest_ok(d, name):
    m = json.load(open(os.path.join(d, "manifest.json"), encoding="utf-8"))
    check(name + ": manifest says generated, not imported", m["status"] == "generated")
    for f in m["files"]:
        raw = open(os.path.join(d, f["name"]), "rb").read()
        check(name + ": " + f["name"] + " checksum", hashlib.sha256(raw).hexdigest() == f["sha256"])
        check(name + ": " + f["name"] + " bytes", len(raw) == f["bytes"])
        if f["format"] == "csv":
            _, rows = rows_of(os.path.join(d, f["name"]))
            check(name + ": " + f["name"] + " row count", len(rows) - 1 == f["rows"], "%d vs %d" % (len(rows) - 1, f["rows"]))
    return m


D = Decimal

# ---- general --------------------------------------------------------------
GENERAL = {
    "customers.csv": ["customer_id", "name", "email", "address_line1", "address_line2", "city", "region", "postal_code", "country"],
    "invoices.csv": ["invoice_id", "number", "customer_id", "issue_date", "due_date", "currency", "subtotal", "tax_total", "total", "memo"],
    "invoice_lines.csv": ["invoice_id", "line_id", "line_no", "description", "quantity", "unit_price", "discount", "amount", "tax",
                          "tax_code", "category", "item", "service_from", "service_to"],
    "payments.csv": ["payment_id", "customer_id", "date", "currency", "gross_amount", "allocated", "unallocated", "method",
                     "reference", "processor", "payout_id"],
    "payment_allocations.csv": ["payment_id", "invoice_id", "currency", "amount"],
    "refunds.csv": ["refund_id", "customer_id", "date", "currency", "amount", "payment_id", "credit_note_id", "reference",
                    "processor", "payout_id"],
    "fees.csv": ["fee_id", "date", "currency", "amount", "processor", "payment_id", "payout_id", "description"],
    "payouts.csv": ["payout_id", "date", "currency", "gross_payments", "refunds", "fees", "net_amount", "processor", "reference"],
    "payout_items.csv": ["payout_id", "item_type", "item_id", "currency", "effect_on_payout"],
}


def general(d):
    manifest_ok(d, "general")
    t = {}
    for f, head in GENERAL.items():
        raw, rows = rows_of(os.path.join(d, f))
        check("general " + f + " header", rows[0] == head, rows[0])
        check("general " + f + " CRLF", crlf_only(raw))
        check("general " + f + " UTF-8 with no byte-order mark", not raw.startswith(b"\xef\xbb\xbf"))
        t[f] = [dict(zip(rows[0], r)) for r in rows[1:]]
    cust = {r["customer_id"]: r for r in t["customers.csv"]}
    check("Unicode survives", cust["c1"]["name"] == "Łódź Print House" and cust["c3"]["name"] == "Café Ñandú GmbH")
    check("quotes and a comma survive", cust["c2"]["name"] == 'Acme "Quoted", Inc.')
    lines = t["invoice_lines.csv"]
    check("a two-line description survives", lines[0]["description"] == "Team plan, per seat\nJanuary 2027")
    inv = {r["number"]: r for r in t["invoices.csv"]}
    want = {"WS-0001": ("USD", "237.00"), "WS-0002": ("USD", "1.07"), "WS-0004": ("EUR", "59.50")}
    for n, (c, tot) in want.items():
        check("invoice %s total" % n, (inv[n]["currency"], inv[n]["total"]) == (c, tot), inv[n])
        mine = [l for l in lines if l["invoice_id"] == inv[n]["invoice_id"]]
        s = sum(D(l["amount"]) + D(l["tax"]) for l in mine)
        check("invoice %s lines sum to its total" % n, s == D(tot), str(s))
    l3 = [l for l in lines if l["line_id"] == "l3"][0]
    check("unit price keeps every digit; the amount is at the minor unit", (l3["unit_price"], l3["amount"]) == ("0.333", "1.00"), l3)
    pays = {r["payment_id"]: r for r in t["payments.csv"]}
    for p in pays.values():
        check("payment %s: unallocated is gross less allocated" % p["payment_id"],
              D(p["unallocated"]) == D(p["gross_amount"]) - D(p["allocated"]))
    al = sum(D(a["amount"]) for a in t["payment_allocations.csv"] if a["invoice_id"] == "inv1")
    check("inv1: 237.00 less the 49.00 credit note is paid by 100.00 and 88.00", al == D("188.00"), str(al))
    po = {r["payout_id"]: r for r in t["payouts.csv"]}
    check("payout nets by hand", (po["po1"]["net_amount"], po["po2"]["net_amount"]) == ("172.66", "58.30"))
    for pid, p in po.items():
        eff = sum(D(i["effect_on_payout"]) for i in t["payout_items.csv"] if i["payout_id"] == pid)
        check("payout %s is its items" % pid, eff == D(p["net_amount"]), str(eff))
        check("payout %s: gross less refunds and fees" % pid,
              D(p["gross_payments"]) - D(p["refunds"]) - D(p["fees"]) == D(p["net_amount"]))
    check("gross receipts, fees and the net payout are three amounts",
          (po["po1"]["gross_payments"], po["po1"]["fees"], po["po1"]["net_amount"]) == ("189.07", "6.41", "172.66"))


# ---- QuickBooks Online invoices --------------------------------------------
QBO_HEAD = ["InvoiceNo", "Customer", "InvoiceDate", "DueDate", "Terms", "Memo", "Item(Product/Service)", "ItemDescription",
            "ItemQuantity", "ItemRate", "ItemAmount", "Service Date"]


def qbo_files(d):
    return sorted(f for f in os.listdir(d) if f.startswith("qbo_invoices_"))


def qbo(d, name, region, totals, date_re):
    manifest_ok(d, name)
    head = QBO_HEAD + (["ItemTaxCode"] if region != "us" else [])
    seen = {}
    for f in qbo_files(d):
        raw, rows = rows_of(os.path.join(d, f))
        check(name + " " + f + " header", rows[0] == head, rows[0])
        body = [dict(zip(rows[0], r)) for r in rows[1:]]
        check(name + " " + f + " at most 1,000 rows (Intuit)", len(body) <= 1000, len(body))
        nos = sorted(set(r["InvoiceNo"] for r in body))
        check(name + " " + f + " at most 100 invoices (Intuit)", len(nos) <= 100, len(nos))
        for n in nos:
            check(name + " invoice " + n + " in one file only", n not in seen, f)
            seen[n] = f
        for r in body:
            for c in ["InvoiceNo", "Customer", "InvoiceDate", "DueDate", "ItemAmount"]:
                check(name + " required " + c, r[c] != "", r)
            check(name + " date format", re.fullmatch(date_re, r["InvoiceDate"]) is not None, r["InvoiceDate"])
            check(name + " no negative charges (Intuit)", not r["ItemAmount"].startswith("-"))
            if region != "us":
                check(name + " ItemTaxCode", r["ItemTaxCode"] != "")
        for n, tot in totals.items():
            mine = [r for r in body if r["InvoiceNo"] == n]
            if mine:
                check(name + " " + n + " amounts", sum(D(r["ItemAmount"]) for r in mine) == D(tot))
    return seen


# ---- Xero --------------------------------------------------------------------
XERO_HEAD = ["*ContactName", "EmailAddress", "POAddressLine1", "POAddressLine2", "POAddressLine3", "POAddressLine4", "POCity",
             "PORegion", "POPostalCode", "POCountry", "*InvoiceNumber", "Reference", "*InvoiceDate", "*DueDate", "Total",
             "InventoryItemCode", "*Description", "*Quantity", "*UnitAmount", "Discount", "*AccountCode", "*TaxType", "TaxAmount",
             "TrackingName1", "TrackingOption1", "TrackingName2", "TrackingOption2", "Currency", "BrandingTheme"]


def xero(d, name, sign, totals):
    manifest_ok(d, name)
    seen = {}
    for f in sorted(x for x in os.listdir(d) if x.endswith(".csv")):
        raw, rows = rows_of(os.path.join(d, f))
        check(name + " " + f + " header", rows[0] == XERO_HEAD, rows[0])
        body = [dict(zip(rows[0], r)) for r in rows[1:]]
        check(name + " " + f + " at most 500 rows (Xero's recommendation)", len(body) <= 500, len(body))
        for n in set(r["*InvoiceNumber"] for r in body):
            check(name + " " + n + " in one file only", n not in seen, f)
            seen[n] = f
        for r in body:
            for c in ["*ContactName", "*InvoiceNumber", "*InvoiceDate", "*DueDate", "*Description", "*Quantity", "*UnitAmount",
                      "*AccountCode", "*TaxType"]:
                check(name + " required " + c, r[c] != "", r)
            check(name + " DD/MM/YYYY (Xero)", re.fullmatch(r"\d\d/\d\d/\d{4}", r["*InvoiceDate"]) is not None, r["*InvoiceDate"])
            ua = D(r["*UnitAmount"])
            check(name + " UnitAmount sign (credit notes negative, Xero)", (ua < 0) == (sign < 0), r["*UnitAmount"])
        for n, tot in totals.items():
            mine = [r for r in body if r["*InvoiceNumber"] == n]
            if mine:
                # Xero's own arithmetic: quantity x unit x (1 - discount%) plus the tax given
                s = sum(D(r["*Quantity"]) * abs(D(r["*UnitAmount"])) * (1 - D(r["Discount"] or "0") / 100) + D(r["TaxAmount"] or "0")
                        for r in mine)
                check(name + " " + n + " as Xero computes it", s.quantize(D("0.01")) == D(tot), str(s))
    return seen


# ---- bank statements -------------------------------------------------------
def bank(d, name, vendor, account):
    manifest_ok(d, name)
    f = [x for x in os.listdir(d) if x.endswith(".csv")][0]
    raw, rows = rows_of(os.path.join(d, f))
    head = ["Date", "Description", "Amount"] if vendor == "qbo" else ["Date", "Amount", "Payee", "Description", "Reference"]
    check(name + " header", rows[0] == head, rows[0])
    body = [dict(zip(rows[0], r)) for r in rows[1:]]
    amounts = [D(r["Amount"]) for r in body]
    for r in body:
        check(name + " DD/MM/YYYY", re.fullmatch(r"\d\d/\d\d/\d{4}", r["Date"]) is not None)
    if account == "bank":
        check(name + ": the bank sees the payout, net", amounts == [D("172.66")], amounts)
        check(name + ": on its date, DD/MM/YYYY", body[0]["Date"] == "10/02/2027", body[0]["Date"])
    else:
        # in: payments 189.07; out: refund 10.00, fees 6.41, payout 172.66 -- the clearing account empties
        check(name + ": the clearing account empties", sum(amounts) == 0, str(sum(amounts)))
        check(name + ": gross in, fees and refund and payout out", sorted(amounts) ==
              sorted([D("100.00"), D("88.00"), D("1.07"), D("-10.00"), D("-3.20"), D("-2.88"), D("-0.33"), D("-172.66")]), amounts)


# ---- IIF ---------------------------------------------------------------------
# Written down from Intuit's IIF Import Kit, "QB 2007 & Enterprise 7.0 and later":
# the union of the !TRNS and !SPL columns of its Invoice, Credit Memo, Receive
# Payment and Deposit examples.
KIT_TRNS = {"TRNSID", "TRNSTYPE", "DATE", "ACCNT", "NAME", "CLASS", "AMOUNT", "DOCNUM", "MEMO", "CLEAR", "TOPRINT", "NAMEISTAXABLE",
            "ADDR1", "ADDR2", "ADDR3", "ADDR4", "ADDR5", "DUEDATE", "TERMS", "PAID", "SHIPVIA", "SHIPDATE", "OTHER1", "REP", "FOB",
            "PONUM", "INVTITLE", "INVMEMO", "SADDR1", "SADDR2", "SADDR3"}
KIT_SPL = {"SPLID", "TRNSTYPE", "DATE", "ACCNT", "NAME", "CLASS", "AMOUNT", "DOCNUM", "MEMO", "CLEAR", "QNTY", "PRICE", "INVITEM",
           "PAYMETH", "TAXABLE", "VALADJ", "REIMBEXP", "SERVICEDATE", "OTHER2", "OTHER3", "EXTRA"}
KIT_CUST = {"NAME", "REFNUM", "TIMESTAMP", "BADDR1", "BADDR2", "BADDR3", "BADDR4", "BADDR5", "SADDR1", "SADDR2", "SADDR3", "SADDR4",
            "SADDR5", "PHONE1", "PHONE2", "FAXNUM", "EMAIL", "NOTE", "CONT1", "CONT2", "CTYPE", "TERMS", "TAXABLE", "SALESTAXCODE",
            "LIMIT", "RESALENUM", "REP", "TAXITEM", "NOTEPAD", "SALUTATION", "COMPANYNAME", "FIRSTNAME", "MIDINIT", "LASTNAME"}


def kit_headers(kit):
    trns, spl, cust = set(), set(), set()
    for root, _, files in os.walk(kit):
        for f in files:
            if not f.lower().endswith(".iif"):
                continue
            for line in open(os.path.join(root, f), "rb").read().decode("latin-1").split("\r\n"):
                cells = [c for c in line.split("\t") if c]
                if cells and cells[0] == "!TRNS":
                    trns |= set(cells[1:])
                elif cells and cells[0] == "!SPL":
                    spl |= set(cells[1:])
                elif cells and cells[0] == "!CUST":
                    cust |= set(cells[1:])
    return trns, spl, cust


def iif(d, kit):
    manifest_ok(d, "iif")
    raw = open(os.path.join(d, "qbd.iif"), "rb").read()
    check("iif: ASCII only (Intuit)", all(b < 128 for b in raw))
    check("iif: CRLF line ends", raw.endswith(b"\r\n") and b"\n" not in raw.replace(b"\r\n", b""))
    trns_cols, spl_cols, cust_cols = KIT_TRNS, KIT_SPL, KIT_CUST
    if kit:
        trns_cols, spl_cols, cust_cols = kit_headers(kit)
        check("iif: the kit's own examples were read", len(trns_cols) > 5 and len(spl_cols) > 5, kit)
    lines = [l.split("\t") for l in raw.decode("ascii").split("\r\n") if l]
    heads = {}
    block = None
    blocks = []
    for cells in lines:
        tag = cells[0]
        if tag.startswith("!"):
            heads[tag] = cells[1:]
            if tag == "!TRNS":
                check("iif: !TRNS columns are the kit's", set(cells[1:]) <= trns_cols, set(cells[1:]) - trns_cols)
            if tag == "!SPL":
                check("iif: !SPL columns are the kit's", set(cells[1:]) <= spl_cols, set(cells[1:]) - spl_cols)
            if tag == "!CUST":
                check("iif: !CUST columns are the kit's", set(cells[1:]) <= cust_cols, set(cells[1:]) - cust_cols)
            continue
        if tag == "TRNS":
            block = [dict(zip(heads["!TRNS"], cells[1:]))]
        elif tag == "SPL":
            block.append(dict(zip(heads["!SPL"], cells[1:])))
        elif tag == "ENDTRNS":
            blocks.append(block)
            block = None
    types = [b[0]["TRNSTYPE"] for b in blocks]
    check("iif: an invoice, a credit memo, two payments and a deposit", types == ["INVOICE", "CREDIT MEMO", "PAYMENT", "PAYMENT", "DEPOSIT"], types)
    for b in blocks:
        s = sum(D(x["AMOUNT"]) for x in b)
        check("iif: %s balances to zero" % b[0]["TRNSTYPE"], s == 0, str(s))
        check("iif: M/D/YYYY dates", all(re.fullmatch(r"\d{1,2}/\d{1,2}/\d{4}", x["DATE"]) for x in b))
    inv, cm, pay, _, dep = blocks
    check("iif: an invoice debits receivables (kit: AR positive)", inv[0]["ACCNT"] == "Accounts Receivable" and D(inv[0]["AMOUNT"]) == D("237.00"))
    check("iif: its splits are income, negative, with negative quantity (kit)", all(D(x["AMOUNT"]) < 0 and D(x["QNTY"]) < 0 for x in inv[1:]))
    check("iif: a credit memo reverses it (kit)", D(cm[0]["AMOUNT"]) < 0 and all(D(x["AMOUNT"]) > 0 for x in cm[1:]))
    check("iif: a payment goes to Undeposited Funds (kit)", pay[0]["ACCNT"] == "Undeposited Funds" and pay[1]["ACCNT"] == "Accounts Receivable")
    check("iif: a deposit puts the net in the bank (kit)", dep[0]["ACCNT"] == "Checking" and D(dep[0]["AMOUNT"]) == D("181.92"))
    check("iif: dates as the kit writes them, M/D/YYYY", (inv[0]["DATE"], inv[0]["DUEDATE"], dep[0]["DATE"]) == ("2/1/2027", "2/15/2027", "2/10/2027"),
          (inv[0]["DATE"], inv[0]["DUEDATE"]))
    fees = [D(x["AMOUNT"]) for x in dep[1:] if x["ACCNT"] == "Merchant Fees"]
    check("iif: the fees withheld are their own lines", sorted(fees) == [D("2.88"), D("3.20")], fees)


def main():
    d = sys.argv[1]
    kit = sys.argv[2] if len(sys.argv) > 2 else None
    general(os.path.join(d, "general"))
    qbo(os.path.join(d, "qbo_us"), "qbo_us", "us", {"WS-0001": "237.00"}, r"\d\d/\d\d/\d{4}")
    raw, rows = rows_of(os.path.join(d, "qbo_us", "qbo_invoices_001.csv"))
    setup = [r for r in rows[1:] if r[7] == "Onboarding, set-up"][0]
    check("qbo: a discounted line has no rate (folded, and the manifest says so)", setup[9] == "" and setup[10] == "90.00", setup)
    qbo(os.path.join(d, "qbo_uk"), "qbo_uk", "uk", {"WS-0001": "237.00", "WS-0002": "1.00"}, r"\d\d/\d\d/\d{4}")
    big = qbo(os.path.join(d, "qbo_big"), "qbo_big", "us", {}, r"\d\d/\d\d/\d{4}")
    check("qbo: 150 invoices split at 100 (Intuit)", len(set(big.values())) == 2 and len(big) == 150)
    tall = qbo(os.path.join(d, "qbo_tall"), "qbo_tall", "us", {}, r"\d\d/\d\d/\d{4}")
    check("qbo: two 600-line invoices split at 1,000 rows", len(set(tall.values())) == 2)
    xero(os.path.join(d, "xero_inv"), "xero_inv", 1, {"WS-0001": "237.00", "WS-0002": "1.07", "WS-0004": "59.50"})
    # the date VALUES, not only their shape: WS-0001 is issued 2027-02-01 and
    # due 2027-02-15, and only a day past 12 tells DD/MM from MM/DD
    _, xr = rows_of(os.path.join(d, "xero_inv", "xero_sales_invoices_001.csv"))
    w = [dict(zip(xr[0], r)) for r in xr[1:] if r[10] == "WS-0001"][0]
    check("xero: WS-0001's dates as DD/MM/YYYY", (w["*InvoiceDate"], w["*DueDate"]) == ("01/02/2027", "15/02/2027"), w["*DueDate"])
    _, qr = rows_of(os.path.join(d, "qbo_us", "qbo_invoices_001.csv"))
    q = [dict(zip(qr[0], r)) for r in qr[1:] if r[0] == "WS-0001"][0]
    check("qbo US: WS-0001's dates as MM/DD/YYYY", (q["InvoiceDate"], q["DueDate"], q["Service Date"]) == ("02/01/2027", "02/15/2027", "01/01/2027"), q)
    _, ur = rows_of(os.path.join(d, "qbo_uk", "qbo_invoices_001.csv"))
    u = [dict(zip(ur[0], r)) for r in ur[1:] if r[0] == "WS-0001"][0]
    check("qbo UK: WS-0001's dates as DD/MM/YYYY", (u["InvoiceDate"], u["DueDate"]) == ("01/02/2027", "15/02/2027"), u)
    xero(os.path.join(d, "xero_cn"), "xero_cn", -1, {"WS-CN-0001": "49.00"})
    xb = xero(os.path.join(d, "xero_big"), "xero_big", 1, {})
    check("xero: 700 rows split, never inside an invoice", len(set(xb.values())) == 2 and len(xb) == 100)
    for acct in ["clearing", "bank"]:
        bank(os.path.join(d, "qbo_bank_" + acct), "qbo_bank_" + acct, "qbo", acct)
        bank(os.path.join(d, "xero_bank_" + acct), "xero_bank_" + acct, "xero", acct)
    iif(os.path.join(d, "iif"), kit)
    print("checks: %d\nmismatches: %d" % (checks, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
