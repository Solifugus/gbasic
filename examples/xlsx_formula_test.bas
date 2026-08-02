' xlsx Stage 3b — the formula evaluator and its oracle (docs/xlsx_design.md §13.D/G).
'
' WHY THIS EXISTS IN THIS ORDER. Measured on the Enron corpus, four of the nine
' most-used "functions" are arithmetic operators, so the expression evaluator —
' precedence, references, ranges — carries the largest single share of real
' usage before any function library exists.
'
' THE ORACLE. An xlsx stores both the formula and Excel's cached result for
' every formula cell, so `xlsx.check` can evaluate each one and compare. It
' needs NO dependency graph: every input cell already carries a cached value, so
' each formula is checkable in isolation. The graph is only required once
' something is changed.
'
' THE ORACLE'S LIMIT, stated plainly: this fixture's cached values were
' hand-written (tools/make_xlsx_fixture.py), so `check` here measures
' self-consistency, NOT conformance to Excel. It becomes a true oracle the
' moment it is pointed at a workbook Excel actually wrote — which is the whole
' reason the call exists as a public verb rather than a test-only helper.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "== evaluate a single formula against the sheet's cached values =="
  print "Ledger B5  formula = " + xlsx.cell(wb, "Ledger", "B5").formula
  print "Ledger B5  computed= " + xlsx.evaluate(wb, "Ledger", "B5")
  print "Ledger B5  cached  = " + xlsx.cell(wb, "Ledger", "B5").value

  print ""
  print "== the oracle over a whole sheet =="
  r = xlsx.check(wb, "Formulas")
  print "agree            = " + r.agree
  print "disagree         = " + r.disagree
  print "volatile skipped = " + r.volatile_skipped
  print "unsupported      = " + r.unsupported

  print ""
  print "anything not agreeing is reported, never silently counted:"
  for each n in r.notes
    print "  " + n.verdict + "  " + n.ref + "  " + n.formula
    print "      computed=" + n.computed + "  cached=" + n.cached
  end for

  print ""
  print "== what each formula exercises =="
  ' Spot-checks of the cases most likely to break quietly, evaluated directly
  ' rather than only counted above.
  print "precedence   A1+A2*A3      = " + xlsx.evaluate(wb, "Formulas", "B1")
  print "parens      (A1+A2)*A3     = " + xlsx.evaluate(wb, "Formulas", "B2")
  print "div by zero  A1/0          = " + xlsx.evaluate(wb, "Formulas", "B4")
  print "exponent     2^10          = " + xlsx.evaluate(wb, "Formulas", "B5")
  print "unary minus -A2+1          = " + xlsx.evaluate(wb, "Formulas", "B6")
  print "percent      50%*A1        = " + xlsx.evaluate(wb, "Formulas", "B7")
  print "concat      \"x\"&A1         = " + xlsx.evaluate(wb, "Formulas", "B8")
  print "comparison   A1>A2         = " + xlsx.evaluate(wb, "Formulas", "B9")
  print "IF                         = " + xlsx.evaluate(wb, "Formulas", "B10")
  print "ROUND(2.5,0) half-away     = " + xlsx.evaluate(wb, "Formulas", "B11")
  print "SUM range                  = " + xlsx.evaluate(wb, "Formulas", "B12")
  print "AVERAGE range              = " + xlsx.evaluate(wb, "Formulas", "B13")
  ' COUNT counts NUMBERS only: A4 holds the text "7" and must not be counted.
  print "COUNT skips text           = " + xlsx.evaluate(wb, "Formulas", "B16")
  print "IFERROR catches            = " + xlsx.evaluate(wb, "Formulas", "B17")
end program
