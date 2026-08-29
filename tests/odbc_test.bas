' The odbc module, checked against whatever driver the runner points it at.
'
' SELF-CHECKING: every line states the answer it expects and prints `ok` or a
' MISMATCH naming both sides. A plain golden would record whatever the module
' answered AS the expected output, which is exactly wrong here -- the defect
' this module is most concerned to avoid (a DECIMAL(19,4) narrowed to a
' double) produces a PLAUSIBLE NUMBER, not an error, so a golden would
' enshrine the loss and defend it forever.
'
' The connection string comes from the environment so the same fixture runs
' against SQLite via ODBC in CI and against a real SQL Server or MySQL when
' someone points it at one.

load odbc

' gBASIC has no closures, so the counters live in a shared record and the
' function mutates its fields. A plain `checks += 1` inside a function
' creates a function-local and the totals stay at zero -- which is worth a
' comment because a suite that reports "0 checks, 0 mismatches" looks green.
tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if got = want then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

conn = env("GBASIC_ODBC_CONNECTION")
db = odbc.connect(conn)

' ---------------------------------------------------------------- catalog
' A dashboard has to be able to say "no driver of that name is installed",
' which is a different answer from "the server refused you".
drivers = odbc.drivers()
check("odbc.drivers returns an array", type(drivers), "array")
named = 0
for each d in drivers
    if d.name != "" and has(d, "attributes") then
        named += 1
    end if
next
check("every driver row carries a name and attributes", named, count(drivers))

sources = odbc.sources()
check("odbc.sources returns an array", type(sources), "array")

' ---------------------------------------------------------------- schema
' PORTABLE COLUMN TYPES ONLY, and that is not fussiness: `timestamp` means a
' ROWVERSION on SQL Server -- an auto-generated binary value you cannot insert
' into -- so the obvious spelling for "a date and a time" silently means
' something else on one of the three backends this suite runs against.
odbc.exec(db, "drop table if exists gb_odbc_t")
odbc.exec(db, "drop table if exists gb_odbc_keep")

r = odbc.exec(db, "create table gb_odbc_t (id integer, name varchar(40), note varchar(6000), flag bit, when_on date, at_time datetime, big bigint, exact varchar(40))")
check("create reports its command", r.command, "CREATE")

r = odbc.exec(db, "create table gb_odbc_keep (id integer)")
check("second create reports its command", r.command, "CREATE")
odbc.exec(db, "insert into gb_odbc_keep (id) values (1)")

' ---------------------------------------------------------------- binding
' Every value below travels as a BOUND parameter. There is no path in the
' module that pastes a value into SQL text, and the injection tier further
' down is what proves that rather than asserts it.
march5 {date}= "2024-03-05"
newyear {date}= "2024-12-31"

r = odbc.exec(db, "insert into gb_odbc_t (id, name, note, flag, when_on, at_time, big, exact) values (?, ?, ?, ?, ?, ?, ?, ?)", [1, "alice", "first", true, march5, "2024-03-05 13:45:01", "9007199254740993", "12345678901234.5678"])
check("insert reports its command", r.command, "INSERT")
check("insert reports one row affected", r.rows_affected, 1)

r = odbc.exec(db, "insert into gb_odbc_t (id, name, note, flag, when_on, at_time, big, exact) values (?, ?, ?, ?, ?, ?, ?, ?)", [2, "bob", nothing, false, newyear, "2024-12-31 23:59:59", "-9007199254740993", "-0.05"])
check("second insert reports one row affected", r.rows_affected, 1)

' ---------------------------------------------------------------- reading
rows = odbc.query(db, "select id, name, note, flag, when_on, at_time, big, exact from gb_odbc_t order by id")
check("query returns an array", type(rows), "array")
check("query returns both rows", count(rows), 2)

first_row = rows[0]
check("integer column is a number", type(first_row.id), "number")
check("integer column reads back", first_row.id, 1)
check("varchar column is a string", type(first_row.name), "string")
check("varchar column reads back", first_row.name, "alice")
check("bit column is a boolean", type(first_row.flag), "boolean")
check("bit column reads back true", first_row.flag, true)
check("date column is a datetime", type(first_row.when_on), "datetime")
check("date column reads back", string(first_row.when_on), "2024-03-05")
check("timestamp column is a datetime", type(first_row.at_time), "datetime")
check("timestamp column reads back", string(first_row.at_time), "2024-03-05 13:45:01")

second_row = rows[1]
check("SQL NULL becomes nothing", second_row.note, nothing)
check("nothing is not the empty string", second_row.note = "", false)
check("bit column reads back false", second_row.flag, false)
check("second row reads back", second_row.name, "bob")

' ------------------------------------------------------------- exactness
' THE TIER THIS MODULE EXISTS FOR. 9007199254740993 is 2^53+1: the smallest
' positive integer a double cannot hold. Round-tripped through a double it
' comes back 9007199254740992 -- off by one, entirely plausible, and silent.
' The module answers BIGINT, DECIMAL and NUMERIC as STRINGS for this reason,
' which is also what `pg` already does with oids 20 and 1700.
check("bigint beyond 2^53 is a string", type(first_row.big), "string")
check("bigint beyond 2^53 survives exactly", first_row.big, "9007199254740993")
check("a negative bigint survives exactly", second_row.big, "-9007199254740993")
check("through a double it would not have", string(number(first_row.big)), "9007199254740992")

' The same property one step further out: eighteen significant digits of
' decimal, held in a column the engine will not "helpfully" make numeric.
check("an exact decimal is a string", type(first_row.exact), "string")
check("an exact decimal survives to the last digit", first_row.exact, "12345678901234.5678")
check("a small negative decimal survives", second_row.exact, "-0.05")

' ----------------------------------------------------------- size and shape
' A column is read back with SQLGetData in 1KB pieces, so a value longer than
' one piece is the case where a reader either drops the tail or duplicates a
' chunk -- and either way returns a string that LOOKS like a value.
long_text = ""
i = 0
while i < 500
    long_text = long_text + "0123456789"
    i = i + 1
end while

price {USD}= 1234.56
odbc.exec(db, "insert into gb_odbc_t (id, name, note, exact) values (?, ?, ?, ?)", [10, "", long_text, price])
sized = odbc.query(db, "select name, note, exact from gb_odbc_t where id = ?", [10])
check("an empty string binds and reads back empty", sized[0].name, "")
check("a value spanning several read chunks has the right length", len(sized[0].note), 5000)
check("and is byte-identical, not merely the right length", sized[0].note = long_text, true)
' Money binds as exact decimal text with its SCALE DECLARED. Bound with a
' scale of zero, a driver is entitled to round 1234.56 to 1234 -- the loss
' this module refuses on the read side, arriving through the write side.
check("money keeps its cents through a bound parameter", sized[0].exact, "1234.56")

' ---------------------------------------------------------------- filters
picked = odbc.query(db, "select name from gb_odbc_t where id = ?", [2])
check("a bound filter selects one row", count(picked), 1)
check("a bound filter selects the right row", picked[0].name, "bob")

picked = odbc.query(db, "select name from gb_odbc_t where flag = ?", [true])
check("a boolean parameter filters", count(picked), 1)
check("a boolean parameter filters correctly", picked[0].name, "alice")

none_at_all = odbc.query(db, "select name from gb_odbc_t where id = ?", [99])
check("a query matching nothing returns an empty array", count(none_at_all), 0)
check("an empty result is still an array", type(none_at_all), "array")

' -------------------------------------------------------------- injection
' THE CLAIM IS EXECUTED, NOT ASSERTED, AND IT IS PROVEN NON-VACUOUS IN THE
' SAME BREATH. The tempting test -- bind `'); drop table x;--` and check the
' table still stands -- is worthless against a driver that refuses multiple
' statements anyway, which the SQLite3 ODBC driver does ("only one SQL
' statement allowed"). It would pass on a module that pasted every parameter
' straight into the SQL.
'
' So the payload here is a TAUTOLOGY, which subverts a SINGLE statement and
' therefore works on every engine. The fixture runs it BOTH ways: pasted, it
' must return every row; bound, exactly the one row whose name really is that
' text. The pasted half is what keeps the bound half honest.
tautology = "' or '1'='1"
odbc.exec(db, "insert into gb_odbc_t (id, name) values (?, ?)", [3, tautology])

pasted = odbc.query(db, "select id from gb_odbc_t where name = '" + tautology + "'")
bound = odbc.query(db, "select id from gb_odbc_t where name = ?", [tautology])
every_row = odbc.query(db, "select id from gb_odbc_t")
check("pasted, the payload matches every row", count(pasted), count(every_row))
check("bound, it matches only the row that holds it", count(bound), 1)
check("bound, it matches the right row", bound[0].id, 3)
check("the payload is genuinely hostile", count(pasted) > count(bound), true)

' The classic payload as well, with the witness table it targets. This one is
' inert against a single-statement driver, which is precisely why it is not
' the tier's only case.
hostile = "'); drop table gb_odbc_keep;--"
odbc.exec(db, "insert into gb_odbc_t (id, name) values (?, ?)", [7, hostile])
back = odbc.query(db, "select name from gb_odbc_t where id = ?", [7])
check("a hostile value stores and reads back unchanged", back[0].name, hostile)
survivors = odbc.query(db, "select id from gb_odbc_keep")
check("the table the injection targeted still stands", count(survivors), 1)

' ------------------------------------------------------------ transaction
odbc.begin(db)
odbc.exec(db, "insert into gb_odbc_t (id, name) values (?, ?)", [4, "rolled back"])
mid = odbc.query(db, "select id from gb_odbc_t where id = ?", [4])
check("a row inserted in a transaction is visible inside it", count(mid), 1)
odbc.rollback(db)
after = odbc.query(db, "select id from gb_odbc_t where id = ?", [4])
check("rollback discards it", count(after), 0)

odbc.begin(db)
odbc.exec(db, "insert into gb_odbc_t (id, name) values (?, ?)", [5, "committed"])
odbc.commit(db)
after = odbc.query(db, "select name from gb_odbc_t where id = ?", [5])
check("commit keeps it", count(after), 1)
check("commit keeps the right value", after[0].name, "committed")

' A statement after commit must still work: autocommit has to come back on,
' or every later write sits in an open transaction nobody ends.
odbc.exec(db, "insert into gb_odbc_t (id, name) values (?, ?)", [6, "after commit"])
odbc.close(db)
db2 = odbc.connect(conn)
after = odbc.query(db2, "select name from gb_odbc_t where id = ?", [6])
check("a write after commit is durable without an explicit commit", count(after), 1)

' ---------------------------------------------------------------- updates
r = odbc.exec(db2, "update gb_odbc_t set name = ? where id = ?", ["renamed", 1])
check("update reports its command", r.command, "UPDATE")
check("update counts the row it changed", r.rows_affected, 1)

r = odbc.exec(db2, "update gb_odbc_t set name = ? where id = ?", ["nobody", 999])
check("an update matching nothing counts zero", r.rows_affected, 0)

r = odbc.exec(db2, "delete from gb_odbc_t where id = ?", [6])
check("delete reports its command", r.command, "DELETE")
check("delete counts the row it removed", r.rows_affected, 1)

odbc.exec(db2, "drop table gb_odbc_t")
odbc.exec(db2, "drop table gb_odbc_keep")
odbc.close(db2)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
