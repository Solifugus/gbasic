' Recipe 8 — When there is more than one answer, and when there is none.
'
' A rate solver returns a plausible percentage whatever you feed it. These are
' the two cases where "a number came back" is not the same as "that is the
' answer", and gBASIC says so rather than letting you find out later.
program main()
  load finance

  ' A conventional project -- pay once, receive after -- changes sign ONCE and
  ' has exactly one rate. Silent, as it should be.
  a {USD}= "-1000.00"
  b {USD}= "600.00"
  print "conventional project: " + string(round(finance.irr([a, b, b]) * 100, 2)) + "%"

  ' This one pays out again at the end -- a decommissioning cost, a
  ' reinvestment. It changes sign TWICE, and Descartes says that admits two
  ' rates. Both 100% and 200% satisfy it exactly.
  print ""
  on warning goto next
  m0 {USD}= "-1000.00"
  m1 {USD}= "5000.00"
  m2 {USD}= "-6000.00"
  r = finance.irr([m0, m1, m2])
  w = warning
  print "two sign changes: a rate comes back (" + string(round(r * 100, 2)) + "%)"
  print "and gBASIC says so:"
  print "  " + w.message
  on warning print

  ' Check it: the OTHER root satisfies the equation just as exactly.
  print ""
  print "npv at 100%: " + string(finance.npv(1.0, [m1, m2]) + m0)
  print "npv at 200%: " + string(finance.npv(2.0, [m1, m2]) + m0)

  ' And the case with no answer at all is an error, not a number.
  print ""
  on error goto next
  never = finance.irr([b, b])
  if error then
    print "all-positive flows: " + error.message
    error.clear()
  end if
  on error stop
end program
