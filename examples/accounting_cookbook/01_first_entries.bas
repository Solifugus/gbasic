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
