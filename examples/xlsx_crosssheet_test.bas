' xlsx -- CROSS-SHEET REFERENCES (docs/xlsx_design.md §13.L).
'
' Measured over the Enron corpus this was the single largest cause of
' disagreement: 3.09M cells, 54% of all of them. The evaluator held one sheet's
' snapshot and could not see another, so every 'Rate Table'!B2 was #VALUE!.
'
' The three populations, and how each is handled:
'   42%  quoted sheet name   'Nymex hist.'!A:B   -- quoting is required
'                                                   whenever the name has a
'                                                   space, hence the majority
'   30%  plain               Data!$A$1
'   28%  EXTERNAL            [4]CurveFetch!$D$8  -- a DIFFERENT WORKBOOK, not
'                                                   in front of us. Reported as
'                                                   unavailable, never guessed:
'                                                   Excel caches a copy, but
'                                                   presenting a stale value
'                                                   from a file we do not have
'                                                   as current is exactly the
'                                                   confident wrong number this
'                                                   module refuses.
'
' The overwhelming consumer is VLOOKUP, which is why it and its relatives are
' here too rather than in a separate test.
'
' WHERE THE EXPECTED VALUES COME FROM. The sheet XML was hand-written so these
' exact shapes are guaranteed present, but with NO cached values; LibreOffice
' then had to compute them to convert the file. So the shapes are ours and the
' answers are an independent implementation's. Same caveat as modern.xlsx:
' LibreOffice is not Excel, so agreement is strong evidence, not proof -- and
' the numbers are small enough to check by eye.
'
' The table on 'Rate Table' is East 10, North 20, South 30, West 40.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/crosssheet.xlsx")

  print "== the referenced sheet =="
  for each r in [1, 2, 3, 4]
    print "  A" + r + "=" + xlsx.cell(wb, "Rate Table", "A" + r).value + "  B" + r + "=" + xlsx.cell(wb, "Rate Table", "B" + r).value
  end for

  print ""
  print "== every cross-sheet formula, computed by us =="
  for each c in xlsx.cells(wb, "Calc")
    if is_unknown(c.formula) then
      continue
    end if
    print "  " + c.formula
    print "      = " + xlsx.evaluate(wb, "Calc", c.ref)
  end for

  print ""
  print "== compared against the values LibreOffice cached =="
  ' The assertion. A golden records whatever we produce; this compares against
  ' another implementation's answer for the same sheet.
  r = xlsx.check(wb, "Calc")
  print "agree       = " + r.agree
  print "disagree    = " + r.disagree
  print "unsupported = " + r.unsupported
end program
