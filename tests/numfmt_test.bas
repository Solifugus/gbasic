' PLAT-NUMFMT semantics fixture. Golden-compared by tests/run_numfmt.sh.
'
' `print`, `string()` and `quote()` all render a number through one function
' (format_number in src/eval.c). It used to emit %g -- six significant digits --
' so 265550.75 printed as 265551 and a program could compute a difference it had
' no way to show. It now emits the SHORTEST decimal that reads back as the same
' double.
'
' The load-bearing tier here is not the golden, it is ROUND-TRIP: for every value
' below, number(string(x)) must equal x. That is the defining property of the
' format, it is checkable inside the language, and it stays true no matter how
' many digits any particular value needs -- so it cannot rot the way a list of
' expected digit strings can. The golden then pins the SHORTNESS, which
' round-trip alone does not: %.17g also round-trips and would render 0.1 as
' 0.10000000000000001.

function rt(label, x)
  ' the property: rendering a number and reading it back is lossless
  if number(string(x)) = x then
    return label + " roundtrip ok"
  end if
  return label + " ROUNDTRIP LOST " + string(x) + " -> " + string(number(string(x)))
end function

function shows(label, x, expected)
  if string(x) = expected then
    return label + " ok"
  end if
  return label + " MISMATCH got [" + string(x) + "] want [" + expected + "]"
end function

program main(args)
  ' --- the three cases DOGFOOD 2026-08-12 (b) named -------------------------
  print(shows("money-ish", 265550.75, "265550.75"))
  print(shows("cents", 23750.25, "23750.25"))
  print(shows("date serial", 46237.5674884, "46237.5674884"))

  ' --- shortness: none of these may grow spurious digits --------------------
  ' 0.1 is the one that fails under %.17g (0.10000000000000001), which also
  ' round-trips -- so this line is what separates "shortest" from "faithful".
  print(shows("tenth", 0.1, "0.1"))
  print(shows("half", 0.5, "0.5"))
  print(shows("three halves", 3.5, "3.5"))
  print(shows("quarter", 0.25, "0.25"))

  ' --- honesty: a difference the code computes must be visible --------------
  ' 0.1 + 0.2 is not 0.3 in binary floating point. Under the old %g, and under
  ' %.15g, this printed as 0.3 -- the format hiding a value the program really
  ' held. It is deliberately shown.
  print(shows("point-one plus point-two", 0.1 + 0.2, "0.30000000000000004"))
  print(shows("third", 1 / 3, "0.3333333333333333"))
  print(shows("two thirds", 2 / 3, "0.6666666666666666"))

  ' --- integers keep the plain form (the pre-existing branch) ---------------
  ' Integer-valued doubles below 2^53 print in full with no exponent and no
  ' decimal point, so epoch seconds, ids and bitwise results read correctly.
  print(shows("small int", 42, "42"))
  print(shows("int-valued float", 3.0, "3"))
  print(shows("negative int", -17, "-17"))
  print(shows("epoch-sized", 1786000000, "1786000000"))
  print(shows("2^53 - 1", 9007199254740991, "9007199254740991"))
  print(shows("bitwise result", shl(1, 31), "2147483648"))
  print(shows("zero", 0, "0"))

  ' --- print / string() / quote() must agree, since they share one formatter -
  v = 265550.75
  print("parity print below, string [" + string(v) + "], quote [" + quote(v) + "]")
  print(v)

  ' --- round-trip over a generated battery ----------------------------------
  ' Values nobody wrote down by hand: quotients, roots and accumulations, which
  ' between them land on doubles needing every width from 1 to 17 digits.
  lost = 0
  checked = 0
  i = 1
  while i <= 60
    j = 1
    while j <= 12
      x = i / j
      if number(string(x)) != x then
        lost = lost + 1
        print("LOST quotient " + string(i) + "/" + string(j) + " -> " + string(x))
      end if
      checked = checked + 1
      j = j + 1
    end while
    i = i + 1
  end while

  i = 1
  while i <= 200
    x = sqrt(i)
    if number(string(x)) != x then
      lost = lost + 1
      print("LOST sqrt " + string(i) + " -> " + string(x))
    end if
    checked = checked + 1
    x = 1 / i
    if number(string(x)) != x then
      lost = lost + 1
      print("LOST reciprocal " + string(i) + " -> " + string(x))
    end if
    checked = checked + 1
    i = i + 1
  end while

  ' an accumulation, where the error compounds into awkward mantissas
  acc = 0
  i = 1
  while i <= 300
    acc = acc + 1 / i
    if number(string(acc)) != acc then
      lost = lost + 1
      print("LOST accumulation at " + string(i) + " -> " + string(acc))
    end if
    checked = checked + 1
    i = i + 1
  end while

  print("roundtrip battery: " + string(checked) + " values, " + string(lost) + " lost")

  ' --- named round-trip checks, incl. the awkward ones ----------------------
  print(rt("tiny", 0.0000001))
  print(rt("big non-integer", 12345678901234.5))
  print(rt("negative fraction", -0.007))
  print(rt("near-integer", 2.9999999999999996))

  ' --- extremes, and the WIDTH they need ------------------------------------
  ' The renderings below are the longest this format can produce, and they are
  ' pinned because the change made output LONGER: format_number writes into
  ' fixed 32-byte buffers at two call sites (128 at the other two), and %g with
  ' six digits could never have overflowed them while seventeen digits plus a
  ' sign, a point and a three-digit exponent comes within a few bytes. The
  ' `widest` line asserts the length directly, so a buffer that stops being big
  ' enough fails here as a number rather than as a truncated string somewhere
  ' downstream.
  '
  ' Note these are built with number("...") rather than written as literals:
  ' gBASIC has no exponent literal -- `1e20` lexes as a DURATION and raises
  ' "unknown duration unit: e20". See DOGFOOD 2026-08-14.
  print(shows("widest negative", number("-1.2345678901234567e-308"), "-1.2345678901234567e-308"))
  print("widest is " + string(len(string(number("-1.2345678901234567e-308")))) + " chars")
  print(shows("largest double", number("1.7976931348623157e308"), "1.7976931348623157e+308"))
  print(shows("smallest subnormal", number("5e-324"), "5e-324"))
  print(shows("smallest normal", number("2.2250738585072014e-308"), "2.2250738585072014e-308"))
  ' Integer-valued but at or above 2^53, so the plain-integer branch does not
  ' apply and it renders in exponent form -- deliberate, since past 2^53 the
  ' digits an integer form would show are not all real.
  print(shows("1e20", number("1e20"), "1e+20"))
  print(shows("negative zero", number("-0.0"), "-0"))
  print(rt("largest double", number("1.7976931348623157e308")))
  print(rt("smallest subnormal", number("5e-324")))
  print(rt("smallest normal", number("2.2250738585072014e-308")))
end program
