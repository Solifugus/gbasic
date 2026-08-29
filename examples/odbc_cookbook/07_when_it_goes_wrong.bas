' Recipe 7 — Failures, and telling them apart.
'
' A connection that does not open has two very different causes and an
' application should not confuse them: the driver is not installed on this
' machine (an operator's problem), or the server refused you (a credentials or
' network problem). `odbc.drivers` and `odbc.sources` are how you say which.

program main(args)
  load odbc

  ' Every raise is catchable frame-locally with `on error goto next`; the
  ' statement is abandoned and execution continues at the next one.
  on error goto next
  db = odbc.connect("Driver=NoSuchDriver;Database=whatever")
  if error then
    print "connect failed"
    print "  source : " + error.source
    print "  code   : " + string(error.code)
    ' The driver manager's own words, state code included. Printed here only
    ' shown only as far as the colon here, because the rest is the manager's
    ' version-specific wording and this page is a golden.
    print "  message: " + left(error.message, 22) + " ..."
    error.clear()
  end if
  on error stop

  ' Before blaming credentials, ask what is actually installed.
  installed = []
  for each d in odbc.drivers()
    append(installed, d.name)
  next
  ' (The list itself is whatever this machine has installed, so this page
  ' prints a fact about it rather than its contents.)
  print ""
  print "odbc.drivers() gives   : " + type(odbc.drivers())
  print "is NoSuchDriver one?   : " + string(contains(installed, "NoSuchDriver"))

  ' odbc.sources() lists configured DSNs the same way, so "DSN=warehouse"
  ' failing can be answered with "there is no DSN by that name" rather than a
  ' guess.
  print "configured DSNs        : " + type(odbc.sources())

  ' Errors from a live connection carry the SERVER's diagnostic, which is
  ' usually the sentence that tells you what to fix.
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")
  odbc.exec(db, "drop table if exists small")
  odbc.exec(db, "create table small (id integer)")
  on error goto next
  odbc.query(db, "select nope from small")
  if error then
    print ""
    print "bad column: " + left(error.message, 19) + " (plus the driver's own text)"
    error.clear()
  end if
  on error stop

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
