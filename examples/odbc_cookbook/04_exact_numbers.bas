' Recipe 4 — Money and big integers come back as STRINGS, on purpose.
'
' THE MOST IMPORTANT RECIPE HERE. A gBASIC number is a double. It holds about
' fifteen significant digits, so `DECIMAL(19,4)` and `BIGINT` do not fit --
' and what you would get instead is not an error but an ordinary-looking
' number that is wrong in its last digits. So `odbc` hands those back as the
' driver's exact text and lets you decide.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists ledger")
  odbc.exec(db, "create table ledger (id bigint, note varchar(40), amount varchar(30))")

  ' 9007199254740993 is 2^53+1: the smallest positive integer a double cannot
  ' represent. Watch what a round trip through one does to it.
  big = "9007199254740993"
  odbc.exec(db, "insert into ledger values (?, ?, ?)", [big, "an account number", "12345678901234.5678"])

  row = odbc.query(db, "select id, note, amount from ledger")[0]

  print "id type   : " + type(row.id)
  print "id exact  : " + row.id
  print "id doubled: " + string(number(row.id)) + "   <- one less, silently"
  print ""
  print "amount    : " + row.amount + " (" + type(row.amount) + ")"

  ' Where the loss is acceptable -- a chart axis, a rough total -- convert
  ' deliberately with `number()`. Where it is not, keep the string, or use
  ' gBASIC money, which is exact integer cents.
  print ""
  price {USD}= 1234.56
  odbc.exec(db, "insert into ledger values (?, ?, ?)", ["2", "a price", price])
  back = odbc.query(db, "select amount from ledger where note = ?", ["a price"])
  print "money in, text out: " + back[0].amount

  ' Ordinary INTEGER, REAL and FLOAT columns are numbers, as you would expect.
  ' Only the types that cannot survive a double are held back as text.
  odbc.exec(db, "drop table if exists sizes")
  odbc.exec(db, "create table sizes (n integer, x real)")
  odbc.exec(db, "insert into sizes values (?, ?)", [42, 0.5])
  s = odbc.query(db, "select n, x from sizes")[0]
  print ""
  print "integer -> " + type(s.n) + " " + string(s.n)
  print "real    -> " + type(s.x) + " " + string(s.x)

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
