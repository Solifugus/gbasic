# Accounting exports for an outside accountant — design

Status: **first implementation built** (`stdlib/acctexport.bas`, suite
`tests/run_acctexport.sh`). Asked for 2026-11-10, for Worksplicer Inc.'s sales
backend: software subscriptions, invoices, payments, credits, refunds, payment
processor fees and bank payouts, handed to an outside accountant. Built as a
general library any gBASIC application can use.

## 1. What this is, and what it is not

A business application holds its own records of what it sold and what it was
paid. Its accountant works in an accounting product. This library turns the
application's records into **files the accountant can read or import**, with a
manifest that says exactly what went into each file.

It **generates files**. It does not import them, cannot see whether an import
succeeded, and does not talk to any vendor's API. Stable source IDs and the
manifest help an application avoid sending a record twice, but **nothing here
can prevent the receiving product from creating a duplicate**: Intuit's IIF
manual says outright that "there is no preventative mechanism to stop the same
IIF from being imported multiple times", and Xero skips an invoice number it
already has but creates a new contact for a name it does not recognise.

It does **not** decide accounting: no account codes, tax treatments, currency
conversions or journal entries are invented. Where a target needs an account,
a tax code or an item name, the caller supplies a mapping, and a missing one is
refused by name. Database access, scheduling, email and customer-specific rules
stay in the application.

## 2. Why a companion library and not part of FinIO

`finio` (docs/financial_adapters_design.md) reads financial message formats —
NACHA, camt.053, BAI2, OFX, pain.001 — from a **retained source**, with
provenance back to the bytes, and re-emits a document **in the format it was
read as**. Its §16 says plainly that originating or translating a document is
not built and should wait for a real requirement, and its §18 keeps business
policy out.

An accounting export is a different kind of thing:

- there is **no source document**: the input is the application's own records,
  so FinIO's central value (provenance into a retained source) has nothing to
  attach to;
- the targets are **import templates of accounting products**, not interbank
  message standards, and each is shaped by a product's user interface (column
  mapping screens, date-format pickers, draft status) rather than a schema;
- the model is **receivables and cash** (invoices, payments, refunds, payouts),
  which no FinIO adapter carries.

So it is a companion library, `acctexport`, that **reuses FinIO's discipline**
rather than its code paths:

- §16's three classifications: every export is classified **representable**,
  **lossy** or **impossible** before a byte is written; a lossy export is refused
  unless the caller names each kind of loss it accepts, and an impossible one has
  no override (`finio_pain001.classify_write` is the model);
- §9 and §12's provenance for rules: every profile carries a registry entry
  (`specification_sources` with the date each source was retrieved, known
  variants, what is missing), checked by `finio.check_registry_entry`, so the
  claims below are data a program can read;
- Axiom 6 and 8: never guess, and never lose silently.

## 3. Vendor research (retrieved 2026-11-10)

### QuickBooks Online (QBO) — file import, not the API

- **What imports from a file** (Intuit, "Common questions about importing data to
  QuickBooks Online", global edition): bank data, customers, suppliers, chart of
  accounts, products and services, invoices, credit notes, sales receipts,
  journal entries, bills. Regional editions list fewer. **Customer payments,
  refunds and payouts are not file imports**; they need the QBO API (out of
  scope) or manual entry.
- **Invoices** (Intuit, "Import multiple invoices at once", UK edition; Intuit
  PDF "Make sure your invoices import correctly"): required columns Invoice
  Number, Customer, Invoice Date, Due Date, Item Amount (and Item Tax Code
  outside the US); at most **1,000 rows** per file and **100 invoices** per
  import; **no negative charges** ("such as discounts or credit notes");
  customers must match display names unless "Add new customers" is ticked;
  products must already exist; custom invoice numbers need "Custom transaction
  numbers" turned on; the date format is chosen at upload. The **US** edition
  says invoices cannot be imported this way when sales tax is set up. Column
  headings are mapped at upload; the names used here (`InvoiceNo`, `Customer`,
  `InvoiceDate`, `DueDate`, `Terms`, `Memo`, `Item(Product/Service)`,
  `ItemDescription`, `ItemQuantity`, `ItemRate`, `ItemAmount`, `Service Date`,
  `ItemTaxCode`) follow Intuit's sample file as quoted in the Intuit community
  (2020); the sample itself sits behind a login and was not retrieved.
- **Bank data** (Intuit, "Prepare your CSV file for bank upload to QuickBooks",
  UK edition): a **3-column** layout (Date, Description, Amount, money in
  positive and money out negative) or a 4-column one (Date, Description,
  Credit, Debit); no numbers in Description; a consistent date format.

### Xero — file import, not the API

- **Sales invoices** (Xero Central, "Import customer invoices"): the only
  required columns are ContactName and InvoiceNumber; the template's headings
  must not be changed; **no more than 500 items** per file is recommended;
  invoices import as **drafts**; an invoice number Xero already has is **not
  imported**; dates are **DD/MM/YYYY**; AccountCode, TaxType, Tracking,
  Currency and BrandingTheme must match Xero exactly; Discount is a
  **percentage** to two places; TaxAmount is an added column (amounts then
  tax-exclusive); a file cannot mix tax-inclusive and exclusive prices; a
  currency must be enabled first.
- **Credit notes** (Xero Central, "Import customer credit notes", UK): the same
  template, imported from Sales overview, with **UnitAmount as a negative
  amount**; also drafts.
- **Bank statements** (Xero Central, "Import a bank statement in CSV format",
  US): Date and Amount required, Payee, Description, Reference optional; income
  positive and expenses negative in **one Amount column**; up to 100,000 rows;
  DD/MM/YYYY among the accepted date formats; headings are regional and mapped
  at import.
- **Payments, refunds and payouts** have no CSV import in these articles.
- The exact **column order** of Xero's template is in a file behind a login; the
  order used here (`*ContactName`, `EmailAddress`, `POAddressLine1`-`4`,
  `POCity`, `PORegion`, `POPostalCode`, `POCountry`, `*InvoiceNumber`,
  `Reference`, `*InvoiceDate`, `*DueDate`, `Total`, `InventoryItemCode`,
  `*Description`, `*Quantity`, `*UnitAmount`, `Discount`, `*AccountCode`,
  `*TaxType`, `TaxAmount`, `TrackingName1`, `TrackingOption1`, `TrackingName2`,
  `TrackingOption2`, `Currency`, `BrandingTheme`) is the one third-party guides
  agree on; every column name is in Xero's own articles. Recorded as a gap.

### QuickBooks Desktop (QBD) — IIF

- **Intuit's IIF Import Kit** (`iif_import_kit.zip`, from the Intuit article "IIF
  Overview: import kit, sample files, and headers", updated 8/4/2026; manual
  dated 9/30/2008): IIF is **ASCII, tab-separated**, CRLF; a transaction is a
  `TRNS` line, one or more `SPL` lines and `ENDTRNS`, under `!TRNS`, `!SPL`,
  `!ENDTRNS` header lines; lists (`!CUST`, `!ACCNT`) must exist or come first.
  Example files (QuickBooks 2007 and later) show `INVOICE` (Accounts Receivable
  positive, income negative, quantity negative), `CREDIT MEMO` (the reverse),
  `PAYMENT` (Undeposited Funds positive, Accounts Receivable negative) and
  `DEPOSIT` (the bank account positive, the source accounts negative). Dates
  are M/D/YYYY.
- **Limits from the manual:** no duplicate prevention; **no link** between an
  invoice and its payment; taxed invoices need sales-tax items and `VTYPE`
  vendors and are "best" entered in the user interface; Excel/CSV import in
  Desktop is **lists only**; error checking is limited. No refund example exists
  in the kit.
- The kit is Intuit's and its redistribution terms are not stated, so it is
  **not** copied into this repository; the tests hold the export to the header
  columns and sign conventions written down from it, with the file names cited.

## 4. The model

One **bundle** of plain records, every money amount a gBASIC `money` value (exact,
in its own currency), every date a `date`:

| record | fields |
|---|---|
| `organization` | `name`, `rounding` (`"half_up"` or `"half_even"`: how the application rounded line amounts) |
| customer | `id`, `name`, `email`, `address` `{ line1, line2, city, region, postal_code, country }`, `external` |
| invoice | `id`, `number`, `customer`, `issue_date`, `due_date`, `currency`, `lines`, `total`, `memo`, `external` |
| line | `id`, `description`, `quantity`, `unit_price`, `discount`, `amount` (after discount, before tax), `tax`, `tax_code`, `category`, `item`, `service_from`, `service_to` |
| credit note | as an invoice, plus `applies_to` (an invoice id); amounts positive |
| payment | `id`, `customer`, `date`, `currency`, `amount` (gross received), `method`, `reference`, `processor`, `allocations` `[{ invoice, amount }]` |
| refund | `id`, `customer`, `date`, `currency`, `amount`, `payment` or `credit_note`, `reference`, `processor` |
| fee | `id`, `date`, `currency`, `amount`, `processor`, `payment`, `description` |
| payout | `id`, `date`, `currency`, `amount` (net to the bank), `processor`, `reference`, `payments`, `refunds`, `fees` |

**Revenue is invoices.** A payment settles a receivable, a refund returns
money, a fee is an expense the processor withheld, and a payout is a transfer of
what was left to the bank. The general export keeps all four distinct and
labels them, and validation refuses the shapes that double count: a payment in
two payouts, a payout whose net is not its payments less refunds and fees, an
allocation larger than what is left of an invoice.

## 5. Profiles

| profile | carries | refuses or reports |
|---|---|---|
| `general` | everything: one CSV per record type and per link (allocations, payout items) | nothing is lost |
| `qbo.invoices` | invoices | discounts (no negative lines; folding is a named loss), US sales tax, a currency other than `home_currency`; splits at 100 invoices / 1,000 rows |
| `qbo.bank` | payouts (`account: "bank"`), or payments, refunds, fees and payouts as the movements of a processor clearing account (`account: "clearing"`) | — |
| `xero.invoices` | invoices | a discount that is not a percentage to two places, an amount Xero would recompute differently; needs account and tax mappings; splits at 500 rows |
| `xero.credit_notes` | credit notes | as invoices |
| `xero.bank` | as `qbo.bank` | — |
| `qbd.iif` | customers, untaxed invoices and credit memos, payments, payouts as deposits | taxed lines, refunds, fees outside a payout, non-ASCII text, a second currency; the invoice-payment link is a named loss |

A record type the chosen profile does not carry is **unsupported**: the export
refuses unless the caller lists that type in `skip`, and the manifest names what
was skipped. Nothing is left out silently.

## 6. Batches and the manifest

Every export answers its files and a manifest: batch id (the caller's, or one
derived from the content), profile and version, the date format used, each
file's name, rows, bytes and SHA-256, every source record with the file and key
it went to, counts and totals by currency and record type, what was skipped and
why, and every accepted loss. Output is deterministic: the same bundle and
options give the same bytes and the same batch id, so a batch can be reproduced
and compared. The manifest's own statement is that the files were **generated**;
an application records whether its accountant imported them.

## 7. Gaps

- QBO and Xero **payments**, refunds and payouts as transactions need the vendors'
  APIs; out of scope. The bank and clearing statements are the file route.
- The QBO sample file and the Xero template file sit behind logins; the column
  names used are documented above with their sources.
- QBO **credit note** import is listed only in some regional editions, with no
  column list retrieved, so it is not offered.
- QBD **refunds** have no example in Intuit's kit and are not offered.
- **Taxed IIF** transactions need sales-tax vendors and items; not offered.
- Nothing here has been imported into a vendor's product. The tests check the
  files against the documented layouts; an import is the accountant's step.
