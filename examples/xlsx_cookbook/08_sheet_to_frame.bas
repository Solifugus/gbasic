' Recipe 8 — Turn a worksheet into a data frame.
'
' A sheet is a grid of cells; a frame is columns with names. `grid.of` wraps a
' sheet, `grid.extract` pulls a table out of it according to a SPEC. Needs
' GBASIC_PATH to find stdlib (grid.bas loads frame.bas).

program main(args)
  load grid
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")

  ' The "Clean" sheet is the easy case: a header row, then data.
  g = grid.of(wb, "Clean")
  r = grid.extract(g, { header_row: 1 })

  print "ok = " + r.ok
  print "columns: " + join(frame.columns(r.frame), " | ")
  print "rows: " + frame.shape(r.frame)[0]
  print ""
  for each row in frame.to_rows(r.frame)
    print "  " + row["Item"] + "  qty " + row["Qty"] + "  @ " + row["Price"]
  end for

  ' Columns come back as real values, not text: Qty is a number here, so it
  ' sums without parsing.
  print ""
  print "total units = " + sum(r.frame["Qty"])

  ' A spec that matches nothing reports ok=false rather than handing back an
  ' empty frame -- "no data" and "I could not find the table" are different
  ' answers and only one of them is a bug in your spec.
  print ""
  miss = grid.extract(g, { starts: "No Such Anchor", header_row: 1 })
  print "spec matching nothing: ok = " + miss.ok
end program
