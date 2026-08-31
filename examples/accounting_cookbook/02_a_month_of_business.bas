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
