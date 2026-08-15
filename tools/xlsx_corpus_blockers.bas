' Print what a workbook's UNSUPPORTED formula cells are blocked on, one line
' per cell, for tools/xlsx_corpus_blockers.sh to rank across a corpus.
'
'   BLOCK <function-name>
'   DISAGREE <function-or-EXPR>
'
' Why this and not a token count. SS13.I ranked the roadmap by counting `NAME(`
' occurrences in formula text and SS13.J showed that method is structurally
' blind: a formula usually contains several functions, only one of which is the
' one actually being refused, and the biggest defects then turned out not to be
' missing functions at all. `xlsx.check` knows exactly which name it refused --
' the evaluator fills it in -- and since 2026-08-15 reports it as
' `note.blocked_by`. So this ranks the real blocker per cell rather than a proxy.
'
' Disagreements are emitted too, keyed by the first function name in the formula
' or EXPR when there is none, because a wrong answer and a refused one are
' different problems and should not share a ranking.
program main(args)
  if count(args) < 1 then
    print to error "usage: xlsx_corpus_blockers.bas <file.xlsx>"
    return nothing
  end if
  wb = xlsx.open(args[0])

  for each s in xlsx.sheets(wb)
    r = xlsx.check(wb, s)
    for each n in r.notes
      if n.verdict = "unsupported" then
        who = n.blocked_by
        if who = "" then
          who = "(unnamed)"
        end if
        print "BLOCK " + who
      else
        if n.verdict = "disagree" then
          print "DISAGREE " + _first_function(n.formula)
        end if
      end if
    end for
  end for
end program

' The leading function name in a formula, or EXPR if it is pure operators and
' references. Only used to bucket DISAGREE lines, where no single name is
' authoritative -- unlike BLOCK, which the evaluator names exactly.
function _first_function(f)
  m = match(f, regex("[A-Za-z_][A-Za-z0-9_.]*\\("))
  if is_unknown(m) then
    return "EXPR"
  end if
  return upper(left(m.text, m.length - 1))
end function
