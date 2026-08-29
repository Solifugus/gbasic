' What the odbc module REFUSES, and the exact words it refuses with.
'
' These cases all need a live connection, so they cannot be static `.err`
' goldens the way the arity refusals are -- the connection string is supplied
' by the runner. `on error goto next` (PLAT-ERR) makes them assertable in
' language: each raise is caught in this frame, its message compared, and the
' run continues.
'
' Every refusal here exists to prevent a specific PLAUSIBLE WRONG ANSWER
' rather than to be tidy, and the comments say which.

load odbc

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
odbc.exec(db, "drop table if exists gb_odbc_r")
odbc.exec(db, "create table gb_odbc_r (id integer, name varchar(20))")
odbc.exec(db, "insert into gb_odbc_r (id, name) values (1, 'one')")

on error goto next

' A parameter list that does not match the placeholders. Left unchecked, a
' driver either errors obscurely or -- worse on some drivers -- executes with
' the extra parameters ignored, which silently runs a DIFFERENT query.
x = odbc.query(db, "select id from gb_odbc_r where id = ?", [1, 2])
check("too many parameters is refused", error.message, "odbc statement expects 1 parameters but got 2")
error.clear()

x = odbc.query(db, "select id from gb_odbc_r where id = ?", [])
check("too few parameters is refused", error.message, "odbc statement expects 1 parameters but got 0")
error.clear()

' Parameters must be an array. A bare value here is the commonest slip and
' would otherwise be read as a one-element something-or-other.
x = odbc.query(db, "select id from gb_odbc_r where id = ?", 1)
check("a non-array parameter list is refused", error.message, "odbc query parameters must be an array")
error.clear()

x = odbc.query(db, 42)
check("non-string SQL is refused by query", error.message, "odbc.query SQL must be a string")
error.clear()

x = odbc.exec(db, 42)
check("non-string SQL is refused by exec", error.message, "odbc.exec SQL must be a string")
error.clear()

' `exec` returning rows means the caller asked for a command and wrote a
' query. Discarding the rows quietly is how a `select` that was meant to
' check something becomes a no-op nobody notices.
x = odbc.exec(db, "select id from gb_odbc_r")
check("exec refuses a statement that returns rows", error.message, "odbc.exec cannot discard row results; use odbc.query")
error.clear()

' A record has no faithful SQL rendering. Coercing it to its text form would
' store something that reads back as a string and compares equal to nothing.
x = odbc.query(db, "select id from gb_odbc_r where id = ?", [{ id: 1 }])
check("an unrepresentable parameter is refused", error.message, "unsupported odbc parameter type")
error.clear()

' Two columns of the same name collapse in a record: the second would win and
' the first would vanish with no sign that it ever existed.
x = odbc.query(db, "select id as c, name as c from gb_odbc_r")
check("duplicate result column names are refused", error.message, "duplicate odbc result column: c")
error.clear()

' SQL the server rejects arrives with the driver's own diagnostic attached,
' state code and all -- a bare "query failed" would send the author to read
' the server log instead of the message.
x = odbc.query(db, "select nope from gb_odbc_r")
check("bad SQL is refused", left(error.message, 19), "odbc prepare failed")
check("bad SQL carries the driver's diagnostic", contains(error.message, "["), true)
error.clear()

on error stop
odbc.exec(db, "drop table gb_odbc_r")
odbc.close(db)
on error goto next

' Use after close. The handle is still a value -- gBASIC has no way to
' un-exist it -- so the module has to say so rather than dereference a freed
' driver handle.
x = odbc.query(db, "select 1")
check("a query on a closed connection is refused", error.message, "odbc connection is closed")
error.clear()

odbc.close(db)
check("closing twice is refused", error.message, "odbc connection is already closed")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
