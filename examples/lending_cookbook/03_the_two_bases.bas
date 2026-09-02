' Recipe 3 — Accrual basis: two different loans wearing the same numbers.
'
' `amortized` accrues one period's interest per whole period elapsed since
' origination, and nothing for a part period. `daily_simple` runs a meter: the
' actual balance for the actual days.
'
' Same principal, same rate, same payment. Different loans. This is why the
' basis is declared and never inferred — and why the first version of the
' library was WRONG here: it prorated the amortized basis by days, which makes
' the two algebraically identical. The bases agreed to the cent and the
' declaration was decorative. The test that caught it is the one below: the
' two must DIFFER, and the difference must be the days.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  amort = lending.loan({ principal: amount, rate: 0.12, term: 12,
                         opened: opened, basis: "amortized",
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })
  daily = lending.loan({ principal: amount, rate: 0.12, term: 12,
                         opened: opened, basis: "daily_simple",
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })

  feb {date}= "2026-02-01"
  jan27 {date}= "2026-01-27"

  on_time = [{ on: feb, kind: "payment", amount: scheduled }]
  early   = [{ on: jan27, kind: "payment", amount: scheduled }]

  a1 = lending.apply(amort, on_time, false)
  d1 = lending.apply(daily, on_time, false)

  print "one payment, on the due date (31 days after origination)"
  print "  amortized      interest " + string(a1.paid_interest)
  print "  daily_simple   interest " + string(d1.paid_interest)
  print ("  daily-simple charges for the 31st day too, so it is dearer by "
         + string(d1.paid_interest - a1.paid_interest))

  a2 = lending.apply(amort, early, false)
  d2 = lending.apply(daily, early, false)

  print ""
  print "the same payment five days early (26 days after origination)"
  print "  amortized      interest " + string(a2.paid_interest)
  print "  daily_simple   interest " + string(d2.paid_interest)

  ' That is the difference, stated plainly. On a daily-simple loan paying early
  ' saves the borrower five days, because the meter stopped five days sooner.
  ' On an amortized loan the interest belongs to the PERIOD; on the 27th no
  ' period has closed, so nothing has accrued and paying early buys nothing.
  ' Neither is a bug. They are different products, and a borrower who thinks
  ' they have one when they have the other is surprised at payoff.
  print ""
  print ("paying early saves interest on a daily-simple loan: "
         + string(d2.paid_interest < d1.paid_interest))
  print "on an amortized loan the interest arrives with the period, not the day."

  ' So the balances part company, which is the whole point.
  print ""
  print "balance after the on-time payment"
  print "  amortized     " + string(a1.balance)
  print "  daily_simple  " + string(d1.balance)
  print "  the two bases differ: " + string(a1.balance != d1.balance)
end program
