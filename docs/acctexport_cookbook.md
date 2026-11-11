# Accounting export cookbook

Handing an outside accountant a month of sales: invoices, payments, credit
notes, refunds, processor fees and payouts. `stdlib/acctexport.bas` turns an
application's own records into files under an explicit **target profile**, with
a manifest per batch. Design, vendor research and sources:
[accounting_export_design.md](accounting_export_design.md).

Every code block on this page is quoted from `examples/acctexport/`, which
`tests/run_acctexport.sh` runs, and the suite checks the quotations still
match.

**These recipes generate files.** Importing them is the accountant's step, in
the accountant's product, and nothing here can see whether it worked or stop the
product from creating duplicates. Record the outcome against the batch id.

## What you supply

| | |
|---|---|
| records | customers, invoices and credit notes with their lines, payments with their allocations to invoices, refunds, fees and payouts, as plain records; every amount a `money` value, every date a `date` |
| `organization.rounding` | how your application rounded line amounts (`"half_up"` or `"half_even"`), so each line can be checked rather than re-rounded |
| mappings | for a target that needs them: account codes, tax types or codes, income accounts, item names, as the accountant names them in their product |
| `skip` | the record types you are deliberately leaving out of a profile that cannot carry them |
| `allow_lossy` | each kind of loss you accept, by name |

The library supplies none of these. It never chooses an account, a tax
treatment, a currency conversion or a journal entry.

## 1. The month, as a bundle

One invoice for three seats, the customer's card payment through Stripe, the
fee Stripe withheld, and the payout of the rest. A line's `amount` is after any
discount and before tax; a document's `total` is its amounts plus tax.

```basic
b = acctexport.bundle({
    organization: { name: "Worksplicer Inc.", rounding: "half_up" },
    customers: [ { id: "cus_1001", name: "Łódź Print House", email: "ap@lodzprint.example",
                   address: { line1: "ul. Piotrkowska 1", city: "Łódź", postal_code: "90-001", country: "PL" } } ],
    invoices: [ { id: "inv_2027_0001", number: "WS-2027-0001", customer: "cus_1001",
                  issue_date: feb1, due_date: feb1, currency: "USD", total: amount,
                  memo: "Team plan, January 2027",
                  lines: [ { id: "li_1", description: "Team plan, per seat\nJanuary 2027", quantity: seats,
                             unit_price: price, amount: amount, category: "subscriptions", item: "TEAM-SEAT",
                             service_from: jan1, service_to: jan31 } ],
                  external: { billing_system: "sub_77" } } ],
    payments: [ { id: "pay_9001", customer: "cus_1001", date: feb3, currency: "USD", amount: amount,
                  method: "card", reference: "ch_3Q", processor: "Stripe",
                  allocations: [ { invoice: "inv_2027_0001", amount: amount } ] } ],
    fees: [ { id: "fee_9001", date: feb3, currency: "USD", amount: fee_amt, processor: "Stripe",
              payment: "pay_9001", description: "card processing fee" } ],
    payouts: [ { id: "po_501", date: feb5, currency: "USD", amount: net, processor: "Stripe",
                 reference: "po_1Q", payments: ["pay_9001"], fees: ["fee_9001"] } ] })
```

## 2. Validate, then the general bundle

`validate` lists everything wrong (totals, rounding, currencies, allocations
larger than what is left of an invoice, a payment in two payouts, a payout that
is not its payments less refunds and fees) and corrects nothing. `render` under
`general` writes every record type as its own CSV, with the links between them,
a README for the accountant, and nothing lost.

```basic
v = acctexport.validate(b)
print "valid: " + string(v.ok)

' 1. The general bundle: every record, nothing lost.
r = acctexport.render(b, "general", { batch_id: "worksplicer-2027-02" })
print "general: batch " + r.batch_id + ", " + string(count(r.files)) + " files"
for each f in r.manifest.files
    print "  " + f.name + "  rows " + string(f.rows) + "  sha256 " + left(f.sha256, 12)
next
for each t in r.manifest.totals
    print "  total " + t.type + " " + t.currency + " " + t.amount
next
```

Revenue is the invoices. Payments settle them, fees are an expense the
processor withheld, and the payout is a transfer of what was left: the general
bundle keeps gross receipts (147.00), fees (4.56) and the net payout (142.44) as
three separate amounts, so nothing is counted as revenue twice.

## 3. Xero, which needs the accountant's chart

Xero takes an account code and a tax type on every line, written exactly as the
organisation has them. Without them the export refuses and names what is
missing; with them, the one thing the template cannot carry (a service period)
is accepted by name and recorded in the manifest.

```basic
' 2. Xero needs an account code and a tax type for each line: the
' accountant's chart, not the application's guess. Without them it refuses.
on error goto next
x = acctexport.render(b, "xero.invoices", { skip: ["payments", "fees", "payouts"] })
print "xero without mappings: " + left(error.message, 95) + "..."
error.clear()
on error stop
x = acctexport.render(b, "xero.invoices", {
        skip: ["payments", "fees", "payouts"],
        mappings: { accounts: { subscriptions: "200" }, tax_types: { none: "Tax Exempt (0%)" } },
        allow_lossy: ["service_period"] })
print "xero: " + x.files[0].name + ", accepted losses: " + string(count(x.manifest.accepted_losses))
```

## 4. The payout, as the bank sees it

Payments, refunds and payouts are not file imports in QuickBooks Online or Xero.
What both do import is a **bank statement**: the payout as one deposit of what
arrived, for the accountant to reconcile against the invoices.

```basic
' 3. The payout as Xero sees the bank: one deposit of what arrived.
s = acctexport.render(b, "xero.bank", { account: "bank", currency: "USD", skip: ["invoices"] })
print "bank statement:"
print s.files[0].text
```

## 5. Saving a batch

`save` writes each file and `manifest.json` into a folder, each file whole or
not at all, and refuses to replace a batch already there unless asked.

```basic
if count(args) > 0 then
    paths = acctexport.save(r, args[0])
    print "wrote " + string(count(paths)) + " files to " + args[0]
end if
```

The manifest holds the batch id, the profile and its version, the date format,
each file's rows, bytes and SHA-256, every record with the file and key it went
to, counts and totals by type and currency, what was skipped and every accepted
loss. The same bundle and options give the same bytes and the same batch id, so
a batch can be reproduced and compared.

## 6. Ask before exporting

`plan` answers what a profile would do without writing anything: representable,
lossy or impossible, the types it does not carry, the mappings it still needs.

```basic
' What a profile would do, before it does it.
p = acctexport.plan(b, "qbo.invoices", { region: "us", home_currency: "USD" })
print "qbo.invoices: " + p.classification
for each u in p.unsupported
    print "  not carried: " + u.type + " " + join(u.ids, ", ")
next
```

## 7. QuickBooks Online invoices

At most 100 invoices and 1,000 rows go in one file (more are split, never
inside an invoice); a discount cannot be a negative line, the US edition cannot
take invoices with sales tax, and everything must be in the home currency.

```basic
' QuickBooks Online takes the invoices; payments, fees and payouts are not
' file imports there, so they are skipped by name and the manifest says so.
q = acctexport.render(b, "qbo.invoices", { region: "us", home_currency: "USD",
                                           skip: ["payments", "fees", "payouts"] })
print q.files[0].text
print "skipped: " + q.manifest.skipped[0].type
```

## 8. A processor clearing account

If the accountant keeps the processor's balance as its own account, the
statement is its movements: gross payments in, refunds, fees and payouts out.
When everything has been paid out it sums to zero.

```basic
' The processor's balance as a clearing account: gross in, fee and payout out.
c = acctexport.render(b, "qbo.bank", { account: "clearing", currency: "USD", skip: ["invoices"] })
print c.files[0].text
```

## 9. QuickBooks Desktop (IIF)

IIF is ASCII and tab-separated, and every account is the accountant's. It cannot
link a payment to the invoices it pays, so that loss is accepted by name; refunds
and taxed lines are not written at all.

```basic
' QuickBooks Desktop: every account named by the accountant; the
' payment-to-invoice link IIF cannot carry is accepted by name.
i = acctexport.render(b, "qbd.iif", {
        home_currency: "USD",
        mappings: { accounts: { receivable: "Accounts Receivable", undeposited_funds: "Undeposited Funds",
                                bank: "Checking", processor_fees: "Merchant Fees" },
                    income_accounts: { subscriptions: "Subscription Income" } },
        allow_lossy: ["payment_link"] })
print "iif: " + string(i.manifest.files[0].rows) + " records, losses accepted: " + i.manifest.accepted_losses[0].kind

' Without the accounts, IIF refuses rather than inventing them.
on error goto next
x = acctexport.render(b, "qbd.iif", { home_currency: "USD" })
print "refused: " + left(error.message, 80) + "..."
error.clear()
```

## What is not here

- Importing anything, or calling QuickBooks or Xero APIs.
- QuickBooks Online credit notes (listed in some regions only, with no column
  list found), QuickBooks Desktop refunds and taxed transactions.
- Currency conversion, account assignment, tax treatment, journal entries.
- Reading the vendors' files back.
