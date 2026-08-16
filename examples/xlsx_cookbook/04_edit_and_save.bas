' Recipe 4 — Change a cell and save, without damaging the rest of the workbook.
'
' `xlsx.set` writes into the in-memory workbook; `xlsx.save` writes a new file
' and returns the byte count. Saving rebuilds ONLY the parts that changed --
' every other part, including ones gBASIC does not model, is copied through
' byte-for-byte. That is why editing a real workbook is safe here and why a
' library that can only generate new files was not enough.

program main(args)
  out = "examples/tmp_cookbook_edit.xlsx"

  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")
  print "before: B2 = " + xlsx.cell(wb, "Ledger", "B2").value

  ' Types are preserved: a number stays a number, a string a string.
  xlsx.set(wb, "Ledger", "B2", 9999.99)
  xlsx.set(wb, "Ledger", "A1", "Renamed")
  xlsx.set(wb, "Ledger", "B6", false)

  n = xlsx.save(wb, out)
  print "wrote " + n + " bytes"

  ' Re-open the file we just wrote and confirm it round-tripped.
  wb2 = xlsx.open(out)
  print ""
  print "after:  A1 = " + xlsx.cell(wb2, "Ledger", "A1").value
  print "after:  B2 = " + xlsx.cell(wb2, "Ledger", "B2").value
  print "after:  B6 = " + xlsx.cell(wb2, "Ledger", "B6").value

  ' The part nothing models survived the round trip untouched.
  print ""
  print "vendor part still present = " + (not is_unknown(xlsx.part(wb2, "xl/customData/vendor.xml")))
  print "sheets unchanged          = " + join(xlsx.sheets(wb2), ", ")

  ' Saves are byte-deterministic: the ZIP timestamps are fixed rather than
  ' taken from the clock, so saving the same workbook twice gives identical
  ' bytes and file comparison stays meaningful.
  '
  ' Cleanup. `delete` needs a FILE REFERENCE, not a path string -- the `(file)`
  ' modifier is how a string becomes one.
  tmp(file)= out
  if exists(tmp) then
    delete(tmp)
  end if
end program
