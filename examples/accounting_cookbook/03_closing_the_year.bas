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
