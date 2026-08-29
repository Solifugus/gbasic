' Recipe 8 — Query results are already the shape the rest of the tree wants.
'
' `odbc.query` returns an array of records, which is exactly what `frame` and
' everything built on it consumes. So a table in SQL Server reaches the chart
' library, the statistics library or an .xlsx without a conversion step.

program main(args)
  load odbc
  load frame

  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")
  odbc.exec(db, "drop table if exists sales")
  odbc.exec(db, "create table sales (region varchar(20), units integer)")
  odbc.exec(db, "insert into sales values ('north', 120)")
  odbc.exec(db, "insert into sales values ('south', 80)")
  odbc.exec(db, "insert into sales values ('north', 45)")

  rows = odbc.query(db, "select region, units from sales order by region, units")

  ' Straight into a frame -- no mapping, no rename.
  f = frame.from_rows(rows)
  print "columns: " + string(frame.columns(f))
  print "shape  : " + string(frame.shape(f))

  ' A frame is column-major plain data, so a column IS an array and the whole
  ' univariate statistics surface works on it directly.
  print "units  : " + string(f.units)
  print "total  : " + string(sum(f.units))
  print "mean   : " + string(mean(f.units))

  ' PUSH THE WORK TO THE DATABASE where you can. A grouped total computed by
  ' SQL moves three numbers over the wire instead of three thousand -- and
  ' this is the habit that matters once the table is real.
  print ""
  for each row in odbc.query(db, "select region, sum(units) as total from sales group by region order by region")
    print row.region + ": " + string(row.total)
  next

  odbc.close(db)
  g {file}= "examples/tmp_cookbook_odbc.db"
  if exists(g) then
    delete(g)
  end if
end program
