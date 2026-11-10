' A DURATION column (docs/gsql_duration_design.md): a duration stored exactly,
' field for field, and never keyed. The hand-written half states each answer
' and sets each refusal beside an accepted neighbour. The generated half makes
' its durations' FIELDS first, writes them as text, and holds what comes back
' against those fields -- the generator is the oracle, never the output read
' back -- then holds a dump and restore to the same serialize bytes.
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

' the refusal's code and its words after the column, or "ok"
function outcome(db, q)
    on error goto next
    db.exec(q)
    if error then
        m = error.message
        cut = find(m, " [")
        if not (is_unknown(cut) or cut = nothing) then
            m = mid(m, 0, cut)
        end if
        return error.details.code + ": " + m
    end if
    return "ok"
end function

function fields(d)
    return [d.years, d.months, d.weeks, d.days, d.hours, d.minutes, d.seconds]
end function

db = gsql.connect("durations.gsql", { create: true })
db.exec("create table t (k integer primary key, d duration, r record, duration integer)")

' --- stored as given --------------------------------------------------------
db.exec("insert into t values (?, ?, ?, ?)", [1, 24 hours, { d: 1 day }, 5])
got = db.query("select d, r, duration from t where k = 1")[0]
check("24 hours stays 24 hours, not 1 day", fields(got.d), [0, 0, 0, 0, 24, 0, 0])
check("and prints as given", string(got.d), "24 hours")
check("a duration inside a record is stored", fields(got.r.d), [0, 0, 0, 1, 0, 0, 0])
check("a column named duration is still a name", got.duration, 5)
db.exec("insert into t (k, d) values (2, '1 month 14 days 1 minute 30 seconds')")
check("text is read field for field", fields(db.query("select d from t where k = 2")[0].d), [0, 1, 0, 14, 0, 1, 30])
db.exec("insert into t (k, d) values (3, '-2 hours'), (4, '0 seconds'), (5, '2 weeks')")
check("a negative part", fields(db.query("select d from t where k = 3")[0].d), [0, 0, 0, 0, -2, 0, 0])
check("no duration", fields(db.query("select d from t where k = 4")[0].d), [0, 0, 0, 0, 0, 0, 0])
check("weeks stay weeks", fields(db.query("select d from t where k = 5")[0].d), [0, 0, 2, 0, 0, 0, 0])
check("a unit before its number", outcome(db, "insert into t (k, d) values (6, 'seconds 1')"),
      "TYPE: gsql.exec: t.d is duration, and the text given does not read as one: 'seconds 1': expected a whole number at 'seconds 1'")
check("units in any order", outcome(db, "insert into t (k, d) values (7, '5 seconds 2 days')"), "ok")
check("read in any order", fields(db.query("select d from t where k = 7")[0].d), [0, 0, 0, 2, 0, 0, 5])

' --- what the column refuses ------------------------------------------------
check("a number has no unit", outcome(db, "insert into t (k, d) values (8, 90)"),
      "TYPE: gsql.exec: t.d is duration, and a number has no unit: give text such as '90 seconds', or from gBASIC a duration such as (1 second) * n")
check("a fraction", outcome(db, "insert into t (k, d) values (8, '1.5 hours')"),
      "TYPE: gsql.exec: t.d is duration, and the text given does not read as one: '1.5 hours': a part is not a whole number, and each part of a duration is")
check("beside a whole one", outcome(db, "insert into t (k, d) values (8, '90 minutes')"), "ok")
check("a unit twice", outcome(db, "insert into t (k, d) values (9, '2 hours 3 hours')"),
      "TYPE: gsql.exec: t.d is duration, and the text given does not read as one: '2 hours 3 hours': hours are given twice")
check("an unknown unit", outcome(db, "insert into t (k, d) values (9, '2 fortnights')"),
      "TYPE: gsql.exec: t.d is duration, and the text given does not read as one: '2 fortnights': 'fortnights' is not a unit (year, month, week, day, hour, minute, second)")
check("empty text", outcome(db, "insert into t (k, d) values (9, '')"),
      "TYPE: gsql.exec: t.d is duration, and the text given does not read as one: '': the text is empty (no duration is '0 seconds')")
check("a date is not a duration", outcome(db, "insert into t (k, d) values (9, date '2026-01-01')"),
      "TYPE: gsql.exec: t.d is duration, and the value is a date")

why = "a duration has no single order (`1 month` against `31 days` has none); to find or sort by its length, keep its total_seconds in a NUMBER column beside it"
check("no index", outcome(db, "create index td on t (d)"), "SCHEMA: gsql.exec: t.d is a duration, which index td cannot hold: " + why)
check("beside an index on a number", outcome(db, "create index tdur on t (duration)"), "ok")
check("no primary key", outcome(db, "create table u (d duration primary key)"), "SCHEMA: gsql.exec: u.d is a duration, so it cannot be in a key: " + why)
check("no unique", outcome(db, "create table u (d duration unique)"), "SCHEMA: gsql.exec: u.d is a duration, which index u_d_key cannot hold: " + why)
check("no group by", outcome(db, "select d, count(*) from t group by d"), "EXPRESSION: gsql.exec: a duration cannot be grouped or made DISTINCT: G-SQL has no key for it")
check("no distinct", outcome(db, "select distinct d from t"), "EXPRESSION: gsql.exec: a duration cannot be grouped or made DISTINCT: G-SQL has no key for it")
check("no count(distinct)", outcome(db, "select count(distinct d) from t"), "EXPRESSION: gsql.exec: a duration cannot be grouped or made DISTINCT: G-SQL has no key for it")
check("INTERVAL is named", outcome(db, "create table u (d interval)"),
      "SYNTAX: gsql.exec: `interval` is not a G-SQL type: the types are NUMBER, INTEGER, TEXT, BOOLEAN, MONEY, MONEY(CUR), DATE, TIME, DATETIME, RECORD and DURATION; for INTERVAL, write DURATION")

' --- what works on one -------------------------------------------------------
check("= is gBASIC's, so 24 hours = 1 day", count(db.query("select k from t where d = ?", [1 day])), 1)
check("< between exact durations", count(db.query("select k from t where k <> 2 and d < cast('1 hour' as duration)")), 2)
check("ORDER BY over exact ones", db.query("select k from t where k in (1, 3, 8) order by d")[0].k, 3)
check("ORDER BY meeting a month refuses", outcome(db, "select k from t order by d"),
      "EXPRESSION: gsql.exec: ORDER BY cannot order a duration carrying months or years, because a month has no fixed length; use exact durations or dates.between")
check("CAST to text is its printed form", db.query("select cast(d as text) as x from t where k = 2")[0].x, "1 month 14 days 1 minute 30 seconds")
check("CAST from a number", outcome(db, "select cast(5 as duration) from t"),
      "EXPRESSION: gsql.exec: CAST to DURATION takes a duration's text, such as '90 seconds', and this value is number")
check("a duration plus a date", db.query("select date '2026-01-30' + d as x from t where k = 5")[0].x, {date}"2026-02-13")
db.exec("alter table t add column e duration default '1 hour'")
db.exec("insert into t (k) values (20)")
check("a DEFAULT is read as the column reads text", fields(db.query("select e from t where k = 20")[0].e), [0, 0, 0, 0, 1, 0, 0])
db.exec("update t set e = '3 minutes' where k = 1")
check("UPDATE reads text", fields(db.query("select e from t where k = 1")[0].e), [0, 0, 0, 0, 0, 3, 0])
db.insert("t", [{ k: 21, d: 7 seconds }, { k: 22, d: "4 days" }])
check("gsql.insert takes a duration", fields(db.query("select d from t where k = 21")[0].d), [0, 0, 0, 0, 0, 0, 7])
check("and text", fields(db.query("select d from t where k = 22")[0].d), [0, 0, 0, 4, 0, 0, 0])
check("gsql.columns names the kind", db.columns("select d from t")[0].kind, "duration")

' --- generated: the fields first ---------------------------------------------
' a fixed LCG, so the corpus is the same on every machine
state = { s: 20261108 }
function draw(n)
    state.s = mod(state.s * 1103515245 + 12345, 2147483648)
    return mod(state.s, n)
end function

units = ["year", "month", "week", "day", "hour", "minute", "second"]
db.exec("create table g (k integer primary key, d duration)")
want = []
for i = 0 to 299
    f = [0, 0, 0, 0, 0, 0, 0]
    parts = []
    for u = 0 to 6
        if draw(3) = 0 then
            amount = draw(2000) - 600
            if draw(10) = 0 then
                amount = draw(2147483647) - 1073741823
            end if
            f[u] = amount
            word = units[u]
            if amount != 1 and amount != -1 then
                word = word + "s"
            end if
            ' the reader takes any order, so half are written smallest first
            if draw(2) = 0 then
                append(parts, string(amount) + " " + word)
            else
                front = [string(amount) + " " + word]
                for each p in parts
                    append(front, p)
                end for
                parts = front
            end if
        end if
    end for
    text = join(parts, " ")
    if text = "" then
        text = "0 seconds"
    end if
    append(want, f)
    db.exec("insert into g values (?, ?)", [i, text])
end for
rows = db.query("select k, d from g order by k")
mism = 0
for each row in rows
    if fields(row.d) != want[row.k] then
        mism = mism + 1
        if mism <= 3 then
            print "MISMATCH generated " + string(row.k) + ": " + string(fields(row.d)) + " want " + string(want[row.k])
        end if
    end if
end for
check("300 generated durations read back as their own fields", mism, 0)
mism = 0
for each row in db.query("select k, cast(cast(d as text) as duration) as e from g order by k")
    if fields(row.e) != want[row.k] then
        mism = mism + 1
    end if
end for
check("and each one's printed text reads back as the same fields", mism, 0)

before = []
for each row in rows
    append(before, serialize(row.d))
end for
' a duration inside a record is refused by a dump, as a date there is
' (docs/gsql_dump_design.md §5), since encode has no form for either
on error goto next
gsql.dump(db, "durations.sql")
check("a dump refuses a record holding a duration", error.details.code, "TYPE")
if error then
    error.clear()
end if
db.exec("update t set r = null")
gsql.dump(db, "durations.sql")
db.close()
db2 = gsql.connect("restored.gsql", { create: true })
gsql.restore(db2, "durations.sql")
mism = 0
for each row in db2.query("select k, d from g order by k")
    if serialize(row.d) != before[row.k] then
        mism = mism + 1
    end if
end for
check("a dump and restore puts back the same bytes", mism, 0)
check("all 300 of them", count(db2.query("select k from g")), 300)
check("the default is restored", fields(db2.query("select e from t where k = 20")[0].e), [0, 0, 0, 0, 1, 0, 0])
db2.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
