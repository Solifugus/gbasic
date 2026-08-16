' Recipe 1 — Open a workbook and see what is in it.
'
' `xlsx.open` needs zlib and libxml2 in the build. It returns a HANDLE, not a
' copy: passing it around costs nothing and every call reads the same workbook.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  print "sheets: " + join(xlsx.sheets(wb), ", ")

  ' `dims` reports the USED range of a sheet, not a fixed grid.
  '
  ' MIND THE BASES, they differ: rows are 1-BASED (row 1 is row 1, as Excel
  ' shows it) and columns are 0-BASED (column A is 0). So the range below is
  ' rows 1..7 and columns A..D.
  d = xlsx.dims(wb, "Ledger")
  print "Ledger rows " + d.first_row + "-" + d.last_row + ", cols " + d.first_col + "-" + d.last_col

  ' A workbook is a tree of ZIP parts. The reader keeps ALL of them, including
  ' the ones it does not understand -- that is what makes editing and saving an
  ' existing workbook possible instead of generating a new one.
  print ""
  print "parts (modelled = something interpreted it):"
  for each p in xlsx.parts(wb)
    print "  " + p.name + " bytes=" + p.bytes + " modelled=" + p.modelled
  end for
end program
