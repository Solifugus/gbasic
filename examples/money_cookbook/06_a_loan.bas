' Recipe 6 — What a loan actually costs.
'
' Rates here are PER PERIOD, not per year. A 6% annual loan paid monthly is
' `0.06 / 12`. That arithmetic is yours on purpose: compounding conventions
' differ by product and jurisdiction, and a library that guessed would be
' wrong somewhere without telling you.

program main(args)
  load finance

  principal {USD}= "250000.00"
  monthly = 0.06 / 12

  payment = finance.pmt(monthly, 360, principal)
  print "250,000 at 6% over 30 years"
  print "  monthly payment: " + string(payment)

  ' The sign convention is the spreadsheet one — money you pay is negative —
  ' because that is what you will check the answer against.
  print "  paid in total:   " + string(payment * 360)

  ' What is a stream of payments worth today?
  print ""
  rent {USD}= "-2500.00"
  print "a 5-year lease at 2,500/month, 6%/yr, is worth"
  print "  " + string(finance.pv(monthly, 60, rent)) + " today"

  ' How long would a given payment take?
  print ""
  ' Negative: the payment leaves you. Excel's convention throughout.
  pay {USD}= "-2000.00"
  print "paying 2,000/month instead clears it in "
  print "  " + string(round(finance.nper(monthly, pay, principal), 1)) + " months"
end program
