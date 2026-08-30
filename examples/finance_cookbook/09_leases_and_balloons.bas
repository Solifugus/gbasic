' Recipe 9 — Payments at the start, and money still owed at the end.
'
' Every solver takes two more arguments you can ignore until you need them:
' `fv`, a balance still outstanding when the term ends, and `timing`, which is
' "end" (the default) or "begin". Leases are paid in advance; car loans often
' end in a balloon.
program main()
  load finance

  monthly = finance.periodic(0.06, 12)
  principal {USD}= "250000.00"

  ' A lease is paid at the START of each period, so every payment earns one
  ' extra period of interest and the payment is smaller.
  at_end = finance.pmt(monthly, 360, principal)
  at_begin = finance.pmt(monthly, 360, principal, 0, "begin")
  print "paid in arrears: " + string(at_end)
  print "paid in advance: " + string(at_begin)
  print "  smaller by:    " + string(at_end - at_begin)

  ' A balloon: 50,000 still owed at the end of the term. Less principal is
  ' amortized, so the monthly payment falls.
  balloon {USD}= "-50000.00"
  print ""
  print "with 50,000 owing at the end: " + string(finance.pmt(monthly, 360, principal, balloon))
  print "  versus fully amortized:     " + string(at_end)

  ' Saving TOWARDS a target is the same equation read the other way: how much
  ' must go in each month to reach 100,000 in fifteen years?
  target {USD}= "100000.00"
  nothing_now {USD}= "0.00"
  print ""
  print "to reach 100,000 in 15 years at 6%:"
  print "  save each month: " + string(finance.pmt(monthly, 180, nothing_now, target))

  ' And omitting the tail must equal supplying its defaults -- the property
  ' that makes the short form safe to use everywhere.
  print ""
  print "short form equals the full one: " + string(finance.pmt(monthly, 360, principal) = finance.pmt(monthly, 360, principal, 0, "end"))
end program
