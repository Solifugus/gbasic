' grid.bas -- ARI-for-grids: a messy worksheet turned into clean frames.
'
' Layer 2 of docs/xlsx_design.md §5. Reading a workbook is done; this is the
' layer that copes with real sheets being IRREGULAR. Every irregularity in the
' fixture is one the design names as the reason this layer exists:
'
'     row 1-2   a title and a run date, so row 1 is NOT the header
'     row 3     blank
'     row 4-5   a TWO-ROW header, the parent "Q1" written only above the first
'               of its children -- how Excel stores a merged cell
'     row 6-11  data with a SUBTOTAL interleaved and a TOTAL at the end
'     row 12    blank
'     row 13    a trailing NOTE a "read to the bottom" reader swallows as data
'     row 15+   a SECOND table, different width, its own header
'     column D  empty: a spacer column inside the first table's span
'
' THE RULE: automatic when safe, spec when not, and it tells you which. The
' correct answer here is deliberately NOT what a naive top-left-to-bottom-right
' reader produces, so getting it wrong is loud rather than plausible.

program main(args)
  load grid
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  g = grid.of(wb, "Report")

  print "== the sheet's structure, as blank rows reveal it =="
  ' A blank row is the single most reliable structural signal a sheet gives.
  for each b in grid.blocks(g)
    print "  rows " + b.first_row + ".." + b.last_row
  end for

  print ""
  print "== the automatic path: a guess, with its confidence and reasons =="
  ' Never a silent commitment. The messy table comes back LOW confidence with
  ' the two things wrong with the guess named, which is the point.
  for each t in grid.tables(g)
    line = "  rows " + t.first_row + ".." + t.last_row + "  confidence " + t.confidence
    if is_unknown(t.header_row) then
      line = line + "  (no header)"
    else
      line = line + "  header row " + t.header_row
    end if
    print line
    for each n in t.notes
      print "      ! " + n
    end for
    if not is_unknown(t.frame) then
      print "      columns: " + join(frame.columns(t.frame), " | ")
    end if
  end for

  print ""
  print "== the spec path: exactly what it is told =="
  ' header_rows: 2 joins the parent level to each child, carrying "Q1" across
  ' the blank cell Excel leaves for a merged span -- without which two columns
  ' would both be called "Units". drop_totals removes the subtotal AND the
  ' grand total, which a naive read would add to the data and double-count.
  r = grid.extract(g, { header_row: 4, header_rows: 2, drop_totals: true })
  print "ok = " + r.ok + "   data rows " + r.first_row + ".." + r.last_row
  print "columns: " + join(frame.columns(r.frame), " | ")
  print "row count: " + frame.shape(r.frame)[0] + "   (6 data rows minus 2 totals)"
  for each row in frame.to_rows(r.frame)
    print "  " + row["Region"] + "  Q1 " + row["Q1 Units"] + "/" + row["Q1 Value"] + "   Q2 " + row["Q2 Units"] + "/" + row["Q2 Value"]
  end for

  print ""
  print "== the totals are gone from the data, so the sum is the data's own =="
  ' The check that matters: summing the extracted column must give the figure
  ' the sheet's own TOTAL row claims. If a subtotal had been absorbed as data
  ' the sum would be too big -- and still look like a number.
  print "sum of Q1 Units   = " + sum(r.frame["Q1 Units"])
  print "sheet's TOTAL row = " + grid.at(g, 11, 1)

  print ""
  print "== anchoring by content, not row number =="
  ' `starts` finds the second table by what it says, so inserting rows above it
  ' cannot break the spec -- the ARI idea, on a grid.
  a = grid.extract(g, { starts: "Adjustments", header_row: 16 })
  print "second table columns: " + join(frame.columns(a.frame), " | ")
  print "second table rows:    " + frame.shape(a.frame)[0]

  print ""
  print "== a clean sheet needs no spec at all =="
  cg = grid.of(wb, "Clean")
  for each t in grid.tables(cg)
    print "  confidence " + t.confidence + "  columns: " + join(frame.columns(t.frame), " | ")
  end for

  print ""
  print "== a spec that matches nothing REPORTS, it does not guess =="
  bad = grid.extract(g, { starts: "No Such Section" })
  print "ok = " + bad.ok + "   message = " + bad.message
end program
