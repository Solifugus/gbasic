' Recipe 1 — A loan is a set of conventions, not a number.
'
' `finance` answers what the payment is. `lending` answers what happens next —
' and what happens next depends on three choices that no formula settles:
' how interest accrues, what order a payment is applied in, and how days are
' counted. Each changes the balance. None has a defensible default, so the
' loan DECLARES all three and the library never assumes.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12, opened: opened,
                     basis:     "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  ' The rate on the loan is ANNUAL. The rate it accrues at is per period, and
  ' the loan's own `periods_per_year` (12 unless you say otherwise) converts it.
  print "annual rate:       " + string(l.rate)
  print "periods per year:  " + string(l.periods_per_year)
  print "period rate:       " + string(lending.period_rate(l))
  print "scheduled payment: " + string(lending.payment(l))

  ' The schedule is `finance.schedule` in the loan's own terms. Its last
  ' payment is adjusted so the balance lands exactly on zero.
  rows = lending.schedule(l)
  print ""
  print "first payment:  interest " + string(rows[0].interest)
  print "                principal " + string(rows[0].principal)
  print "last payment:   interest " + string(rows[11].interest)
  print "                principal " + string(rows[11].principal)
  print "final balance:  " + string(rows[11].balance)

  ' A MISSING CONVENTION IS REFUSED, not filled in. Amortized and daily-simple
  ' accrual are different loans; guessing is wrong by a few dollars a month,
  ' compounding, with nothing on screen to say so.
  on error goto next
  guess = lending.loan({ principal: amount, rate: 0.12, term: 12, opened: opened,
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
