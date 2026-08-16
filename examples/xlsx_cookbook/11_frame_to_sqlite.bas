' Recipe 11 — Load a frame into SQLite and query it.
'
' The payoff of the whole pipeline: four incompatible spreadsheets become one
' table a query can answer questions about. Needs sqlite in the build.

program main(args)
  load grid
  load consolidate
  load dbframe
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")
  sources = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    append(sources, { name: s, frame: r.frame })
  end for
  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }
  merged = consolidate.merge(sources, spec)

  ' A spreadsheet heading is not a SQL identifier. The mapping is RETURNED so
  ' you can see it -- a quiet rename is how a report ends up joined on the
  ' wrong column. An unsafe name is refused, not escaped.
  print "headings become identifiers:"
  for each h in ["Q1 Units", "Rate (%)", "Loan #"]
    print "  " + h + " -> " + dbframe.to_identifier(h)
  end for

  db = sqlite.connect(":memory:")

  ' Column TYPE is decided from EVERY value, never from the first one: all
  ' whole -> INTEGER, any fractional -> REAL, mixed -> TEXT, unknown -> NULL.
  ' Deciding from the first is how "n/a" becomes 0, and a zero that should
  ' have been a gap changes a total.
  out = dbframe.to_table(merged.frame, db, "loans", { replace: true })
  print ""
  print "loaded ok = " + out.ok + "  rows = " + out.rows
  print out.sql

  print ""
  for each row in sqlite.query(db, "select tape, count(*) n from loans group by tape order by tape", [])
    print "  " + row.tape + "  loans " + row.n
  end for

  ' Assert exactness by COMPARING, not by printing: the point of the pipeline
  ' is that the cents survive it.
  t = sqlite.query(db, "select sum(balance) s from loans", [])[0].s
  print ""
  print "total balance = " + t
  print "exact         = " + (t = 265550.75)
end program
