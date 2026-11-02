' Recipe 6 — All of it, or none of it.
'
' `begin` / `commit` / `rollback` are spelled exactly as `sqlite`'s and `pg`'s
' are, so code that moves between backends does not change. Underneath, ODBC
' has no BEGIN statement: `begin` turns the connection's autocommit off and
' the other two turn it back on.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  db.exec("drop table if exists accounts")
  db.exec("create table accounts (name varchar(20), balance integer)")
  db.exec("insert into accounts values ('alice', 100)")
  db.exec("insert into accounts values ('bob', 0)")

  ' A transfer is two writes that must both happen or neither.
  db.begin()
  db.exec("update accounts set balance = balance - ? where name = ?", [40, "alice"])
  db.exec("update accounts set balance = balance + ? where name = ?", [40, "bob"])
  db.commit()
  print "after commit:   " + balances(db)

  ' The same pair, abandoned. Nothing survives -- not even the first write,
  ' which is the point.
  db.begin()
  db.exec("update accounts set balance = balance - ? where name = ?", [999, "alice"])
  db.exec("update accounts set balance = balance + ? where name = ?", [999, "bob"])
  db.rollback()
  print "after rollback: " + balances(db)

  ' Autocommit is back on after either, so an ordinary write needs no
  ' ceremony and is durable on its own.
  db.exec("update accounts set balance = ? where name = ?", [7, "bob"])
  print "plain write:    " + balances(db)

  db.close()
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program

function balances(db)
  parts = []
  for each row in db.query("select name, balance from accounts order by name")
    append(parts, row.name + "=" + string(row.balance))
  next
  return join(parts, " ")
end function
