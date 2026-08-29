' Recipe 4 — Splitting money into amounts you can actually pay.
'
' Division and allocation are different problems. `100.00 / 3` is a perfectly
' good number and keeps its guard digits — but three PAYMENTS cannot each be
' 33.3333. An invoice line, a payroll entry or a dividend has to be a whole
' number of minor units.

program main(args)
  bill {USD}= "100.00"

  parts = money.allocate(bill, 3)
  print "100.00 three ways: " + string(parts)

  ' THE PROPERTY THAT MATTERS: they sum back to the original exactly. Three of
  ' 33.33 would lose a cent; three of 33.34 would invent one. Both look
  ' perfectly reasonable on the page.
  total {USD}= "0.00"
  for each p in parts
    total = total + p
  next
  print "  they sum to:     " + string(total)

  ' Weights, for anything proportional: shares, floor area, headcount.
  print ""
  shares = money.allocate(bill, [1, 1, 2])
  print "split 1:1:2      : " + string(shares)
  awkward = money.allocate(bill, [1, 1, 1, 1, 1, 1, 7])
  print "split six and one: " + string(awkward)
  t2 {USD}= "0.00"
  for each p in awkward
    t2 = t2 + p
  next
  print "  still exact:     " + string(t2 = bill)

  ' Currency is respected: yen split into whole yen.
  print ""
  y {JPY}= "100"
  print "JPY 100 three ways: " + string(money.allocate(y, 3))
end program
