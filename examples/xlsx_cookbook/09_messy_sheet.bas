' Recipe 9 — A messy sheet: let it guess, but read the confidence.
'
' Real reports have a title above the table, a two-row header, subtotals mixed
' into the data, and a note underneath. `grid.tables` guesses where the tables
' are and returns a CONFIDENCE plus the reasons it is unsure. A detector that
' were confident about a sheet like this would be worse than none.

program main(args)
  load grid
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  g = grid.of(wb, "Report")

  ' A blank row is the most reliable structural signal a sheet gives.
  print "blocks the blank rows reveal:"
  for each b in grid.blocks(g)
    print "  rows " + b.first_row + ".." + b.last_row
  end for

  print ""
  print "the automatic guess, with its reasons:"
  for each t in grid.tables(g)
    line = "  rows " + t.first_row + ".." + t.last_row + " confidence " + t.confidence
    if is_unknown(t.header_row) then
      line = line + " (no header)"
    else
      line = line + " header row " + t.header_row
    end if
    print line
    for each n in t.notes
      print "      ! " + n
    end for
  end for

  ' When the guess is low-confidence, STOP GUESSING and write a spec. This one
  ' says: header starts at row 4 and spans 2 rows (Excel writes a merged parent
  ' only above the first child, so joining the levels is what stops two columns
  ' both being called "Units"), and drop the subtotal and grand-total rows,
  ' which a naive read would add to the data and double-count.
  print ""
  r = grid.extract(g, { header_row: 4, header_rows: 2, drop_totals: true })
  print "spec: ok=" + r.ok + " rows " + r.first_row + ".." + r.last_row
  print "columns: " + join(frame.columns(r.frame), " | ")

  ' The assertion that matters is ARITHMETIC, not a printed table: the extracted
  ' column must sum to the figure the SHEET'S OWN total row claims. If a
  ' subtotal had been absorbed as data the sum would be too big -- and would
  ' still look like a perfectly ordinary number.
  print ""
  print "sum of extracted Q1 Units = " + sum(r.frame["Q1 Units"])
  print "the sheet's own TOTAL row = " + grid.at(g, 11, 1)
end program
