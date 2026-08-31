' Double-entry accounting (docs/accounting_design.md).
'
' SELF-CHECKING, not a golden. Every failure mode this library exists to
' prevent leaves a BALANCED ledger and a plausible statement -- right amounts
' on the wrong side, a doubled close, a phantom account -- so a golden would
' record the damaged figures as expected and defend them. Every line states its
' own expected answer.

load accounting

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

books = accounting.chart([
  { code: "1000", name: "Cash", kind: "asset" },
  { code: "1100", name: "Receivables", kind: "asset" },
  { code: "2000", name: "Payables", kind: "liability" },
  { code: "3000", name: "Retained earnings", kind: "equity" },
  { code: "4000", name: "Sales", kind: "revenue" },
  { code: "5000", name: "Rent", kind: "expense" }
])

' --------------------------------------------------- normal balance sides
' One per kind, so a side error cannot hide in an untested quadrant.
check("asset is debit-normal", books["1000"].side, "debit")
check("liability is credit-normal", books["2000"].side, "credit")
check("equity is credit-normal", books["3000"].side, "credit")
check("revenue is credit-normal", books["4000"].side, "credit")
check("expense is debit-normal", books["5000"].side, "debit")

' ------------------------------------------------------------ a month
d1 {date}= "2026-01-05"
d2 {date}= "2026-01-12"
d3 {date}= "2026-01-20"
eom {date}= "2026-01-31"
som {date}= "2026-01-01"

cash_sale {USD}= "1000.00"
credit_sale {USD}= "400.00"
rent {USD}= "300.00"

lg = accounting.ledger()
lg = accounting.post(lg, accounting.entry(books, d1, "Cash sale",
       [{ account: "1000", debit: cash_sale }, { account: "4000", credit: cash_sale }]))
lg = accounting.post(lg, accounting.entry(books, d2, "Sale on account",
       [{ account: "1100", debit: credit_sale }, { account: "4000", credit: credit_sale }]))
lg = accounting.post(lg, accounting.entry(books, d3, "Rent, unpaid",
       [{ account: "5000", debit: rent }, { account: "2000", credit: rent }]))

check("three entries posted", count(lg.entries), 3)

' Computed by hand: cash 1000, receivables 400, payables 300, sales 1400,
' rent 300.
bal = accounting.balances(lg, nothing)
check("cash balance", bal["1000"], "1000.00")
check("receivables balance", bal["1100"], "400.00")
check("payables balance (credit, so negative)", bal["2000"], "-300.00")
check("sales balance (credit, so negative)", bal["4000"], "-1400.00")

tb = accounting.trial_balance(lg)
check("trial balance balances", tb.balanced, true)
check("total debits", tb.debits, "1700.00")
check("total credits", tb.credits, "1700.00")

' THE INVARIANT, asserted arithmetically rather than as a transcript.
bs = accounting.balance_sheet(books, lg, nothing)
check("assets", bs.assets, "1400.00")
check("liabilities", bs.liabilities, "300.00")
check("earnings not yet closed", bs.earnings, "1100.00")
check("the accounting equation holds", bs.balanced, true)

inc = accounting.income_statement(books, lg, som, eom)
check("revenue for the month", inc.revenue, "1400.00")
check("expenses for the month", inc.expenses, "300.00")
check("net income", inc.net, "1100.00")

' A window that excludes the rent must exclude its expense -- the check that
' catches a date filter that is not actually filtering.
before_rent {date}= "2026-01-15"
partial = accounting.income_statement(books, lg, som, before_rent)
check("a narrower window sees less revenue", partial.revenue, "1400.00")
check("and no expenses at all", partial.expenses, "0.00")

' -------------------------------------------------------------- closing
closed = accounting.close(books, lg, eom, "3000")
after = accounting.balances(closed, nothing)
check("closing zeroes revenue", after["4000"], "0.00")
check("closing zeroes expenses", after["5000"], "0.00")
check("closing leaves cash alone", after["1000"], "1000.00")

bs2 = accounting.balance_sheet(books, closed, nothing)
check("net income landed in equity", bs2.equity, "1100.00")
check("nothing left unclosed", bs2.earnings, "0.00")
check("and it still balances", bs2.balanced, true)
check("assets are unchanged by closing", bs2.assets, bs.assets)

' ------------------------------------------------------------ refusals
on error goto next

x = accounting.entry(books, d1, "Unbalanced",
      [{ account: "1000", debit: cash_sale }, { account: "4000", credit: rent }])
check("an unbalanced entry is refused", error.message,
      "accounting.entry does not balance in USD: debits minus credits is 700.00")
error.clear()

x = accounting.entry(books, d1, "Ghost",
      [{ account: "9999", debit: rent }, { account: "4000", credit: rent }])
check("an account not in the chart is refused", error.message,
      "accounting.entry: line 1 names account 9999, which is not in the chart")
error.clear()

x = accounting.entry(books, d1, "Both sides",
      [{ account: "1000", debit: rent, credit: rent }, { account: "4000", credit: rent }])
check("a line with both sides is refused", error.message,
      "accounting.entry: line 1 has both a debit and a credit")
error.clear()

x = accounting.entry(books, d1, "Neither",
      [{ account: "1000" }, { account: "4000", credit: rent }])
check("a line with neither side is refused", error.message,
      "accounting.entry: line 1 has neither a debit nor a credit")
error.clear()

x = accounting.close(books, closed, eom, "3000")
check("closing an already-closed period is refused", error.message,
      "accounting.close: the period through 2026-01-31 is already closed")
error.clear()

x = accounting.post(closed, accounting.entry(books, d1, "Backdated",
      [{ account: "1000", debit: rent }, { account: "4000", credit: rent }]))
check("posting into a closed period is refused", error.message,
      "accounting.post: the period through 2026-01-31 is closed; an entry dated 2026-01-05 cannot be added")
error.clear()

x = accounting.close(books, lg, eom, "1000")
check("closing into a non-equity account is refused", error.message,
      "accounting.close: 1000 is of kind asset, not equity")
error.clear()

' THE CONTROL. Without it the refusals above are satisfied by refusing
' everything: each of these is the nearest legal neighbour of one above.
ok_entry = accounting.entry(books, d1, "Fine",
      [{ account: "1000", debit: rent }, { account: "4000", credit: rent }])
check("a balanced entry is accepted", count(ok_entry.lines), 2)
feb {date}= "2026-02-10"
ok_post = accounting.post(closed, accounting.entry(books, feb, "After the close",
      [{ account: "1000", debit: rent }, { account: "4000", credit: rent }]))
check("a later entry still posts after a close", count(ok_post.entries), count(closed.entries) + 1)

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
