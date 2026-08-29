' Recipe 9 — Writing an asset down.
'
' Three conventions, and which you use is an accounting policy rather than a
' calculation: straight line spreads the cost evenly, and the other two
' front-load it.

program main(args)
  load finance

  cost {USD}= "50000.00"
  salvage {USD}= "5000.00"
  life = 10

  print "a 50,000 asset, 5,000 salvage, 10 years"
  print ""
  print "straight line, every year: " + string(finance.sln(cost, salvage, life))

  print ""
  print "year   sum-of-years  declining-balance"
  y = 1
  while y <= 5
    print pad(string(y), 7) + pad(string(finance.syd(cost, salvage, life, y)), 14) + string(finance.ddb(cost, salvage, life, y))
    y += 1
  end while

  ' Sum-of-years-digits spreads the whole depreciable base over the life, so
  ' the yearly charges add up to cost minus salvage exactly.
  total {USD}= "0.00"
  y = 1
  while y <= life
    total = total + finance.syd(cost, salvage, life, y)
    y += 1
  end while
  print ""
  print "sum-of-years total:  " + string(total)
  print "cost minus salvage:  " + string(cost - salvage)

  ' Declining balance is floored at the salvage value, so an asset is never
  ' written below what it is worth.
  print ""
  print "declining balance, year 10: " + string(finance.ddb(cost, salvage, life, 10))
end program

function pad(s, n)
  out = s
  while len(out) < n
    out = out + " "
  end while
  return out
end function
