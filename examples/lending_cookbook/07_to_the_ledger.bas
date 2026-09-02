' Recipe 7 — Posting a loan to real books.
'
' `lending.entries` EMITS entries; it never posts them. The caller owns the
' ledger, so an application posts to its own chart, batches, or throws them
' away. `accounts` maps the four roles onto your own codes.
'
' This is also how the arithmetic is PROVED rather than asserted. An entry
' that does not balance, or that names an account not in the chart, is refused
' where it is posted — so a loan whose whole life posts cleanly, leaving
' receivables equal to the servicing balance, has demonstrated its own figures.
program main()
  load lending
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash",             kind: "asset" },
    { code: "1200", name: "Loans receivable", kind: "asset" },
    { code: "4100", name: "Interest income",  kind: "revenue" },
    { code: "4200", name: "Fee income",       kind: "revenue" }
  ])

  accounts = { }
  accounts["receivable"]      = "1200"
  accounts["cash"]            = "1000"
  accounts["interest_income"] = "4100"
  accounts["fee_income"]      = "4200"

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  ' Eleven monthly payments, February to December.
  events = []
  for m = 2 to 12
    d {date}= "2026-" + two(m) + "-01"
    append(events, { on: d, kind: "payment", amount: scheduled })
  next

  entries = lending.entries(books, l, events, accounts)
  lg = accounting.ledger()
  for each e in entries
    lg = accounting.post(lg, e)
  next

  print "entries emitted: " + string(count(entries))
  print "  one advance at origination, plus one per payment received."

  bal = accounting.balances(lg, nothing)
  sheet = accounting.balance_sheet(books, lg, nothing)
  serviced = lending.apply(l, events, false)

  print ""
  print "loans receivable on the books:  " + string(bal["1200"])
  print "servicing balance on the loan:  " + string(serviced.balance)
  print "they agree:                     " + string(bal["1200"] = serviced.balance)

  ' Revenue carries a credit balance, which `balances` reports as negative
  ' against the debit convention. Flip the sign to compare with cash collected.
  print ""
  print "interest income recognised:     " + string(bal["4100"] * -1)
  print "interest actually collected:    " + string(serviced.paid_interest)
  print "they agree:                     " + string(bal["4100"] * -1 = serviced.paid_interest)

  print ""
  print "and the ledger balances:        " + string(sheet.balanced)
end program

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
