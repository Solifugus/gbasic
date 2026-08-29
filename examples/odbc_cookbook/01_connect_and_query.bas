' Recipe 1 — Connect, ask a question, close.
'
' One module reaches every database with an ODBC driver installed. What
' changes between SQL Server, MySQL, Oracle and SQLite is the CONNECTION
' STRING and the SQL dialect; nothing in this file's shape changes at all.

program main(args)
  load odbc

  ' Two ways to name a database. A DSN is a named entry in the machine's
  ' odbc.ini -- the connection details live there, not in your source:
  '     odbc.connect("DSN=warehouse;UID=app;PWD=secret")
  ' Or name the driver and give it its own options directly:
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists staff")
  odbc.exec(db, "create table staff (id integer, name varchar(40), team varchar(20))")
  odbc.exec(db, "insert into staff values (1, 'Ada', 'compilers')")
  odbc.exec(db, "insert into staff values (2, 'Grace', 'runtime')")

  ' A query returns an ARRAY OF RECORDS. Each record's fields are the result
  ' columns, named exactly as the SQL named them.
  rows = odbc.query(db, "select id, name, team from staff order by id")

  print "rows: " + string(count(rows))
  for each row in rows
    print string(row.id) + "  " + row.name + " (" + row.team + ")"
  next

  ' Arrays are 0-based, so the first row is rows[0].
  print ""
  print "first name: " + rows[0].name
  print "field names: " + string(keys(rows[0]))

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
