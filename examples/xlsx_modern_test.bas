' xlsx -- POST-2001 capabilities (docs/xlsx_design.md §13.K).
'
' WHY A SEPARATE FIXTURE. The Enron corpus is 2001 and cannot contain a single
' function added since: no SUMIFS, no IFERROR, no XLOOKUP, no TEXTJOIN. Every
' measurement in §13.I/J is therefore about the DURABLE CORE only. This covers
' the other half, and nothing derived from that corpus can.
'
' WHERE THE EXPECTED VALUES COME FROM. LibreOffice computed them, and they are
' the cached values inside the fixture -- so `xlsx.check` at the end is a real
' comparison against another implementation, not a restatement of our own
' output. Hand-written values, which basic.xlsx has to use, would only prove
' self-consistency: misunderstand SUMIFS and you write the wrong expectation
' and the test agrees with you.
'
' The honest limit: LibreOffice is not Excel. Agreement is strong evidence, not
' proof. Strength of evidence runs
'     Excel-authored (corpus) > LibreOffice-authored (this) > hand-written.
' The arithmetic below is small enough to check by eye, which is the third
' independent leg -- e.g. East is 1200 + 1500 + 950, so SUMIFS must be 3650.
'
' THE STRUCTURAL FINDING, which matters more than any single function: a
' function newer than the original ECMA-376 list is not stored under its own
' name. Excel writes _xlfn.XLOOKUP, _xlfn._xlws.SORT, and _xlpm.x for a LET
' parameter. Until those prefixes are stripped, every modern function is an
' unknown name no matter how well it is implemented -- while _xll. (add-ins)
' and _xludf. (VBA) must NOT be stripped, because those really are unevaluable.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/modern.xlsx")
  sheet = xlsx.sheets(wb)[0]

  print "== the data the criteria functions run over =="
  ' East: 1200, 1500, 950   West: 800, 400   North: 2200
  for each r in [2, 3, 4, 5, 6, 7]
    ' No line continuation in gBASIC, so the row is assembled in steps.
    line = "  row " + r
    line = line + "  " + xlsx.cell(wb, sheet, "A" + r).value
    line = line + "  " + xlsx.cell(wb, sheet, "B" + r).value
    line = line + "  " + xlsx.cell(wb, sheet, "C" + r).value
    print line
  end for

  print ""
  print "== every formula, computed by us =="
  ' Column E is a label, column F the formula. Printing the label makes the
  ' golden readable as a table of claims rather than a list of numbers.
  for each c in xlsx.cells(wb, sheet)
    if is_unknown(c.formula) then
      continue
    end if
    if not starts_with(c.ref, "F") then
      continue
    end if
    row = mid(c.ref, 1, len(c.ref) - 1)
    label = xlsx.cell(wb, sheet, "E" + row).value
    if is_unknown(label) then
      label = ""
    end if
    print "  " + label + " = " + xlsx.evaluate(wb, sheet, c.ref)
  end for

  print ""
  print "== the on-disk spelling is prefixed; the evaluator sees through it =="
  ' Proof the prefix handling is real and not a coincidence of naming.
  for each ref in ["F19", "F26", "F41"]
    print "  " + ref + " stored as: " + xlsx.cell(wb, sheet, ref).formula
  end for

  print ""
  print "== compared against the values LibreOffice cached in the file =="
  ' The assertion. A golden alone would happily record a regression as the new
  ' expected output; this compares against another implementation's answer.
  r = xlsx.check(wb, sheet)
  print "agree       = " + r.agree
  print "disagree    = " + r.disagree
  print "unsupported = " + r.unsupported
  for each note in r.notes
    print "  " + note.verdict + "  " + note.ref + "  " + note.formula
  end for
end program
