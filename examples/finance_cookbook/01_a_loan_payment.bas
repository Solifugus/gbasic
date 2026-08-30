' Recipe 1 — What does this loan cost a month?
'
' The five time-value functions all solve one equation for a different unknown,
' and they take EXCEL'S ARGUMENT ORDER: rate first, then the number of periods,
' then the amount. `finance.pmt(0.005, 360, principal)` is `PMT(0.5%, 360,
' 250000)` in a spreadsheet, which is where the answer will be checked.
program main()
  load finance

  principal {USD}= "250000.00"

  ' RATES ARE PER PERIOD. A 6%/year loan paid monthly is 0.06/12 — the library
  ' will not guess, because compounding conventions vary by product.
  monthly = finance.periodic(0.06, 12)

  payment = finance.pmt(monthly, 360, principal)
  print "250,000 at 6% nominal over 30 years"
  print "  monthly payment: " + string(payment)

  ' Negative because the payment LEAVES you. Signs follow the spreadsheet
  ' convention throughout: received is positive, paid is negative.
  print "  paid in total:   " + string(payment * 360)
  print "  interest:        " + string(payment * 360 + principal)
end program
