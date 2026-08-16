' Recipe 2 — Cells are sparse, and a missing cell is ABSENT, not blank.
'
' `xlsx.cells` returns only the cells the sheet actually stores. A spreadsheet
' with 40 rows of data in column A does not carry 40 empty cells in column B,
' so DO NOT loop rows x columns and expect a hit every time -- loop the cells
' you were given, or ask for one by reference and test it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  cells = xlsx.cells(wb, "Ledger")
  print "stored cells: " + count(cells)
  print ""

  for each c in cells
    line = "  " + c.ref + " kind=" + c.kind
    if not is_unknown(c.formula) then
      line = line + " formula=" + c.formula
    end if
    print line + " value=" + string(c.value)
  end for

  ' `kind` is how you tell a number from text from a boolean from an Excel
  ' error. Note "error" is its own kind: #DIV/0! is a VALUE the sheet holds,
  ' not a failure of the read.
  print ""
  print "the sheet has no row 4 and no column C, so those cells are simply not there:"
  print "  C2 present = " + (not is_unknown(xlsx.cell(wb, "Ledger", "C2")))
  print "  A4 present = " + (not is_unknown(xlsx.cell(wb, "Ledger", "A4")))
end program
