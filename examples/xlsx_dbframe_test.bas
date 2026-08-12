' dbframe.bas -- L4's easy half: a frame becomes a database table.
'
' docs/xlsx_design.md §7 sequences this before the formula compiler and calls
' it valuable on its own, which it is: L2 extracts frames from messy sheets and
' L3 consolidates them, and the remaining step for any real workflow is getting
' the result somewhere it can be queried and joined.
'
' This test runs the WHOLE PIPELINE end to end on the tapes fixture --
' xlsx -> grid (L2) -> consolidate (L3) -> sqlite (L4a) -- and then asks the
' database questions, which is the point of the exercise.

program main(args)
  load grid
  load consolidate
  load dbframe
  load frame

  print "== types are decided from EVERY value, not the first =="
  ' Taking the first value's type and coercing the rest is how "n/a" becomes 0
  ' -- and a zero that should have been a gap changes a total.
  print "  all whole numbers      -> " + dbframe.column_type([1, 2, 3])
  print "  one fractional         -> " + dbframe.column_type([1, 2, 3.5])
  print "  numbers then text      -> " + dbframe.column_type([1, 2, "n/a"])
  print "  text then numbers      -> " + dbframe.column_type(["n/a", 1, 2])
  print "  all unknown            -> " + dbframe.column_type([unknown, unknown])
  print "  numbers with a gap     -> " + dbframe.column_type([1, unknown, 3])

  print ""
  print "== headings become identifiers, visibly =="
  ' Returned rather than applied silently: a quiet rename is how a report ends
  ' up joined on the wrong column.
  for each h in ["Q1 Units", "Rate (%)", "Loan #", "  spaced  ", "2024 total"]
    print "  " + h + " -> " + dbframe.to_identifier(h)
  end for

  print ""
  print "== the pipeline, end to end =="
  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")
  sources = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    append(sources, { name: s, frame: r.frame })
  end for
  spec = { columns: {
             loan_id: { names: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { names: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { names: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }
  merged = consolidate.merge(sources, spec)

  db = sqlite.connect(":memory:")
  out = dbframe.to_table(merged.frame, db, "loans", { replace: true })
  print "loaded ok = " + out.ok + "   rows = " + out.rows
  print out.sql

  print ""
  print "== now the database can answer questions about the pool =="
  ' The payoff: four incompatible spreadsheets, one query.
  for each row in sqlite.query(db, "select tape, count(*) n from loans group by tape order by tape", [])
    print "  " + row.tape + "  loans " + row.n
  end for
  ' Compared rather than printed: `print` renders 265550.75 as 265551, so a
  ' printed total would hide exactly the cents this is meant to preserve.
  t = sqlite.query(db, "select sum(balance) s from loans", [])[0].s
  print "  total balance is exact = " + (t = 265550.75)
  hi = sqlite.query(db, "select loan_id from loans order by rate desc limit 1", [])[0].loan_id
  print "  highest rate is " + hi + " (all rates on one scale, so this is meaningful)"

  print ""
  print "== values are BOUND, so data cannot become SQL =="
  ' A loan id that is a SQL injection attempt must come back out as itself and
  ' leave the table standing. Asserted by round-tripping it and then proving
  ' the table still exists, because "we use bound parameters" is a claim only a
  ' test can make true.
  nasty = "'); drop table loans;--"
  odd = frame.from_rows([{ id: nasty, amt: 1 }, { id: "O'Brien", amt: 2 }])
  r2 = dbframe.to_table(odd, db, "odd", { replace: true })
  back = sqlite.query(db, "select id from odd order by amt", [])
  print "  round-tripped intact = " + (back[0].id = nasty)
  print "  apostrophe intact    = " + (back[1].id = "O'Brien")
  print "  loans table survived = " + (sqlite.query(db, "select count(*) c from loans", [])[0].c = 6)

  print ""
  print "== identifiers are validated, and refusals say why =="
  bad = dbframe.to_table(odd, db, "loans; drop table loans", { })
  print "  bad table name: ok=" + bad.ok + "  " + bad.message
  ' Two headings that collapse to the same identifier would silently overwrite
  ' each other, so that is refused too.
  ' Built by assignment because a record LITERAL only accepts identifier keys.
  clash = { }
  clash["Rate (%)"] = [1]
  clash["rate"] = [2]
  bad2 = dbframe.to_table(clash, db, "clash", { })
  print "  colliding columns: ok=" + bad2.ok + "  " + bad2.message

  print ""
  print "== loading twice APPENDS unless replace is asked for =="
  ' No default that silently doubles a total: re-running a load is correct for
  ' a growing pool and wrong for a re-run, so the caller chooses.
  dbframe.to_table(merged.frame, db, "again", { replace: true })
  dbframe.to_table(merged.frame, db, "again", { })
  print "  rows after two loads = " + sqlite.query(db, "select count(*) c from again", [])[0].c
  dbframe.to_table(merged.frame, db, "again", { replace: true })
  print "  rows after replace   = " + sqlite.query(db, "select count(*) c from again", [])[0].c

  sqlite.close(db)
end program
