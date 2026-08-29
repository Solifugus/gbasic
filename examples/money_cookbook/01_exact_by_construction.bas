' Recipe 1 — Money is exact, and the way you write it matters.
'
' A gBASIC number is a double: about fifteen significant digits. Money is not
' a number — it is an exact integer of scaled units — but it still has to be
' WRITTEN somehow, and that is where exactness is won or lost.

program main(args)
  ' Decimal TEXT is parsed digit by digit. Nothing passes through a double.
  price {USD}= "19.95"
  print "from text    : " + string(price)

  ' A literal works too, and is also exact: gBASIC renders the number to its
  ' shortest decimal first, then parses that.
  same {USD}= 19.95
  print "from a literal: " + string(same)
  print "identical     : " + string(price = same)

  ' Exactness is the point. A double cannot add a cent a thousand times.
  total {USD}= "0.00"
  cent {USD}= "0.01"
  i = 0
  while i < 1000
    total = total + cent
    i += 1
  end while
  print ""
  print "0.01 added 1000 times = " + string(total)

  approx = 0.0
  i = 0
  while i < 1000
    approx = approx + 0.01
    i += 1
  end while
  print "the same in numbers   = " + string(approx) + "   <- not 10"

  ' Excess precision you WROTE is refused, because you wrote something money
  ' cannot hold. Excess precision that a CALCULATION produced is rounded,
  ' because `price * 1.08` always has seventeen digits and refusing it would
  ' make the type unusable.
  print ""
  on error goto next
  bad {USD}= "1.23456789"
  if error then
    print "authored 1.23456789 : " + error.message
    error.clear()
  end if
  on error stop
  computed {USD}= 19.95 * 1.08
  print "computed 19.95*1.08 : " + string(computed)
end program
