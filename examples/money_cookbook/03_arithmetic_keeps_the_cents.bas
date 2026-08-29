' Recipe 3 — Arithmetic that does not quietly lose money.
'
' Money stores four GUARD DIGITS below the minor unit. Interest, unit costs
' and conversions all produce values below a cent, and rounding each one as it
' appears loses money across a multi-step calculation.

program main(args)
  h {USD}= "100.00"

  ' A third of a hundred dollars has somewhere to live, so it comes back.
  print "(100.00 / 3) * 3 = " + string((h / 3) * 3) + "   <- 99.99 without guard digits"
  print "(1.00 / 7) * 7   = " + string((one_dollar() / 7) * 7)

  ' A third of a CENT is invisible at display precision and still retained.
  cent {USD}= "0.01"
  third = cent / 3
  print ""
  print "0.01 / 3 displays as " + string(third)
  print "  but x3 gives back " + string(third * 3)

  ' Multiplying by a whole number is exact, with no rounding decision at all.
  big {USD}= "50000000000.01"
  print ""
  print "50000000000.01 x 3 = " + string(big * 3)

  ' Overflow raises rather than wrapping. Guard digits cost range: USD spans
  ' about plus or minus 9.22 trillion, which is generous but not infinite.
  on error goto next
  ceiling {USD}= "9223372036854.77"
  over = ceiling + cent
  if error then
    print ""
    print "past the ceiling: " + error.message
    error.clear()
  end if
  on error stop
end program

function one_dollar()
  d {USD}= "1.00"
  return d
end function
