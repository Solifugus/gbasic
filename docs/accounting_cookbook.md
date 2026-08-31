# Accounting cookbook

Worked recipes for `stdlib/accounting.bas` — double-entry bookkeeping over
exact `money`.

**This page cannot lie.** Every code block below is a real file under
`examples/accounting_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_accounting_cookbook.sh`.

Design and rationale: [accounting_design.md](accounting_design.md). The money
type underneath: [money_cookbook.md](money_cookbook.md).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/accounting_cookbook/01_first_entries.bas
```

---


## 1. A chart of accounts and your first entries

Two rules do most of the work.

Every account has a **kind** — `asset`, `liability`, `equity`, `revenue`,
`expense` — and the kind fixes whether it normally carries a debit or a credit
balance. You write that down once, in the chart, and never again.

Every entry must **balance**. Debits equal credits, or the entry is refused
where you wrote it — not discovered later by a report that cannot tell you
which entry did it.

A line carries `debit` **or** `credit`, never a signed amount. *Is a negative
debit a credit?* is a question that produces statements which balance and are
backwards.

`accounting.post` **returns** the ledger. Inside a function `append` mutates a
local copy, so a mutate-in-place API would silently do nothing to your value.

<!--CODE:01_first_entries-->

```basic
' Recipe 1 — A chart of accounts and your first entries.
'
' Two rules do most of the work. Every account has a KIND, which fixes whether
' it normally carries a debit or a credit balance. And every entry must
' BALANCE — debits equal credits — or it is refused where you wrote it, not
' discovered later by a report that cannot tell you which entry did it.
program main()
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash",     kind: "asset" },
    { code: "3000", name: "Capital",  kind: "equity" },
    { code: "4000", name: "Fees",     kind: "revenue" },
    { code: "5100", name: "Rent",     kind: "expense" }
  ])

  ' The kind decides the normal side, and you never write it down twice.
  print "cash is a  " + books["1000"].kind + ", normally " + books["1000"].side
  print "fees are a " + books["4000"].kind + ", normally " + books["4000"].side

  ' A line carries `debit` OR `credit` — never a signed amount. "Is a negative
  ' debit a credit" is a question that produces statements which balance and
  ' are backwards.
  day1 {date}= "2026-03-01"
  seed {USD}= "5000.00"
  lg = accounting.ledger()
  lg = accounting.post(lg, accounting.entry(books, day1, "Owner puts money in", [
         { account: "1000", debit:  seed },
         { account: "3000", credit: seed }
       ]))
  print ""
  print "entries posted: " + string(count(lg.entries))

  ' NOTE `post` RETURNS the ledger. Inside a function `append` mutates a local
  ' copy, so a mutate-in-place API would silently do nothing to your value.

  ' An entry that does not balance is refused on the spot.
  on error goto next
  short {USD}= "4000.00"
  bad = accounting.entry(books, day1, "Typo", [
          { account: "1000", debit:  seed },
          { account: "3000", credit: short }
        ])
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:01_first_entries-->

```
cash is a  asset, normally debit
fees are a revenue, normally credit

entries posted: 1

refused: accounting.entry does not balance in USD: debits minus credits is 1000.00
```

---


## 2. A month of a small consultancy, end to end

Nine transactions, then the statements. This is the same worked business the
test suite checks, and **every figure was computed independently before the
code was written** — which is what makes it a check rather than a transcript.

The balance sheet reports `earnings` separately from `equity`: revenue minus
expenses that have not yet been closed into an equity account. The identity
that must hold is `assets = liabilities + equity + earnings`, and `balanced`
states it.

Note that the trial balance balancing proves less than it appears to. Debits
still equal credits when both are on the wrong side — which is why the design
leans on the accounting equation instead.

<!--CODE:02_a_month_of_business-->

```basic
' Recipe 2 — A month of a small consultancy, end to end.
'
' Nine transactions, then the statements. Every figure below was computed
' independently before this file was written, which is why it is a check and
' not a transcript.
program main()
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash",            kind: "asset" },
    { code: "1100", name: "Receivables",     kind: "asset" },
    { code: "1500", name: "Equipment",       kind: "asset" },
    { code: "2000", name: "Payables",        kind: "liability" },
    { code: "3000", name: "Owner capital",   kind: "equity" },
    { code: "4000", name: "Consulting fees", kind: "revenue" },
    { code: "5100", name: "Rent",            kind: "expense" },
    { code: "5200", name: "Supplies",        kind: "expense" },
    { code: "5300", name: "Wages",           kind: "expense" },
    { code: "5400", name: "Depreciation",    kind: "expense" }
  ])

  lg = accounting.ledger()
  lg = simple(lg, books, "01", "Owner capital",      "1000", "3000", "20000.00")
  lg = simple(lg, books, "03", "Buy equipment",      "1500", "1000", "6000.00")
  lg = simple(lg, books, "05", "Invoice a client",   "1100", "4000", "12000.00")
  lg = simple(lg, books, "10", "Pay the rent",       "5100", "1000", "2000.00")
  lg = simple(lg, books, "15", "Client pays part",   "1000", "1100", "8000.00")
  lg = simple(lg, books, "18", "Supplies on credit", "5200", "2000", "1500.00")
  lg = simple(lg, books, "22", "Pay wages",          "5300", "1000", "4500.00")
  lg = simple(lg, books, "25", "Pay the supplier",   "2000", "1000", "1000.00")
  lg = simple(lg, books, "31", "Depreciation",       "5400", "1500", "100.00")

  bal = accounting.balances(lg, nothing)
  print "cash on hand:      " + string(bal["1000"])
  print "still owed to you: " + string(bal["1100"])
  print "equipment, net:    " + string(bal["1500"])

  tb = accounting.trial_balance(lg)
  print ""
  print "trial balance:  debits " + string(tb.debits) + "  credits " + string(tb.credits)
  print "  balanced:     " + string(tb.balanced)

  start {date}= "2026-01-01"
  finish {date}= "2026-01-31"
  inc = accounting.income_statement(books, lg, start, finish)
  print ""
  print "January"
  print "  revenue:    " + string(inc.revenue)
  print "  expenses:   " + string(inc.expenses)
  print "  net income: " + string(inc.net)

  bs = accounting.balance_sheet(books, lg, nothing)
  print ""
  print "  assets:      " + string(bs.assets)
  print "  liabilities: " + string(bs.liabilities)
  print "  equity:      " + string(bs.equity)
  print "  earnings:    " + string(bs.earnings) + "  (not yet closed)"
  print "  balances:    " + string(bs.balanced)
end program

' Most entries are one debit and one credit, so this is worth a helper.
function simple(lg, books, dd, memo, dr, cr, amount)
  when {date}= "2026-01-" + dd
  amt {USD}= amount
  return accounting.post(lg, accounting.entry(books, when, memo,
           [{ account: dr, debit: amt }, { account: cr, credit: amt }]))
end function
```

<!--OUT:02_a_month_of_business-->

```
cash on hand:      14500.00
still owed to you: 4000.00
equipment, net:    5900.00

trial balance:  debits 32500.00  credits 32500.00
  balanced:     true

January
  revenue:    12000.00
  expenses:   8100.00
  net income: 3900.00

  assets:      24400.00
  liabilities: 500.00
  equity:      20000.00
  earnings:    3900.00  (not yet closed)
  balances:    true
```

---


## 3. Closing a period, and why it refuses to happen twice

Closing moves revenue and expenses into equity and seals the period.

It is the operation people actually get wrong, and it is wrong in a way that
leaves the ledger **perfectly balanced**: run it twice and equity simply
doubles. So a second close of a closed period is **refused** rather than made
idempotent, and a backdated entry into a sealed period is refused too.

Sealing a period is not sealing the ledger — the next period carries on
normally, which is the control that keeps the refusal from being over-broad.

<!--CODE:03_closing_the_year-->

```basic
' Recipe 3 — Closing a period, and why it refuses to happen twice.
'
' Closing moves revenue and expenses into equity and seals the period. It is
' the operation people actually get wrong, and it is wrong in a way that leaves
' the ledger perfectly balanced — which is why it is REFUSED on a period
' already closed rather than made idempotent.
program main()
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash",     kind: "asset" },
    { code: "3000", name: "Capital",  kind: "equity" },
    { code: "4000", name: "Fees",     kind: "revenue" },
    { code: "5100", name: "Rent",     kind: "expense" }
  ])

  lg = accounting.ledger()
  lg = simple(lg, books, "02", "Capital",  "1000", "3000", "5000.00")
  lg = simple(lg, books, "09", "A fee",    "1000", "4000", "3000.00")
  lg = simple(lg, books, "20", "The rent", "5100", "1000", "1200.00")

  before = accounting.balance_sheet(books, lg, nothing)
  print "before closing"
  print "  equity:   " + string(before.equity)
  print "  earnings: " + string(before.earnings)

  eom {date}= "2026-04-30"
  closed = accounting.close(books, lg, eom, "3000")
  after = accounting.balance_sheet(books, closed, nothing)
  print ""
  print "after closing"
  print "  equity:   " + string(after.equity) + "   <- the earnings moved here"
  print "  earnings: " + string(after.earnings)
  print "  assets unchanged: " + string(after.assets = before.assets)
  print "  still balances:   " + string(after.balanced)

  ' Run it twice and equity would double, silently and in balance.
  on error goto next
  again = accounting.close(books, closed, eom, "3000")
  if error then
    print ""
    print "closing again: " + error.message
    error.clear()
  end if

  ' A period that is closed is closed: a backdated entry is refused too.
  late = simple(closed, books, "15", "Forgotten invoice", "1000", "4000", "500.00")
  if error then
    print "backdating:    " + error.message
    error.clear()
  end if
  on error stop

  ' But the NEXT period carries on normally — sealing a period is not sealing
  ' the ledger.
  may {date}= "2026-05-03"
  amt {USD}= "750.00"
  onward = accounting.post(closed, accounting.entry(books, may, "May fee",
             [{ account: "1000", debit: amt }, { account: "4000", credit: amt }]))
  print ""
  print "May still posts: " + string(count(onward.entries) - count(closed.entries)) + " new entry"
end program

function simple(lg, books, dd, memo, dr, cr, amount)
  when {date}= "2026-04-" + dd
  amt {USD}= amount
  return accounting.post(lg, accounting.entry(books, when, memo,
           [{ account: dr, debit: amt }, { account: cr, credit: amt }]))
end function
```

<!--OUT:03_closing_the_year-->

```
before closing
  equity:   5000.00
  earnings: 1800.00

after closing
  equity:   6800.00   <- the earnings moved here
  earnings: 0.00
  assets unchanged: true
  still balances:   true

closing again: accounting.close: the period through 2026-04-30 is already closed
backdating:    accounting.post: the period through 2026-04-30 is closed; an entry dated 2026-04-15 cannot be added

May still posts: 1 new entry
```

---


## 4. What is refused, and why each one matters

Every mistake here leaves a ledger that balances and a statement that looks
ordinary. That is the whole reason each is refused at the point the entry is
written.

Mixed currencies deserve a note: an entry must balance in **each** currency
separately, and the money type refuses to add across currencies in the first
place — so the commonest class of accounting bug cannot even be expressed.

**And one gBASIC lesson, learned writing this page.** `on error goto next` is
frame-scoped, and so is *acknowledging* an error: `if error then` inside a
helper function does not claim the error pending in its caller. The first draft
used a `show()` helper, left every error unacknowledged, and the second raise
escaped the frame — which is gBASIC's anti-silence rule working exactly as
designed. Acknowledge in the same frame that armed the handler.

<!--CODE:04_what_gets_refused-->

```basic
' Recipe 4 — What is refused, and why each one matters.
'
' Every mistake below leaves a ledger that BALANCES and a statement that looks
' perfectly ordinary. That is the whole reason each is refused where the entry
' is written, rather than reported by something downstream that cannot say
' which entry did it.
'
' NOTE THE SHAPE OF THE ERROR HANDLING. `on error goto next` is FRAME-SCOPED
' and so is acknowledging one: `if error then` in a helper function does not
' claim the error pending in its caller. Writing this recipe with a `show()`
' helper left every error unacknowledged, and the second raise then escaped the
' frame — which is gBASIC's anti-silence rule working exactly as designed.
' Acknowledge in the same frame that armed the handler.
program main()
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash", kind: "asset" },
    { code: "4000", name: "Fees", kind: "revenue" }
  ])
  when {date}= "2026-05-01"
  amt {USD}= "100.00"
  other {USD}= "60.00"
  eur {EUR}= "100.00"

  on error goto next

  ' Debits and credits that do not agree — reported WITH the difference, so you
  ' can see what is missing instead of hunting for it.
  x = accounting.entry(books, when, "Off by forty",
        [{ account: "1000", debit: amt }, { account: "4000", credit: other }])
  print "unbalanced:"
  print "  " + error.message
  error.clear()

  ' An account not in the chart. Without this a typo creates a phantom account
  ' that appears in no statement anyone reads.
  x = accounting.entry(books, when, "Typo",
        [{ account: "1oo0", debit: amt }, { account: "4000", credit: amt }])
  print "account not in the chart:"
  print "  " + error.message
  error.clear()

  ' A line names exactly one side.
  x = accounting.entry(books, when, "Both",
        [{ account: "1000", debit: amt, credit: amt }, { account: "4000", credit: amt }])
  print "a line with both sides:"
  print "  " + error.message
  error.clear()

  x = accounting.entry(books, when, "Neither",
        [{ account: "1000" }, { account: "4000", credit: amt }])
  print "a line with neither:"
  print "  " + error.message
  error.clear()

  ' Currencies do not mix. An entry must balance in EACH currency separately —
  ' and the money type refuses to add across them in the first place, so the
  ' commonest accounting bug cannot even be expressed.
  x = accounting.entry(books, when, "Mixed",
        [{ account: "1000", debit: amt }, { account: "4000", credit: eur }])
  print "mixed currencies:"
  print "  " + error.message
  error.clear()

  ' Two accounts on one code.
  x = accounting.chart([{ code: "1000", name: "A", kind: "asset" },
                        { code: "1000", name: "B", kind: "asset" }])
  print "duplicate account code:"
  print "  " + error.message
  error.clear()

  on error stop

  ' THE CONTROL: the nearest legal neighbour of every refusal above still
  ' works. A library that refused everything would satisfy the list.
  good = accounting.entry(books, when, "Fine",
           [{ account: "1000", debit: amt }, { account: "4000", credit: amt }])
  print ""
  print "and a correct entry is accepted: " + string(count(good.lines)) + " lines"
end program
```

<!--OUT:04_what_gets_refused-->

```
unbalanced:
  accounting.entry does not balance in USD: debits minus credits is 40.00
account not in the chart:
  accounting.entry: line 1 names account 1oo0, which is not in the chart
a line with both sides:
  accounting.entry: line 1 has both a debit and a credit
a line with neither:
  accounting.entry: line 1 has neither a debit nor a credit
mixed currencies:
  accounting.entry does not balance in USD: debits minus credits is 100.00
duplicate account code:
  accounting.chart: duplicate account code 1000

and a correct entry is accepted: 2 lines
```

---
