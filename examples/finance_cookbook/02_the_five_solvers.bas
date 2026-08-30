' Recipe 2 — The same equation, five ways round.
'
' pv * (1+r)^n  +  pmt * ((1+r)^n - 1)/r  +  fv  =  0
'
' Each function solves it for one unknown, so any four of the quantities give
' you the fifth. That is the whole of time-value arithmetic.
program main()
  load finance

  principal {USD}= "250000.00"
  monthly = finance.periodic(0.06, 12)

  ' Solve for the PAYMENT.
  payment = finance.pmt(monthly, 360, principal)
  print "payment for 250,000 over 360:   " + string(payment)

  ' Solve for the RATE, given that payment. It must come back to where we
  ' started — which is how you check a solver you did not write.
  recovered = finance.rate(360, payment, principal)
  print "rate recovered from it (x12):   " + string(round(recovered * 12, 6))

  ' Solve for the NUMBER OF PERIODS.
  print "periods at that payment:        " + string(round(finance.nper(monthly, payment, principal), 2))

  ' Solve for the PRESENT VALUE of that stream.
  print "present value of the payments:  " + string(finance.pv(monthly, 360, payment))

  ' Solve for the FUTURE VALUE. Saving 500 a month for ten years:
  saving {USD}= "-500.00"
  print ""
  print "500/month for 10 years at 6%:   " + string(finance.fv(monthly, 120, saving))
end program
