' Recipe 5 — What writing refuses, and why each refusal is a feature.
'
' Every refusal below exists to stop a SILENT wrong answer. Catching them needs
' `on error resume next`, then read `error.message` and call `error.clear()`
' before the next attempt.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  on error resume next

  ' 1. Overwriting a FORMULA cell. The cell holds a formula and Excel's cached
  '    value. Writing a literal over it would leave the formula in place, so
  '    Excel would silently revert your edit on its next recalculation. Change
  '    the inputs and recalc instead (recipe 8).
  xlsx.set(wb, "Ledger", "B5", 1)
  print "overwrite a formula cell: " + error.message
  error.clear()

  ' 2. Creating a cell that does not exist. The sheet is sparse, and inventing
  '    a cell means guessing its style and its place in the row order, so a new
  '    cell is refused rather than placed wrongly.
  xlsx.set(wb, "Ledger", "Z99", 1)
  print "create a new cell:        " + error.message
  error.clear()

  ' 3. A sheet that is not there. Note this is a genuine miss -- a name that
  '    IS in the workbook but has no worksheet behind it (a macro sheet) reads
  '    as empty rather than erroring.
  xlsx.cells(wb, "No Such Sheet")
  print "unknown sheet:            " + error.message
  error.clear()
end program
