' Recipe 7 — The schedule behind the payment.
'
' `finance.schedule` returns one record per period: the payment, how much of
' it is interest, how much reduces the balance, and what is left.
'
' THE LAST PAYMENT IS ADJUSTED so the balance lands exactly on zero. Every
' payment is whole minor units — you cannot pay a third of a cent — and those
' roundings accumulate, so a schedule using one figure throughout would end
' owing a few cents or having overpaid. Lenders do the same.

program main(args)
  load finance

  loan {USD}= "10000.00"
  rows = finance.schedule(0.005, 12, loan)

  print "period   payment  interest principal   balance"
  for each r in rows
    print pad(string(r.period), 6) + pad(string(r.payment), 10) + pad(string(r.interest), 9) + pad(string(r.principal), 10) + pad(string(r.balance), 10)
  next

  ' The properties worth checking, and they are arithmetic rather than
  ' eyeballed: the balance ends at zero and the principal parts sum to the loan.
  zero {USD}= "0.00"
  last = rows[count(rows) - 1]
  print ""
  print "final balance is zero:            " + string(last.balance = zero)

  paid {USD}= "0.00"
  interest {USD}= "0.00"
  for each r in rows
    paid = paid + r.principal
    interest = interest + r.interest
  next
  print "principal parts sum to the loan:  " + string(paid = loan)
  print "total interest paid:              " + string(interest)
end program

function pad(s, n)
  out = s
  while len(out) < n
    out = out + " "
  end while
  return out
end function
