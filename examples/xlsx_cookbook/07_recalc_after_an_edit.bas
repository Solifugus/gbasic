' Recipe 7 — Recalculate after changing an input, and mind the ordering.
'
' `xlsx.recalc(wb, sheet)` recalculates one sheet. `xlsx.recalc(wb)` -- with NO
' sheet argument -- orders across the whole workbook, and that is the one you
' almost always want: a formula can depend on a formula on ANOTHER sheet, and
' the per-sheet form will happily read that other sheet's stale cached value.
' Nothing errors; the number is just wrong.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "B5 = SUM(B2:B3), and D7 = B5*2 -- but D7 sits ABOVE B5 in sheet order."
  print "before: B2=" + xlsx.cell(wb, "Ledger", "B2").value + " B5=" + xlsx.cell(wb, "Ledger", "B5").value + " D7=" + xlsx.cell(wb, "Ledger", "D7").value

  ' Change an input.
  xlsx.set(wb, "Ledger", "B2", 1000)

  r = xlsx.recalc(wb, "Ledger")
  print ""
  print "evaluated=" + r.evaluated + " changed=" + r.changed + " circular=" + r.circular
  print "after:  B2=" + xlsx.cell(wb, "Ledger", "B2").value + " B5=" + xlsx.cell(wb, "Ledger", "B5").value + " D7=" + xlsx.cell(wb, "Ledger", "D7").value
  print ""
  print "D7 is 1801.5, not 2302.5 -- it was recomputed from the NEW B5, not the"
  print "stale one, because recalc walks dependencies rather than sheet order."

  ' A circular reference is REPORTED, not iterated toward a fixed point. Healthy
  ' cells on the same sheet still evaluate: one cycle must not sink a sheet.
  c = xlsx.recalc(wb, "Circular")
  print ""
  print "Circular sheet: evaluated=" + c.evaluated + " circular=" + c.circular
end program
