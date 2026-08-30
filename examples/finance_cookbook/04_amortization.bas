' Recipe 4 — Where the money actually goes.
'
' `finance.schedule` returns one record per period: the payment, how much of it
' is interest, how much repays principal, and what is left owing.
program main()
  load finance

  loan {USD}= "10000.00"
  rows = finance.schedule(0.005, 12, loan)

  print "period  payment   interest  principal   balance"
  for each r in rows
    print (string(r.period) + "       " + string(r.payment) + "   " + string(r.interest)
           + "      " + string(r.principal) + "    " + string(r.balance))
  next

  ' THE LAST PAYMENT IS ADJUSTED so the balance lands exactly on zero. Every
  ' period is rounded to whole cents and those roundings accumulate; a schedule
  ' using one figure throughout would end owing a few cents. Lenders do this too.
  print ""
  print "final balance is exactly zero:   " + string(rows[11].balance = loan * 0)

  ' The parts must reconstruct the loan EXACTLY -- the check worth making,
  ' because a schedule that is off by a cent looks perfectly reasonable.
  total_principal = loan * 0
  total_interest = loan * 0
  for each r in rows
    total_principal = total_principal + r.principal
    total_interest = total_interest + r.interest
  next
  print "principal parts sum to the loan: " + string(total_principal = loan)
  print "total interest paid:             " + string(total_interest)
end program
