' Record fields may be named with RESERVED WORDS.
'
' Before this, the keyword namespace reached into the DATA namespace: a record
' could not have a field called `from`, `to`, `next`, `end` or `error`, so a
' data structure got named by the grammar rather than by meaning. The
' consolidation spec in stdlib/consolidate.bas carries `names:`/`kind:` for
' exactly this reason -- `from:`/`as:` would not parse.
'
' It works because a field name sits in a CLOSED CONTEXT: immediately before
' ':' or '=' inside a record literal, nothing but a name is legal, so a keyword
' there cannot be anything else. Measured rather than assumed -- the grammar
' change introduces ZERO new LALR conflicts.
'
' NOT covered here, and deliberately: `rec.next` in DOT position. That is not
' the same problem -- `a.b` is assembled by the LEXER, so admitting keywords
' after a dot is a lexer change rather than a grammar one. Bracket access
' reaches those fields today.

program main(args)
  r = { from: "src", to: "dst", next: 1, end: 2, error: "none",
        for: 3, if: 4, in: 5, not: 6, print: 7, while: 8, function: 9,
        as: "money" }

  print "from  = " + r["from"]
  print "to    = " + r["to"]
  print "next  = " + r["next"]
  print "end   = " + r["end"]
  print "error = " + r["error"]
  print "for   = " + r["for"]
  print "if    = " + r["if"]
  print "not   = " + r["not"]
  print "print = " + r["print"]
  ' `as` needed its token DECLARING as well as the grammar rule: the lexer
  ' emits it for ARI's `as money` conversions, but the parser had no rule at
  ' all, so it failed as an unexpected token rather than a keyword clash.
  print "as    = " + r["as"]

  ' The keyword still works as a keyword in the very next statement, which is
  ' the property that would break if this leaked out of the field-name context.
  i = 0
  while i < 2
    i = i + 1
  end while
  print "keywords still parse as keywords: i = " + i

  ' A field whose name is not an identifier at all still needs bracket
  ' assignment; that is a separate gap and this does not close it.
  q = { }
  q["Rate (%)"] = 5
  print "non-identifier key = " + q["Rate (%)"]
end program
