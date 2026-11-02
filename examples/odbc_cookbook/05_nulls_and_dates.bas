' Recipe 5 — SQL NULL is `nothing`, and date columns are datetimes.
'
' Two conversions worth knowing before you write a condition that reads
' correctly and answers wrongly.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  db.exec("drop table if exists tickets")
  db.exec("create table tickets (id integer, closed_on date, note varchar(40))")

  opened {date}= "2026-03-05"
  db.exec("insert into tickets values (?, ?, ?)", [1, opened, "shipped"])
  db.exec("insert into tickets values (?, ?, ?)", [2, nothing, nothing])

  rows = db.query("select id, closed_on, note from tickets order by id")

  ' NULL becomes `nothing` -- which is NOT the empty string. Comparing against
  ' "" is the mistake this recipe exists to prevent: it is false for a NULL
  ' and true for a column that really is empty, and those are different facts.
  for each row in rows
    print "ticket " + string(row.id)
    print "  note is nothing : " + string(is_nothing(row.note))
    print "  note = \"\"       : " + string(row.note = "")
  next

  ' A DATE column arrives as a gBASIC datetime, so the date fields and the
  ' date arithmetic work on it directly -- no parsing step.
  print ""
  d = rows[0].closed_on
  print "closed_on type  : " + type(d)
  print "closed_on       : " + string(d)
  print "  year          : " + string(d.year)
  print "  dayname       : " + d.dayname

  ' TIME and TIMESTAMP columns convert the same way. Fractional seconds are
  ' dropped: gBASIC datetimes carry whole seconds.
  db.exec("drop table if exists events")
  db.exec("create table events (at_time timestamp)")
  db.exec("insert into events values (?)", ["2026-03-05 13:45:01"])
  e = db.query("select at_time from events")[0]
  print ""
  print "timestamp -> " + type(e.at_time) + " " + string(e.at_time)

  db.close()
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
