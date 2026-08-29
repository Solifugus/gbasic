' Recipe 2 — Every currency, with its own number of decimal places.
'
' Every ISO 4217 code is an assignment modifier. Each carries its own
' minor-unit exponent, which is not decoration: JPY has no decimal places at
' all and KWD has three, so a cents-shaped money type cannot represent either.

program main(args)
  u {USD}= "19.95"
  e {EUR}= "19.95"
  j {JPY}= "1995"
  k {KWD}= "19.950"

  print "USD " + string(u)
  print "EUR " + string(e)
  print "JPY " + string(j) + "     <- no decimal places"
  print "KWD " + string(k) + "  <- three"

  ' A money value KNOWS its currency, so the mistakes that matter are caught.
  print ""
  on error goto next
  x = u + e
  if error then
    print "USD + EUR : " + error.message
    error.clear()
  end if
  y = u < e
  if error then
    print "USD < EUR : " + error.message
    error.clear()
  end if
  on error stop

  ' But equality ANSWERS rather than raising. "Is 19.95 USD the same as 19.95
  ' EUR" is a real question, and the answer is no; "is it less than" is not a
  ' question at all without an exchange rate.
  print "USD = EUR : " + string(u = e)

  ' The built-in table is the CURRENT ISO list, so withdrawn currencies are
  ' not in it. Register what your data needs.
  print ""
  money.register("ITL", 0)
  lira {ITL}= "1000"
  print "registered ITL: " + string(lira)
  print "currencies known: " + string(count(money.currencies()))
end program
