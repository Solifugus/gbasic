' Recipe 3 — A formula cell carries TWO things, and you usually want both.
'
' An .xlsx stores, for every formula cell, the formula text AND the value Excel
' computed the last time it calculated. gBASIC keeps both. That cached value is
' not clutter: it is the only independent check on our own formula engine, and
' `xlsx.check` (recipe 7) is built entirely out of it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  b5 = xlsx.cell(wb, "Ledger", "B5")
  print "B5 formula      = " + b5.formula
  print "B5 cached value = " + b5.value
  print "B5 kind         = " + b5.kind

  ' `evaluate` ignores the cached value and computes the formula now.
  print "B5 evaluated    = " + xlsx.evaluate(wb, "Ledger", "B5")

  ' A cell with no formula reports `unknown` for it -- test with is_unknown,
  ' do not compare against "".
  b2 = xlsx.cell(wb, "Ledger", "B2")
  print ""
  print "B2 has a formula = " + (not is_unknown(b2.formula))
  print "B2 value         = " + b2.value

  ' Reading a formula does not require the sheet to be recalculated, and
  ' evaluating one does not write anything back. Recipe 8 covers writing
  ' computed values into the workbook.
end program
