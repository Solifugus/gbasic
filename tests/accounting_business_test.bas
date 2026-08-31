' A WORKED BUSINESS -- docs/accounting_design.md §6, and the Phase 2 exit
' criterion: a small company modelled from transactions through statements.
'
' EVERY EXPECTED FIGURE WAS COMPUTED OUTSIDE gBASIC before this file existed.
' That is what separates this from a transcript: a golden records whatever the
' library produced, and every mistake double-entry exists to prevent leaves a
' balanced ledger and a plausible statement. These numbers were arrived at
' independently, so agreeing with them is evidence.

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

' A one-person consultancy's first month.
books = accounting.chart([
  { code: "1000", name: "Cash",              kind: "asset" },
  { code: "1100", name: "Receivables",       kind: "asset" },
  { code: "1500", name: "Equipment",         kind: "asset" },
  { code: "2000", name: "Payables",          kind: "liability" },
  { code: "3000", name: "Owner capital",     kind: "equity" },
  { code: "4000", name: "Consulting fees",   kind: "revenue" },
  { code: "5100", name: "Rent",              kind: "expense" },
  { code: "5200", name: "Supplies",          kind: "expense" },
  { code: "5300", name: "Wages",             kind: "expense" },
  { code: "5400", name: "Depreciation",      kind: "expense" }
])

function usd(text)
    m {USD}= text
    return m
end function

function on_day(d)
    when {date}= "2026-01-" + d
    return when
end function

' `post` RETURNS the ledger: inside a function `append` mutates a local copy,
' so a mutate-in-place API would silently do nothing.
lg = accounting.ledger()

function record_one(lg, books, day, memo, dr, dr_amt, cr, cr_amt)
    e = accounting.entry(books, day, memo,
          [{ account: dr, debit: dr_amt }, { account: cr, credit: cr_amt }])
    return accounting.post(lg, e)
end function

lg = record_one(lg, books, on_day("01"), "Owner capital",      "1000", usd("20000.00"), "3000", usd("20000.00"))
lg = record_one(lg, books, on_day("03"), "Buy equipment",      "1500", usd("6000.00"),  "1000", usd("6000.00"))
lg = record_one(lg, books, on_day("05"), "Invoice a client",   "1100", usd("12000.00"), "4000", usd("12000.00"))
lg = record_one(lg, books, on_day("10"), "Pay the rent",       "5100", usd("2000.00"),  "1000", usd("2000.00"))
lg = record_one(lg, books, on_day("15"), "Client pays part",   "1000", usd("8000.00"),  "1100", usd("8000.00"))
lg = record_one(lg, books, on_day("18"), "Supplies on credit", "5200", usd("1500.00"),  "2000", usd("1500.00"))
lg = record_one(lg, books, on_day("22"), "Pay wages",          "5300", usd("4500.00"),  "1000", usd("4500.00"))
lg = record_one(lg, books, on_day("25"), "Pay the supplier",   "2000", usd("1000.00"),  "1000", usd("1000.00"))
lg = record_one(lg, books, on_day("31"), "Depreciation",       "5400", usd("100.00"),   "1500", usd("100.00"))

check("nine entries posted", count(lg.entries), 9)

' ------------------------------------------------------- account balances
bal = accounting.balances(lg, nothing)
check("cash", bal["1000"], "14500.00")
check("receivables", bal["1100"], "4000.00")
check("equipment, net of depreciation", bal["1500"], "5900.00")
check("payables (credit-normal, so negative)", bal["2000"], "-500.00")

' ------------------------------------------------------- trial balance
tb = accounting.trial_balance(lg)
check("trial balance balances", tb.balanced, true)
check("total debits", tb.debits, "32500.00")
check("total credits", tb.credits, "32500.00")

' ------------------------------------------------------- the statements
bs = accounting.balance_sheet(books, lg, nothing)
check("assets", bs.assets, "24400.00")
check("liabilities", bs.liabilities, "500.00")
check("equity before closing", bs.equity, "20000.00")
check("earnings not yet closed", bs.earnings, "3900.00")
check("THE ACCOUNTING EQUATION HOLDS", bs.balanced, true)

jan1 = on_day("01")
jan31 = on_day("31")
inc = accounting.income_statement(books, lg, jan1, jan31)
check("revenue", inc.revenue, "12000.00")
check("expenses", inc.expenses, "8100.00")
check("net income", inc.net, "3900.00")

' ------------------------------------------------------------- closing
closed = accounting.close(books, lg, jan31, "3000")
bs2 = accounting.balance_sheet(books, closed, nothing)
check("equity absorbs the net income", bs2.equity, "23900.00")
check("nothing left unclosed", bs2.earnings, "0.00")
check("assets are untouched by closing", bs2.assets, "24400.00")
check("and it still balances", bs2.balanced, true)

' Closing must not disturb the balance sheet accounts.
after = accounting.balances(closed, nothing)
check("cash unchanged by closing", after["1000"], "14500.00")
check("every revenue account zeroed", after["4000"], "0.00")
check("every expense account zeroed", after["5400"], "0.00")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
