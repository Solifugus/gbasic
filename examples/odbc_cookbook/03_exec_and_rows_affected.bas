' Recipe 3 — `query` asks, `exec` changes.
'
' `odbc.query` returns rows. `odbc.exec` returns a record describing what the
' statement did: `{ command, rows_affected }`. Using the wrong one is refused
' rather than quietly doing half of what you meant.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  r = odbc.exec(db, "drop table if exists jobs")
  r = odbc.exec(db, "create table jobs (id integer, state varchar(20))")
  print r.command

  r = odbc.exec(db, "insert into jobs values (?, ?)", [1, "queued"])
  print r.command + " " + string(r.rows_affected)
  odbc.exec(db, "insert into jobs values (?, ?)", [2, "queued"])
  odbc.exec(db, "insert into jobs values (?, ?)", [3, "done"])

  ' rows_affected is how you tell "I updated the row" from "there was no such
  ' row" -- both are successes, and only this number separates them.
  r = odbc.exec(db, "update jobs set state = ? where state = ?", ["running", "queued"])
  print r.command + " " + string(r.rows_affected)

  r = odbc.exec(db, "update jobs set state = ? where id = ?", ["running", 99])
  print r.command + " " + string(r.rows_affected) + "   (no such row)"

  r = odbc.exec(db, "delete from jobs where state = ?", ["done"])
  print r.command + " " + string(r.rows_affected)

  ' A `select` handed to `exec` is a caller who wrote a query and meant a
  ' command. Discarding the rows silently is how a check becomes a no-op.
  print ""
  on error goto next
  odbc.exec(db, "select id from jobs")
  if error then
    print "refused: " + error.message
    error.clear()
  end if
  on error stop

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
