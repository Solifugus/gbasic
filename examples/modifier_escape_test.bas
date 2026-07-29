' A string literal means the same thing inside a modifier clause as anywhere
' else (PLAT-DEBT 4).
'
' It did not used to. `p(split "\n") = text` split on a literal backslash-n and
' returned ONE element, so a user got a wrong answer with nothing in it to
' suggest the escape was the problem.
'
' A modifier clause is deliberately captured as raw text -- that is what lets a
' clause hold a multi-word phrase like `split ","` whose name and argument can
' only be separated once the registered modifiers are known -- so its argument
' passes through THREE separate string scanners on its way to being a value:
'
'   1. modifier_lparen_ahead   (src/parser.y)  decides it is a clause at all
'   2. modifier_content_token  (src/lexer.c)   finds the `)` that closes it
'   3. modifier_string_literal (src/eval.c)    turns the argument into a value
'
' Each one has to agree about where a string ends and what its escapes mean.
' This file is the parity harness that holds all three in step with the ordinary
' string literal path in the parser: every escape is emitted BOTH ways and the
' two must be byte-identical. Fixing one scanner and not the others fails here.

' Byte codes of a string -- printed rather than the string itself, so that a
' control character or an invalid byte cannot corrupt the golden.
function codes(s)
  o = ""
  i = 0
  n = byte_count(s)
  while i < n
    if i > 0 then
      o = o + ","
    end if
    o = o + byte_at(s, i)
    i = i + 1
  end while
  return o
end function

function report(label, from_clause, from_literal)
  a = codes(from_clause)
  b = codes(from_literal)
  same = (a = b)
  print label + " clause=[" + a + "] literal=[" + b + "] identical=" + same
  return nothing
end function

program main(args)
  pair = ["a", "b"]

  print "-- every escape, through a modifier clause and through a literal"

  n1(join "\n") = pair
  report("newline  ", n1, "a" + "\n" + "b")

  t1(join "\t") = pair
  report("tab      ", t1, "a" + "\t" + "b")

  q1(join "\"") = pair
  report("quote    ", q1, "a" + "\"" + "b")

  b1(join "\\") = pair
  report("backslash", b1, "a" + "\\" + "b")

  u1(join "\u{263A}") = pair
  report("u+263A   ", u1, "a" + "\u{263A}" + "b")

  u2(join "\u{1F600}") = pair
  report("u+1F600  ", u2, "a" + "\u{1F600}" + "b")

  p1(join "plain") = pair
  report("plain    ", p1, "a" + "plain" + "b")

  m1(join "x\ny\tz") = pair
  report("mixed    ", m1, "a" + "x\ny\tz" + "b")

  m2(join "q\"r\\s\nt") = pair
  report("all four ", m2, "a" + "q\"r\\s\nt" + "b")

  print "-- the reported case: split on a real newline"
  ' The DOGFOOD entry that started this. Splitting on "\n" must find the three
  ' lines, not treat the separator as two literal characters and find one.
  text = "one" + "\n" + "two" + "\n" + "three"
  s1(split "\n") = text
  print "clause split count=" + count(s1)
  sep = "\n"
  s2(split sep) = text
  print "variable split count=" + count(s2)
  print "agree=" + (count(s1) = count(s2))
  print "first=" + s1[0] + " last=" + s1[2]

  print "-- a separator that is itself a quote or a backslash"
  qtext = "a" + "\"" + "b"
  s3(split "\"") = qtext
  print "quote sep count=" + count(s3)
  btext = "a" + "\\" + "b"
  s4(split "\\") = btext
  print "backslash sep count=" + count(s4)

  print "-- escapes still work in the trailing position and alone"
  e1(join "\n\n") = pair
  report("two nl   ", e1, "a" + "\n\n" + "b")
end program
