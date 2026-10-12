' Recipe 4 — Money and big integers come back as STRINGS, on purpose.
'
' THE MOST IMPORTANT RECIPE HERE. A `DECIMAL(19,4)` holds more significant
' digits than a gBASIC number can, and what you would get instead of an error
' is an ordinary-looking number that is wrong in its last digits. So `odbc`
' hands those back as the driver's exact text and lets you decide.
'
' WHAT CHANGED, because this recipe used to say "a gBASIC number is a double"
' and that is no longer the whole story. Since 0.6.0 a number is EXACT for any
' integer the runtime can hold, and since 0.7.1 `number("...")` produces one
' from integer text -- so a BIGINT now converts without losing anything, its
' whole range included. The column still arrives as text, and whether it should
' is an open question in docs/exact_number_design.md.
'
' A FRACTION IS WHERE THE LOSS STILL LIVES. 12345678901234.5678 has no exact
' number to become, so converting it drops a digit, quietly. That is the case
' this recipe is really about.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists ledger")
  odbc.exec(db, "create table ledger (id bigint, note varchar(40), amount varchar(30))")

  ' 9007199254740993 is 2^53+1: the smallest positive integer a DOUBLE cannot
  ' represent -- which is why this column is text, and why converting it used to
  ' cost you the last digit.
  big = "9007199254740993"
  odbc.exec(db, "insert into ledger values (?, ?, ?)", [big, "an account number", "12345678901234.5678"])

  row = odbc.query(db, "select id, note, amount from ledger")[0]

  print "id type   : " + type(row.id)
  print "id exact  : " + row.id
  print "id number : " + string(number(row.id)) + "   <- exact, since 0.7.1"
  print ""
  print "amount    : " + row.amount + " (" + type(row.amount) + ")"
  print "amount num: " + string(number(row.amount)) + "   <- a digit gone, silently"

  ' So: an integer id converts freely. A fractional amount does not -- where the
  ' loss is acceptable (a chart axis, a rough total) convert deliberately with
  ' `number()`, and where it is not, keep the string or use gBASIC money, which
  ' is exact integer cents.
  print ""
  price {USD}= 1234.56
  odbc.exec(db, "insert into ledger values (?, ?, ?)", ["2", "a price", price])
  back = odbc.query(db, "select amount from ledger where note = ?", ["a price"])
  print "money in, text out: " + back[0].amount

  ' Ordinary INTEGER, REAL and FLOAT columns are numbers, as you would expect.
  ' Only the three types whose values may not survive the conversion -- BIGINT,
  ' DECIMAL and NUMERIC -- are held back as text.
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
