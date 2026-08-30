' Recipe 3 — Nominal, effective, periodic: three numbers, one loan.
'
' "12% compounded monthly" and "12% a year" are different loans. Getting this
' wrong is not a rounding difference — it is 68 basis points.
program main()
  load finance

  quoted = 0.12

  print "quoted (nominal, monthly):  " + string(quoted)
  print "  one month of it:          " + string(finance.periodic(quoted, 12))
  print "  what it actually earns:   " + string(round(finance.effective(quoted, 12), 6))
  print "  the difference:           " + string(round(finance.effective(quoted, 12) - quoted, 6))

  ' Compounding more often earns more, converging on the continuous rate.
  print ""
  print "the same 12% nominal, compounded"
  print "  yearly:     " + string(round(finance.effective(quoted, 1), 6))
  print "  quarterly:  " + string(round(finance.effective(quoted, 4), 6))
  print "  monthly:    " + string(round(finance.effective(quoted, 12), 6))
  print "  daily:      " + string(round(finance.effective(quoted, 365), 6))

  ' Going the other way: a bank advertises an APY. What nominal rate is that?
  print ""
  apy = 0.05
  print "an advertised APY of 5% is a nominal rate of"
  print "  " + string(round(finance.nominal(apy, 12), 6)) + " compounded monthly"

  ' And the exact inverse, which is how you know the pair is trustworthy.
  print "  back to APY: " + string(round(finance.effective(finance.nominal(apy, 12), 12), 6))
end program
