' Recipe 7 — How much of a year is that?
'
' Every accrual rests on this, and the conventions DISAGREE. There is no
' default: you name the one your contract uses, because none is dominant and a
' guess would be wrong somewhere without saying so.
program main()
  load finance

  start {date}= "2026-01-31"
  finish {date}= "2026-03-31"

  print "31 Jan 2026 to 31 Mar 2026 is 59 actual days"
  print "  actual/360:     " + string(round(finance.year_fraction(start, finish, "actual/360"), 8))
  print "  actual/365:     " + string(round(finance.year_fraction(start, finish, "actual/365"), 8))
  print "  actual/actual:  " + string(round(finance.year_fraction(start, finish, "actual/actual"), 8))
  print "  30/360:         " + string(round(finance.year_fraction(start, finish, "30/360"), 8))

  ' On a million dollars at 5%, that spread is real money.
  balance {USD}= "1000000.00"
  print ""
  print "interest at 5% on 1,000,000 for that period"
  print "  actual/360:     " + string(balance * (0.05 * finance.year_fraction(start, finish, "actual/360")))
  print "  30/360:         " + string(balance * (0.05 * finance.year_fraction(start, finish, "30/360")))

  ' actual/actual weights each day by the length of ITS OWN year, so a leap
  ' year is exactly one year and a span across several is split at the
  ' boundaries.
  ly {date}= "2024-01-01"
  ly2 {date}= "2025-01-01"
  print ""
  print "2024 (a leap year) under actual/actual: " + string(finance.year_fraction(ly, ly2, "actual/actual"))
  print "  the same span under actual/365:      " + string(round(finance.year_fraction(ly, ly2, "actual/365"), 8))
end program
