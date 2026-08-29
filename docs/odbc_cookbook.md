# The ODBC cookbook

Eight recipes over `load odbc` — the module that reaches SQL Server, MySQL,
MariaDB, Oracle, DB2, Snowflake, Access and SQLite through whichever driver
the machine has installed. The API reference is
[reference.md](reference.md#odbc-module); this page is the working tour.

**This page cannot lie.** It owns neither the code nor the output it shows.
`examples/odbc_cookbook/NN_name.bas` owns the code, its `.out` owns the
output, `tools/sync_odbc_cookbook.sh` copies both in, and
`tests/run_odbc_cookbook.sh` fails while any of them disagree. Every block
below is executed on every test run.

The recipes use the SQLite3 ODBC driver so they can run anywhere, but nothing
about their shape is SQLite-specific: point `odbc.connect` at a different
connection string and the same code runs against a different database.

**A caution about scope.** As of this writing the module has been exercised
against the SQLite3 ODBC driver only. The design is driver-neutral and the
fixtures are parameterised (`GBASIC_ODBC_CONNECTION`) so they can be pointed
at a commercial backend, but that has not yet been done — treat behaviour
against SQL Server, MySQL, Oracle and DB2 as expected rather than verified.

---

## 1. Connect, ask a question, close

One module, every backend. What changes between databases is the connection
string and the SQL dialect — not the shape of this code.

<!--CODE:01_connect_and_query-->

```basic
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
```

<!--OUT:01_connect_and_query-->

```
rows: 2
1  Ada (compilers)
2  Grace (runtime)

first name: Ada
field names: ["id","name","team"]
```

A query returns an **array of records**. Field names are the result column
names, exactly as the SQL named them; arrays are 0-based, so the first row is
`rows[0]`. Two result columns with the same name are an error rather than a
record that silently keeps only the last one — write `select a.id as a_id,
b.id as b_id` when a join would collide.

---

## 2. Never build SQL by pasting values into it

`?` is a placeholder; the values travel in a separate array and the driver
binds them, so a value can never be read as SQL.

<!--CODE:02_parameters_never_paste-->

```basic
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
```

<!--OUT:02_parameters_never_paste-->

```
pasted matched: 3 rows
bound matched:  1 rows
and it is row:  3

refused: odbc statement expects 1 parameters but got 2
```

The recipe runs the same search term both ways so the difference is a fact
rather than a warning: pasted, the term's `or` takes over the condition and
the query answers about every row; bound, it matches only the row whose text
it really is. The parameter count must match the placeholders exactly — a
mismatch is refused rather than executed as a different query than you wrote.

Table and column *names* cannot be bound; only values can. If a name has to
be chosen at runtime, validate it against a list you control rather than
escaping it.

---

## 3. `query` asks, `exec` changes

<!--CODE:03_exec_and_rows_affected-->

```basic
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
```

<!--OUT:03_exec_and_rows_affected-->

```
CREATE
INSERT 1
UPDATE 2
UPDATE 0   (no such row)
DELETE 1

refused: odbc.exec cannot discard row results; use odbc.query
```

`odbc.exec` returns `{ command, rows_affected }`. That count is how you tell
"I updated the row" from "there was no such row" — both are successes, and
nothing else separates them. Handing `exec` a statement that returns rows is
refused: discarding them quietly is how a check that was meant to verify
something becomes a no-op nobody notices.

---

## 4. Money and big integers come back as strings

The most important page here.

<!--CODE:04_exact_numbers-->

```basic
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
```

<!--OUT:04_exact_numbers-->

```
id type   : string
id exact  : 9007199254740993
id doubled: 9007199254740992   <- one less, silently

amount    : 12345678901234.5678 (string)

money in, text out: 1234.56

integer -> number 42
real    -> number 0.5
```

A gBASIC number is a double: about fifteen significant digits. `DECIMAL(19,4)`
and `BIGINT` do not fit, and what you would get instead is not an error but an
ordinary-looking number wrong in its last digits — see the account number
above losing its final `3`. So `BIGINT`, `DECIMAL` and `NUMERIC` arrive as the
driver's exact text and the decision to narrow is yours, spelled `number()`.
`pg` already answers `bigint` and `numeric` this way, so the convention is the
same across backends.

Going the other way, a gBASIC money value binds as exact decimal text with its
scale declared, so the write side does not reintroduce the loss the read side
avoids. `INTEGER`, `REAL` and `FLOAT` are ordinary numbers.

---

## 5. NULL is `nothing`, and dates are datetimes

<!--CODE:05_nulls_and_dates-->

```basic
' Recipe 5 — SQL NULL is `nothing`, and date columns are datetimes.
'
' Two conversions worth knowing before you write a condition that reads
' correctly and answers wrongly.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists tickets")
  odbc.exec(db, "create table tickets (id integer, closed_on date, note varchar(40))")

  opened {date}= "2026-03-05"
  odbc.exec(db, "insert into tickets values (?, ?, ?)", [1, opened, "shipped"])
  odbc.exec(db, "insert into tickets values (?, ?, ?)", [2, nothing, nothing])

  rows = odbc.query(db, "select id, closed_on, note from tickets order by id")

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
  odbc.exec(db, "drop table if exists events")
  odbc.exec(db, "create table events (at_time timestamp)")
  odbc.exec(db, "insert into events values (?)", ["2026-03-05 13:45:01"])
  e = odbc.query(db, "select at_time from events")[0]
  print ""
  print "timestamp -> " + type(e.at_time) + " " + string(e.at_time)

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program
```

<!--OUT:05_nulls_and_dates-->

```
ticket 1
  note is nothing : false
  note = ""       : false
ticket 2
  note is nothing : true
  note = ""       : false

closed_on type  : datetime
closed_on       : 2026-03-05
  year          : 2026
  dayname       : Thursday

timestamp -> datetime 2026-03-05 13:45:01
```

SQL `NULL` becomes `nothing`, which is **not** the empty string — test with
`is_nothing`, never `= ""`. The two are different facts about a row and a
condition that confuses them reads correctly while answering wrongly.

`DATE`, `TIME` and `TIMESTAMP` columns arrive as gBASIC datetimes, so the date
fields and date arithmetic work with no parsing step. Fractional seconds are
dropped.

---

## 6. All of it, or none of it

<!--CODE:06_transactions-->

```basic
' Recipe 6 — All of it, or none of it.
'
' `begin` / `commit` / `rollback` are spelled exactly as `sqlite`'s and `pg`'s
' are, so code that moves between backends does not change. Underneath, ODBC
' has no BEGIN statement: `begin` turns the connection's autocommit off and
' the other two turn it back on.

program main(args)
  load odbc
  db = odbc.connect("Driver=SQLite3;Database=examples/tmp_cookbook_odbc.db")

  odbc.exec(db, "drop table if exists accounts")
  odbc.exec(db, "create table accounts (name varchar(20), balance integer)")
  odbc.exec(db, "insert into accounts values ('alice', 100)")
  odbc.exec(db, "insert into accounts values ('bob', 0)")

  ' A transfer is two writes that must both happen or neither.
  odbc.begin(db)
  odbc.exec(db, "update accounts set balance = balance - ? where name = ?", [40, "alice"])
  odbc.exec(db, "update accounts set balance = balance + ? where name = ?", [40, "bob"])
  odbc.commit(db)
  print "after commit:   " + balances(db)

  ' The same pair, abandoned. Nothing survives -- not even the first write,
  ' which is the point.
  odbc.begin(db)
  odbc.exec(db, "update accounts set balance = balance - ? where name = ?", [999, "alice"])
  odbc.exec(db, "update accounts set balance = balance + ? where name = ?", [999, "bob"])
  odbc.rollback(db)
  print "after rollback: " + balances(db)

  ' Autocommit is back on after either, so an ordinary write needs no
  ' ceremony and is durable on its own.
  odbc.exec(db, "update accounts set balance = ? where name = ?", [7, "bob"])
  print "plain write:    " + balances(db)

  odbc.close(db)
  f {file}= "examples/tmp_cookbook_odbc.db"
  if exists(f) then
    delete(f)
  end if
end program

function balances(db)
  parts = []
  for each row in odbc.query(db, "select name, balance from accounts order by name")
    append(parts, row.name + "=" + string(row.balance))
  next
  return join(parts, " ")
end function
```

<!--OUT:06_transactions-->

```
after commit:   alice=60 bob=40
after rollback: alice=60 bob=40
plain write:    alice=60 bob=7
```

`begin` / `commit` / `rollback` are spelled exactly as `sqlite`'s and `pg`'s,
so code moving between backends does not change. ODBC has no `BEGIN`
statement underneath: `begin` turns the connection's autocommit off and the
other two turn it back on, which is why an ordinary write after a commit
needs no ceremony and is durable on its own.

---

## 7. Failures, and telling them apart

<!--CODE:07_when_it_goes_wrong-->

```basic
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
```

<!--OUT:07_when_it_goes_wrong-->

```
connect failed
  source : odbc
  code   : 2003
  message: odbc connection failed ...

odbc.drivers() gives   : array
is NoSuchDriver one?   : false
configured DSNs        : array

bad column: odbc prepare failed (plus the driver's own text)
```

A connection that will not open has two very different causes: the driver is
not installed on this machine, or the server refused you. `odbc.drivers()` and
`odbc.sources()` are how an application says which, instead of showing its
user one unhelpful message for both. Errors carry `error.source = "odbc"`,
code `2003`, and the driver's own diagnostic including its SQLSTATE — which is
usually the sentence naming what to fix.

Every raise here is catchable frame-locally with `on error goto next`.

---

## 8. Results are already the shape the rest of the tree wants

<!--CODE:08_rows_into_a_frame-->

```basic
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
```

<!--OUT:08_rows_into_a_frame-->

```
columns: ["region","units"]
shape  : [3,2]
units  : [45,120,80]
total  : 245
mean   : 81.66666666666667

north: 165
south: 80
```

An array of records is what `frame.from_rows` takes, which puts a database
table one call away from the statistics library, the chart library and the
xlsx writer. A frame is column-major plain data, so a column *is* an array and
`sum`/`mean` work on it directly.

The last block is the habit worth forming: **push the work to the database
where you can.** A grouped total computed in SQL moves three numbers over the
wire instead of three thousand, and that difference is the whole game once the
table is real.

---

## What is not here yet

- **Binary columns.** `BINARY`, `VARBINARY` and `LONGVARBINARY` results are
  refused rather than mangled.
- **Verified support for commercial backends.** See the caution at the top.
- **Connection pooling and cursors.** One connection, one statement at a time.

Native MySQL and SQL Server modules — which would use each vendor's own client
rather than a driver manager — remain future work. ODBC came first because it
reaches all of them at once and requires no proprietary library to be shipped
or linked.
