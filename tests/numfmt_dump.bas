' PLAT-NUMFMT oracle input. Each line is a RECIPE followed by how gBASIC
' rendered the value that recipe produces:
'
'     div 7 3<TAB>2.3333333333333335
'
' The recipe is the point. An oracle that read only the rendered text could
' never catch truncation -- it would parse "0.333333", find that the shortest
' form of THAT double is "0.333333", and agree with a broken renderer. (This is
' not hypothetical: the first version of this fixture printed values alone, and
' the oracle tier passed against the pre-change six-digit binary.) So the
' checker recomputes each value from the recipe in its own arithmetic and never
' reads our number at all; the two only meet at the comparison.
'
' Every operation here is one IEEE-754 primitive -- divide, multiply, add,
' sqrt -- each of which is required to be correctly rounded, so awk and gBASIC
' land on the identical double independently.
'
' The population is GENERATED rather than written down, because a hand-picked
' list only contains cases someone already thought of, and the interesting
' doubles are the ones nobody would choose: mantissas needing all seventeen
' digits, values just under a rounding boundary, and exponents at both ends.
program main(args)
  ' quotients, positive and negated: dense coverage of short and awkward
  ' mantissas alike
  i = 1
  while i <= 80
    j = 1
    while j <= 16
      print("div " + string(i) + " " + string(j) + "\t" + string(i / j))
      print("ndiv " + string(i) + " " + string(j) + "\t" + string(0 - i / j))
      j = j + 1
    end while
    i = i + 1
  end while

  ' roots and reciprocals: irrational-ish mantissas at full width
  i = 1
  while i <= 300
    print("sqrt " + string(i) + "\t" + string(sqrt(i)))
    print("recip " + string(i) + "\t" + string(1 / i))
    i = i + 1
  end while

  ' harmonic sums, where rounding error compounds into mantissas with no short
  ' form. Order-dependent, so the checker adds the terms in the same order.
  acc = 0
  i = 1
  while i <= 300
    acc = acc + 1 / i
    print("harm " + string(i) + "\t" + string(acc))
    i = i + 1
  end while

  ' scaling across the whole exponent range in both directions, so the oracle
  ' sees the fixed/exponent crossover from both sides and reaches subnormals
  ' and infinity's doorstep rather than only the neighbourhood of 1
  x = 1
  i = 1
  while i <= 300
    x = x / 7
    print("down7 " + string(i) + "\t" + string(x))
    i = i + 1
  end while

  y = 1
  i = 1
  while i <= 300
    y = y * 7
    print("up7 " + string(i) + "\t" + string(y))
    i = i + 1
  end while

  ' integer-valued doubles around the plain-form boundary at 2^53, where the
  ' rule changes from "%.0f in full" to the shortest round-tripping form.
  ' Written as literals: an integer's decimal text is exact in both languages,
  ' so reading it back introduces no ambiguity for the checker to launder.
  print("lit 9007199254740990\t" + string(9007199254740990))
  print("lit 9007199254740991\t" + string(9007199254740991))
  print("lit 9007199254740992\t" + string(9007199254740992))
  print("lit 9007199254740994\t" + string(9007199254740994))
  print("lit -9007199254740991\t" + string(0 - 9007199254740991))
  print("lit 0\t" + string(0))
  print("lit 1\t" + string(1))
  print("lit -1\t" + string(0 - 1))
  print("lit 42\t" + string(42))
  print("lit 1786000000\t" + string(1786000000))
end program
