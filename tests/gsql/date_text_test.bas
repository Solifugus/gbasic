' Text stored into a DATE column (gdash A7, Matthew's ruling 2026-11-07;
' docs/datetime_conversion_design.md §3): text with a time of day other than
' midnight is refused, since storing it would drop the time in silence, while
' an explicit CAST(... AS DATE) still takes its date. Every answer below is
' written by hand, and each refused shape sits beside an accepted neighbour.
' Run by tests/run_gsql_module.sh from inside an empty directory.
load gsql

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

db = gsql.connect("dates.gsql", { create: true })
db.exec("create table t (k integer primary key, d date)")
next_k = { v: 0 }

' "stored <date>" or "refused CODE <reason>", the reason being the message's
' words after the quoted text, up to the CAST advice
function outcome(e, k)
    if e then
        m = e.message
        at = find(m, "', ")
        if is_unknown(at) or at = nothing then
            return "refused " + e.details.code
        end if
        rest = mid(m, at + 3, len(m) - at - 3)
        cut = find(rest, "; CAST")
        if not (is_unknown(cut) or cut = nothing) then
            rest = mid(rest, 0, cut)
        end if
        return "refused " + e.details.code + " " + rest
    end if
    r = db.query("select d from t where k = $1", [k])
    return "stored " + string(r[0].d)
end function

function store(v)
    next_k.v = next_k.v + 1
    on error goto next
    db.exec("insert into t (k, d) values ($1, $2)", [next_k.v, v])
    return outcome(error, next_k.v)
end function

check("a date", store("2026-01-15"), "stored 2026-01-15")
check("a time of day is refused", store("2026-01-15T10:00"), "refused TYPE has a time of day (10:00:00), which a date would lose")
check("with a space and seconds", store("2026-01-15 10:00:05"), "refused TYPE has a time of day (10:00:05), which a date would lose")
check("a second past midnight", store("2026-01-15 00:00:01"), "refused TYPE has a time of day (00:00:01), which a date would lose")
check("a minute past midnight", store("2026-01-15T00:01"), "refused TYPE has a time of day (00:01:00), which a date would lose")
check("an hour alone", store("2026-01-15T10"), "refused TYPE has a time of day (10:00:00), which a date would lose")
' midnight loses nothing (Matthew: refuse a time other than midnight)
check("midnight is the date", store("2026-01-15T00:00"), "stored 2026-01-15")
check("midnight with seconds", store("2026-01-15 00:00:00"), "stored 2026-01-15")
check("a month has no day, as before", store("2026-01"), "refused TYPE")
check("text that is no date, as before", store("soon"), "refused TYPE")
check("nothing refused was written", len(db.query("select k from t")), 3)

' an explicit CAST still takes the date (§3)
db.exec("insert into t (k, d) values (100, cast('2026-01-15T10:00' as date))")
check("CAST takes the date of a time of day", db.query("select d from t where k = 100")[0].d, {date}"2026-01-15")
db.exec("insert into t (k, d) values (101, cast($1 as date))", ["2026-03-01 23:59:59"])
check("CAST of a bound parameter", db.query("select d from t where k = 101")[0].d, {date}"2026-03-01")
' a datetime VALUE was already refused, and still is
on error goto next
db.exec("insert into t (k, d) values (102, $1)", [{datetime}"2026-01-15 10:00"])
e = error
check("a date-time value is refused", e.details.code, "TYPE")
check("as a date and time", contains(e.message, "t.d is date, and the value is a date and time"), true)

' every place a value is stored
on error goto next
db.exec("update t set d = '2026-02-01 09:00' where k = 1")
e = error
check("UPDATE refuses a time of day", outcome(e, 1), "refused TYPE has a time of day (09:00:00), which a date would lose")
check("and leaves the row", db.query("select d from t where k = 1")[0].d, {date}"2026-01-15")
db.exec("update t set d = '2026-02-01T00:00' where k = 1")
check("UPDATE to midnight is the date", db.query("select d from t where k = 1")[0].d, {date}"2026-02-01")
db.exec("create table src (s text)")
db.exec("insert into src (s) values ('2026-04-01 12:30')")
on error goto next
db.exec("insert into t (k, d) select 200, s from src")
check("INSERT ... SELECT refuses a time of day", outcome(error, 200), "refused TYPE has a time of day (12:30:00), which a date would lose")
on error goto next
db.exec("create table dflt (k integer primary key, d date default '2026-01-01 08:00')")
check("a DEFAULT with a time of day is refused", outcome(error, 0), "refused TYPE has a time of day (08:00:00), which a date would lose")
db.exec("create table dflt (k integer primary key, d date default '2026-01-01T00:00')")
db.exec("insert into dflt (k) values (1)")
check("a DEFAULT at midnight is the date", db.query("select d from dflt")[0].d, {date}"2026-01-01")
' a comparison is not storing: it reads text as the column's kind, as before
' (rows 7, 8 and 100 hold the 15th)
check("a comparison still reads the text as a date", len(db.query("select k from t where d = '2026-01-15'")), 3)
db.close()

database dates( file: "dates.gsql" )
sql put on dates
    insert into t (k, d) values (:k, :d)
end sql
on error goto next
put.exec({ k: 300, d: "2026-05-05T17:45" })
check("an sql block given text from a record refuses it", outcome(error, 300), "refused TYPE has a time of day (17:45:00), which a date would lose")
put.exec({ k: 301, d: "2026-05-05" })
d = gsql.connect("dates.gsql")
check("and stores a date", d.query("select d from t where k = 301")[0].d, {date}"2026-05-05")
d.close()
dates.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
