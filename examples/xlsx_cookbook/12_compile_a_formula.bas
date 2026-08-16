' Recipe 12 — Compile a column formula instead of evaluating it per cell.
'
' A column formula is ONE transformation applied down a column, so it does not
' need a cell-by-cell walk. `xlsx.to_sql` lowers it to SQL the database runs
' once over the whole table; `xlsx.apply` runs it over an in-memory frame.

program main(args)
  load grid
  load dbframe
  load frame

  ' The mapping says which column letter is which database column. `_row` pins
  ' the formula's own row, so a reference to a FIXED cell above the data is
  ' caught rather than silently compiled to that row's column.
  mapping = { A: "id", B: "balance", C: "rate", D: "term", F1: 0.02, _row: 2 }

  print "what lowers:"
  for each f in ["B2*0.02", "IF(C2>0.05, B2*0.02, 0)", "ROUND(B2*C2, 2)", "SUM(B2:D2)", "B2*$F$1"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.sql
  end for

  ' The refusals are the feature. Every one of these COULD be lowered to
  ' something plausible, and plausible-and-wrong on every row of a column is
  ' the worst outcome available.
  print ""
  print "what is refused, and why:"
  for each f in ["VLOOKUP(A2, X:Y, 2, 0)", "SUMIF(B2:B99, \">0\", C2:C99)", "NOW()", "Other!B2", "MIN(B2)"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.reason
  end for

  ' Run it for real, and check the compiled SQL against the interpreter.
  wb = xlsx.open("examples/fixtures/xlsx/formulacol.xlsx")
  r = grid.extract(grid.of(wb, "Loans"), { header_row: 1 })
  db = sqlite.connect(":memory:")
  dbframe.to_table(r.frame, db, "loans", { replace: true })

  f = xlsx.cell(wb, "Loans", "E2").formula
  c = xlsx.to_sql(f, mapping)
  print ""
  print "column E formula = " + f
  print "compiled         = " + c.sql

  rows = sqlite.query(db, "select id, (" + c.sql + ") v from loans order by id", [])
  agree = 0
  i = 0
  while i < count(rows)
    interp = xlsx.evaluate(wb, "Loans", "E" + (i + 2))
    if abs(number(interp) - number(rows[i].v)) < 0.0000001 then
      agree = agree + 1
    end if
    i = i + 1
  end while
  print "rows where compiled SQL agrees with the interpreter: " + agree + "/" + count(rows)

  ' `xlsx.apply` is the other target: one call, one value per row, no database
  ' and no workbook snapshot per cell.
  vec = xlsx.apply(f, mapping, r.frame)
  print "xlsx.apply gives " + count(vec) + " values, first = " + vec[0]
end program
