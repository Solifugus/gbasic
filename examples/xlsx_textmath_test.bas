' The TEXT and MATH function families, checked against an independent engine.
'
' The fixture's cached values were computed by LibreOffice
' (tools/make_xlsx_textmath_fixture.sh), so `xlsx.check` here compares our
' evaluation against a second implementation rather than restating our own --
' the weakness every hand-written fixture has.
'
' WHY THESE FAMILIES, AND WHY ONLY NOW. The evaluator was built by following the
' Enron corpus, and the corpus was being read through a blind spot: `xlsx.check`
' counted `unsupported` cells without recording WHICH function it refused, so
' the roadmap was ranked by counting `NAME(` tokens in formula text -- the method
' docs/xlsx_design.md §13.J had already shown to be structurally blind, because a
' formula usually holds several functions and only one is the blocker. Once the
' notes carried `blocked_by` and the corpus was re-ranked by the name actually
' refused, the top of the list was not the lookup/aggregate work that was next on
' the roadmap (~14k cells) but FIND at 240,587 blocked cells and LEFT at 207,757.
'
' The load-bearing assertion is `disagree = 0`, not the printed values: a golden
' records whatever we produce AS the expectation, so it cannot by itself tell a
' right answer from a wrong one. The value table below it is there so a change
' to any single function is visible in a diff rather than hidden behind a count.
program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/textmath.xlsx")
  sheet = xlsx.sheets(wb)[0]
  r = xlsx.check(wb, sheet)

  print "formulas judged = " + (r.agree + r.disagree)
  print "disagree        = " + r.disagree
  print "unsupported     = " + r.unsupported

  ' Anything not agreeing is named with both sides, never silently counted.
  for each n in r.notes
    print "  " + n.verdict + "  " + n.ref + "  " + n.formula
    print "      computed=" + n.computed + "  cached=" + n.cached + "  blocked_by=" + n.blocked_by
  end for

  ' Every case by label, so the answers themselves are in the golden. The sheet
  ' carries a label in column D and the formula in column E.
  '
  ' Keyed by cell ref into a record and read back -- which is only a reasonable
  ' way to write this since PLAT-RECIDX (2026-08-13) made a record an actual
  ' map; before that this loop was quadratic in the sheet size.
  by_ref = { }
  for each c in xlsx.cells(wb, sheet)
    by_ref[c.ref] = c.value
  end for

  print ""
  print "every case, by label:"
  row = 2
  while row <= 80
    label = by_ref["D" + row]
    if not is_unknown(label) then
      shown = by_ref["E" + row]
      if is_unknown(shown) then
        shown = "(empty)"
      end if
      print "  " + label + " = " + shown
    end if
    row = row + 1
  end while
end program
