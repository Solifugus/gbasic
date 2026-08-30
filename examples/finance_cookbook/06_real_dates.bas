' Recipe 6 — Flows that fall on real dates.
'
' Projects do not pay on tidy period boundaries. `xnpv` and `xirr` take a date
' per flow and discount Actual/365 from the first one, which is Excel's
' definition -- so the answers match the spreadsheet they will be checked in.
program main()
  load finance

  f0 {USD}= "-10000.00"
  f1 {USD}= "2750.00"
  f2 {USD}= "4250.00"
  f3 {USD}= "3250.00"
  f4 {USD}= "2750.00"
  t0 {date}= "2008-01-01"
  t1 {date}= "2008-03-01"
  t2 {date}= "2008-10-30"
  t3 {date}= "2009-02-15"
  t4 {date}= "2009-04-01"

  flows = [f0, f1, f2, f3, f4]
  when = [t0, t1, t2, t3, t4]

  print "xirr:          " + string(round(finance.xirr(flows, when) * 100, 4)) + "%"
  print "xnpv at 9%:    " + string(finance.xnpv(0.09, flows, when))

  ' The defining relationship: discounted at its own IRR, a project is worth
  ' nothing. Checking this is how you know the two agree.
  at_root = finance.xnpv(finance.xirr(flows, when), flows, when)
  print "xnpv at xirr:  " + string(at_root) + "   <- zero, by definition"

  ' The same flows treated as EVEN periods give a different answer, because
  ' they are not evenly spaced. That gap is why the dated form exists.
  print ""
  print "as if evenly spaced: " + string(round(finance.irr(flows) * 100, 4)) + "%"
end program
