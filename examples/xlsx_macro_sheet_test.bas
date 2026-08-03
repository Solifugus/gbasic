' xlsx -- a sheet that exists but has NO WORKSHEET PART.
'
' Excel writes VBA module and macro sheets as
'     <sheet name="Module1" state="veryHidden" r:id=""/>
' so the sheet is really in the workbook while nothing backs it. The reader used
' to list such a sheet from xlsx.sheets and then raise "no such sheet" from
' xlsx.cells -- a contradiction, and a false statement: the sheet is right
' there. It broke the one loop every caller writes, over the sheet list.
'
' Measured, not guessed: over the 15,871-workbook Enron corpus, 400 files (2.5%)
' carry such a sheet, and all 400 scan failures were this one case. The reader
' had no other defect in the whole corpus.
'
' A partless sheet now READS as the empty sheet it is. Writing to it is still
' refused -- there is nowhere to put the cell -- but with the real reason
' (tests/run_xlsx.sh pins that message).

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/macro_sheet.xlsx")

  print "== the sheet list includes the partless sheets =="
  for each s in xlsx.sheets(wb)
    print "  [" + s + "]"
  end for

  print ""
  print "== the natural loop completes instead of raising =="
  ' The partless sheets sit BETWEEN the two real ones, so a loop that aborted
  ' on them would also lose "After" -- the count below proves it did not.
  total = 0
  for each s in xlsx.sheets(wb)
    n = count(xlsx.cells(wb, s))
    print "  " + s + " -> " + n + " cells"
    total = total + n
  end for
  print "  total cells across every sheet = " + total

  print ""
  print "== a partless sheet answers as an empty sheet =="
  print "cells      = " + count(xlsx.cells(wb, "Module1"))
  print "cell A1    = " + xlsx.cell(wb, "Module1", "A1")
  print "evaluate   = " + xlsx.evaluate(wb, "Module1", "A1")
  d = xlsx.dims(wb, "Module1")
  print "dims.cells = " + d.cells
  c = xlsx.check(wb, "Module1")
  print "check      = agree " + c.agree + ", disagree " + c.disagree
  r = xlsx.recalc(wb, "Module1")
  print "recalc     = evaluated " + r.evaluated + ", changed " + r.changed

  print ""
  print "== the trailing space in the name is significant =="
  ' Seven corpus workbooks name the sheet "VBACode " with a trailing space. A
  ' reader that trimmed names would pass on Module1 and still fail here.
  print "cells in [VBACode ] = " + count(xlsx.cells(wb, "VBACode "))

  print ""
  ' That a genuinely absent name STILL RAISES is what keeps this from being
  ' blanket leniency, but a raise cannot be caught here -- tests/run_xlsx.sh
  ' pins it, and both messages, as negative cases.
  print "== a name that is not in the workbook at all =="
  print "sheet [Nope] present = " + contains(xlsx.sheets(wb), "Nope")

  print ""
  print "== the real sheets are unaffected =="
  print "Data B1 formula  = " + xlsx.cell(wb, "Data", "B1").formula
  print "Data B1 computed = " + xlsx.evaluate(wb, "Data", "B1")
  print "After A1         = " + xlsx.cell(wb, "After", "A1").value
end program
