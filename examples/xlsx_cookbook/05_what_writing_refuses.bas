' Recipe 5 — What writing refuses, and why each refusal is a feature.
'
' Every refusal below exists to stop a SILENT wrong answer. Catching them needs
' `on error goto next`, then read `error.message` and call `error.clear()`
' before the next attempt.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  on error goto next

  ' 1. Overwriting a FORMULA cell. The cell holds a formula and Excel's cached
  '    value. Writing a literal over it would leave the formula in place, so
  '    Excel would silently revert your edit on its next recalculation. Change
  '    the inputs and recalc instead (recipe 8).
  wb.set("Ledger", "B5", 1)
  print "overwrite a formula cell: " + error.message
  error.clear()

  ' 2. A value no cell can hold. A cell holds a number and a format, so a
  '    date becomes a serial and a duration a fraction of days -- but a
  '    duration of MONTHS has no length in days, and any number written for it
  '    would be a guess. (A cell that does not exist yet is simply made, in its
  '    place in the row: recipe 13.)
  wb.set("Ledger", "Z99", 2 months)
  print "a value no cell holds:    " + error.message
  error.clear()

  ' 3. A sheet that is not there. Note this is a genuine miss -- a name that
  '    IS in the workbook but has no worksheet behind it (a macro sheet) reads
  '    as empty rather than erroring.
  wb.cells("No Such Sheet")
  print "unknown sheet:            " + error.message
  error.clear()
end program
