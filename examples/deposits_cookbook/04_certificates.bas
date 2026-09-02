' Recipe 4 — Certificates, and the penalty that eats principal.
'
' A certificate is an account with a term and an early-withdrawal penalty
' measured in days of interest. The penalty is computed on the PRINCIPAL, for
' its own number of days, regardless of how long the money has actually been
' there — which is why redeeming a one-year certificate after a month can
' return LESS than was put in.
'
' The library does not clamp that at zero. Clamping would report proceeds the
' holder is not going to receive: a plausible number, and the wrong one.
program main()
  load deposits

  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"

  cd = deposits.certificate({ opened: opened, balance: balance, rate: 0.05,
                              day_count: "actual/365",
                              balance_method: "daily", crediting: 30,
                              term_days: 365, penalty_days: 90 })

  print "a 365-day certificate with a 90-day penalty"
  print "  matures on " + ymd(deposits.matures(cd))

  ' Redeemed after one month. Thirty-one days of interest earned; ninety days
  ' of interest charged.
  early {date}= "2026-02-01"
  r = deposits.redeem(cd, early)

  print ""
  print "redeemed early, on 2026-02-01:"
  print "  principal          " + string(r.principal)
  print "  interest earned    " + string(r.interest) + "   (31 days)"
  print "  penalty            " + string(r.penalty) + "  (90 days)"
  print "  proceeds           " + string(r.proceeds)
  print ""
  print "  was it early?              " + string(r.early)
  print "  did the penalty exceed the interest? " + string(r.principal_reduced)
  print "  so the holder gets back less than they put in: "
  print "    " + string(r.proceeds < r.principal)

  ' THE CONTROL. Without a maturity case, a library that always penalised
  ' would satisfy everything above.
  m = deposits.redeem(cd, deposits.matures(cd))
  print ""
  print "redeemed at maturity:"
  print "  interest earned    " + string(m.interest)
  print "  penalty            " + string(m.penalty)
  print "  proceeds           " + string(m.proceeds)
  print "  was it early?      " + string(m.early)
  print "  proceeds are principal plus interest: "
  print "    " + string(m.proceeds = m.principal + m.interest)

  ' Somewhere between the two the penalty stops biting. Past 90 days of
  ' earnings, an early redemption still returns more than the principal — it
  ' just returns less than waiting would have.
  late {date}= "2026-10-01"
  l = deposits.redeem(cd, late)
  print ""
  print "redeemed early but late in the term, on 2026-10-01:"
  print "  interest earned    " + string(l.interest)
  print "  penalty            " + string(l.penalty)
  print "  proceeds           " + string(l.proceeds)
  print "  principal intact?  " + string(l.proceeds > l.principal)
  print "  but worse than waiting: " + string(l.proceeds < m.proceeds)
end program

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
