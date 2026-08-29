' Recipe 2 — Never build SQL by pasting values into it.
'
' `?` is a placeholder. The values go in a separate array and the driver binds
' them, so a value can never be read as SQL. This is not a style preference:
' the recipe below demonstrates the difference by running both forms against
' the same data.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists notes")
  odbc.exec(db, "create table notes (id integer, body varchar(60))")
  odbc.exec(db, "insert into notes values (1, 'hello')")
  odbc.exec(db, "insert into notes values (2, 'goodbye')")

  ' A perfectly ordinary-looking search term that happens to be SQL.
  term = "' or '1'='1"
  odbc.exec(db, "insert into notes (id, body) values (?, ?)", [3, term])

  ' PASTED: the term closes the quote and its `or` takes over the condition,
  ' so the query answers about every row in the table.
  pasted = odbc.query(db, "select id from notes where body = '" + term + "'")
  print "pasted matched: " + string(count(pasted)) + " rows"

  ' BOUND: the same characters are a value, so it matches only the row whose
  ' body really is that text.
  bound = odbc.query(db, "select id from notes where body = ?", [term])
  print "bound matched:  " + string(count(bound)) + " rows"
  print "and it is row:  " + string(bound[0].id)

  ' The parameter array's length must match the number of `?` exactly --
  ' a mismatch is refused rather than run with the wrong query.
  print ""
  on error goto next
  odbc.query(db, "select id from notes where id = ?", [1, 2])
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
