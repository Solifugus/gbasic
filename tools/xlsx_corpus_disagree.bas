' Classify a workbook's DISAGREEING formula cells, one line each, for
' tools/xlsx_corpus_disagree.sh to rank across a corpus.
'
' Ranking disagreements by function name (what the blocker scanner does for
' UNSUPPORTED cells) is the wrong instrument here: a cell disagrees because of
' something the whole formula did, and its leading function is rarely the cause.
' What separates the causes is the SHAPE of the mismatch -- whether we produced
' an error where Excel produced a number, or a number that differs, or the wrong
' TYPE entirely. Each of those is a different defect with a different fix, and
' they are invisible to a name count.
'
'   SHAPE <computed-kind>-><cached-kind>          the mismatch class
'   ERRC  <our-error>-><their-value-kind>         which error we invent
'   FUNC  <leading-function>                      secondary, for context only
'
' `xlsx.check` compares numbers with a 1e-9 relative tolerance, so nothing here
' is float noise -- a NUM->NUM line is a genuinely different answer.
program main(args)
  if count(args) < 1 then
    print to error "usage: xlsx_corpus_disagree.bas <file.xlsx>"
    return nothing
  end if
  wb = xlsx.open(args[0])

  for each s in xlsx.sheets(wb)
    r = xlsx.check(wb, s)
    for each n in r.notes
      if n.verdict = "disagree" then
        got = _kind_of(n.computed)
        want = _kind_of(n.cached)
        ' The FILE is on every line so the report can count DISTINCT WORKBOOKS
        ' as well as cells. Cell counts alone are badly skewed here: one formula
        ' template filled down tens of thousands of rows outvotes a thousand
        ' separate workbooks, which is exactly how the 1900-serial fix came to
        ' be estimated at ~198,000 cells when it was worth 25,320 (§13.X).
        print "SHAPE " + got + "->" + want + "\t" + args[0]
        if got = "err" then
          print "ERRC " + n.computed + "->" + want + "\t" + args[0]
        end if
        print "FUNC " + _first_function(n.formula) + "\t" + args[0]
      end if
    end for
  end for
end program

' The kind of a rendered value, from its text. `computed` and `cached` arrive as
' strings, so this reads the shape back out: an Excel error always begins with
' '#', an empty rendering is blank, anything `number` accepts is numeric, and
' the rest is text.
function _kind_of(t)
  if t = "" then
    return "empty"
  end if
  if left(t, 1) = "#" then
    return "err"
  end if
  if t = "TRUE" or t = "FALSE" or t = "true" or t = "false" then
    return "bool"
  end if
  m = match(t, regex("^-?[0-9]+(\\.[0-9]+)?([eE][-+]?[0-9]+)?$"))
  if is_unknown(m) then
    return "text"
  end if
  return "num"
end function

function _first_function(f)
  m = match(f, regex("[A-Za-z_][A-Za-z0-9_.]*\\("))
  if is_unknown(m) then
    return "EXPR"
  end if
  return upper(left(m.text, m.length - 1))
end function
