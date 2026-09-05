' When a `(` opens a modifier clause, and when it is just a parenthesis
' (PLAT-CLAUSE, options A and F from docs/gbasic_clause_recognition.md).
'
' A modifier clause `x(mod) = v` is recognised by a lookahead over raw source
' (`modifier_lparen_ahead`, src/parser.y). It used to fire on any parenthesised
' expression whose content looked simple and which was followed by a comparison
' operator — so `if (a - b) > 0` was read as a clause and failed to parse, in
' EVERY context, not just after `if`.
'
' Two refinements fix that without touching the grammar:
'
'   A  a clause may only follow a token that can END AN EXPRESSION. A keyword,
'      an operator, `=`, `,`, `(`, `[`, `.` or the start of a statement cannot,
'      so a `(` after any of those is an ordinary parenthesis.
'   F  a DOTTED name is a call, not a clause target. `helper.kind(1)` and
'      `r.m(1)` are calls; the lookahead no longer reads the `(` as a clause
'      just because it could not find `kind` or `m` among this file's functions.
'
' The first half of this file is what those fix. The second half is everything
' that already worked and must keep working — the point of the exercise is that
' the fix costs none of it.

modifier caseless for compare
  return compare(lower(left), operator, lower(right))
end modifier

modifier wrap(a, b) for compare
  return compare(left + a + b, operator, right)
end modifier

function same_file(x)
  return "record"
end function

function ret_compare(a, b)
  ' `return` is a keyword, so the `(` after it cannot open a clause.
  return (a - b) > 0
end function

function two_args(x, y)
  return y
end function

program main(args)
  load clause_probe from "libs/clause_probe.bas"

  a = 5
  b = 2

  print "-- class A: a parenthesised expression followed by a comparison"

  ' after `if`
  if (a - b) > 0 then
    print "if           ok"
  end if

  ' after `while`
  n = 0
  while (a - b) > 99
    n = n + 1
  end while
  print "while        ok"

  ' after `return`, inside a function
  print "return       ok=" + ret_compare(a, b)

  ' after `print`
  print (a - b) > 0

  ' after `=`
  c = (a - b) > 0
  print "assign       ok=" + c

  ' after an arithmetic operator (string `+`, so the assertion stays about
  ' parsing rather than about numeric coercion)
  d = "" + ((a - b) > 0)
  print "operator     ok=" + d

  ' after a comma, in an argument list
  print "comma        ok=" + two_args(1, (a - b) > 0)

  ' after an opening paren
  e = ((a - b) > 0)
  print "open paren   ok=" + e

  ' (The remaining class A context, a parenthesised expression at the START of
  ' a statement, is not a legal statement in gBASIC either way -- there is no
  ' bare-expression statement form. What changed is the error: an honest
  ' "unexpected LPAREN" instead of a mystifying "unexpected MOD_LPAREN". It is
  ' pinned in tests/negative_clause_stmt_start.bas.)

  ' a single identifier in parens -- the shape no content test could ever
  ' separate from a no-argument clause, and the reason option B alone was
  ' rejected. It is the preceding token that settles it.
  if (a) > 0 then
    print "bare ident   ok"
  end if

  print "-- class B: a call the lookahead cannot see"

  ' Library-qualified. `kind` is declared in another file, so the function
  ' check cannot find it; F settles it on the dot instead.
  if clause_probe.kind(1) = "record" then
    print "qualified    ok"
  end if

  ' A function value held in a record field, called through it.
  holder = { m: same_file }
  if holder.m(1) = "record" then
    print "method       ok"
  end if

  ' A call to a function declared in THIS file already worked, via the
  ' lookahead's function check. It must keep working.
  if same_file(1) = "record" then
    print "same file    ok"
  end if

  ' An UNQUALIFIED call to the library's function. Neither A nor F reaches
  ' this: the preceding token is an ordinary identifier and there is no dot.
  ' A clause body always begins with a modifier NAME, though, and a name is
  ' always identifier-shaped -- so a body that starts a number or a string
  ' cannot be one, and these are calls (PLAT-CLAUSE-B).
  if clause_probe.kind(1) = "record" then
    print "unqual num   ok"
  end if
  if clause_probe.kind("q") = "record" then
    print "unqual str   ok"
  end if
  ' A nested call or a parenthesised argument was never affected: an inner `(`
  ' is not accepted in a clause body.
  if clause_probe.kind(clause_probe.one(1)) = "record" then
    print "unqual nest  ok"
  end if
  ' `kind(x)` -- an IDENTIFIER argument -- is NOT fixed and cannot be, because
  ' it is token-for-token identical to `name(caseless) = "joe"` below. Pinned
  ' in tests/negative_clause_residual.bas.

  print "-- clauses that already worked, and must keep working"

  f{file} = "/tmp/gbasic_clause_probe"
  print "file ref     ok=" + is_string(file_name(f))

  t{trimmed} = "  hi  "
  print "trimmed      ok=[" + t + "]"

  p{split ","} = "a,b,c"
  print "split        ok=" + count(p)

  ' A separator containing an escape, which PLAT-DEBT 4 fixed and which must
  ' still decode here.
  q{split "\n"} = "one" + "\n" + "two"
  print "split esc    ok=" + count(q)

  name = "Joe Barnes"
  if name{caseless} = "joe barnes" then
    print "compare      ok"
  end if

  ' An index target: the `(` follows `]`, which CAN end an expression.
  arr = ["  x  "]
  arr[0]{trimmed} = arr[0]
  print "index target ok=[" + arr[0] + "]"

  print "-- the {...} lens form, including multiple arguments"

  if "x"{wrap "L", "T"} = "xLT" then
    print "lens 2 args  ok"
  end if

  ' The argument splitter used to cut on the first comma it saw, including one
  ' INSIDE a string literal, reporting `undefined variable: "L`.
  if "x"{wrap "L,R", "T"} = "xL,RT" then
    print "comma in arg ok"
  end if

  ' An escaped quote inside an argument, past the comma splitter.
  if "x"{wrap "A\"B", "T"} = "xA\"BT" then
    print "quote in arg ok"
  end if
end program
