' Recipe 5 — "What do I owe if I pay it off on Thursday?"
'
' A payoff quote is not the balance. It is the balance PLUS interest accrued
' since the last event, plus anything still owed in fees — and it goes stale,
' so it carries a per-diem saying what each further day costs.
'
' `payoff` takes `as_of` rather than `on` because `on` is a reserved word and
' cannot be a parameter name.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "daily_simple",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  feb {date}= "2026-02-01"
  mar {date}= "2026-03-01"
  events = [{ on: feb, kind: "payment", amount: scheduled }]

  quote = lending.payoff(l, events, mar)

  print "payoff quote for 2026-03-01"
  print "  principal   " + string(quote.principal)
  print "  interest    " + string(quote.interest)
  print "  fees        " + string(quote.fees)
  print "  total       " + string(quote.total)
  print "  per diem    " + string(quote.per_diem)

  ' The parts must be the total. Worth asserting rather than trusting: a quote
  ' that is off by the accrued interest is a perfectly ordinary-looking number
  ' and the borrower pays it.
  print ""
  print "the parts are the total: "
  print "  " + string(quote.total = quote.principal + quote.interest + quote.fees)

  ' A quote goes stale. Two days later it is the per-diem larger — which is
  ' what the per-diem is FOR, and it is why a quote is dated.
  mar3 {date}= "2026-03-03"
  later = lending.payoff(l, events, mar3)
  print ""
  print "two days later the total is " + string(later.total)
  print "  which is larger by         " + string(later.total - quote.total)
  print "  and two per-diems are      " + string(quote.per_diem * 2)

  ' A quote for a date BEFORE the last event is refused. It is not a quote,
  ' it is a question about the past that the event list already answers.
  on error goto next
  jan {date}= "2026-01-10"
  bad = lending.payoff(l, events, jan)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
