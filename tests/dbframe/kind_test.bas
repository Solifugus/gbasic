' dbframe.schema's `kind` and dbframe.gsql_create_sql (the gBASIC books' volume
' 3, Chapter 6, and Matthew's ruling 2026-11-08): each column's gBASIC kind,
' decided from every value, and G-SQL DDL written from it. The kinds are written
' here by hand. The DDL is then RUN, and the frame loaded into the table it made
' with to_gsql: advice that G-SQL refused, or that refused the frame's own rows,
' would be wrong however it read. Run by tests/run_dbframe.sh with
' GBASIC_PATH=stdlib, from inside an empty directory.
load dbframe
load frame
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

' --- one column at a time -----------------------------------------------------
check("dates, with a gap", dbframe.column_kind([{date}"2026-01-02", unknown, {date}"2026-01-03"]), "date")
check("date-times", dbframe.column_kind([{datetime}"2026-01-02 10:00", {datetime}"2026-01-03 11:30:15"]), "datetime")
check("times", dbframe.column_kind([{time}"09:00", {time}"17:30:00"]), "time")
check("a date beside a date-time is mixed", dbframe.column_kind([{date}"2026-01-02", {datetime}"2026-01-02 10:00"]), "text")
check("durations, one with a month", dbframe.column_kind([90 seconds, 1 month]), "duration")
check("money in one currency", dbframe.column_kind([{USD}"1.00", {USD}"2.50"]), "money(USD)")
check("money in two", dbframe.column_kind([{USD}"1.00", {EUR}"2.00"]), "money")
check("booleans", dbframe.column_kind([true, false]), "boolean")
check("whole numbers are number, not integer", dbframe.column_kind([1, 2, 3]), "number")
check("numbers", dbframe.column_kind([1, 2.5]), "number")
check("text", dbframe.column_kind(["a", "b"]), "text")
check("numbers then text", dbframe.column_kind([1, "n/a"]), "text")
check("money then a number", dbframe.column_kind([{USD}"1.00", 2]), "text")
check("nothing but gaps", dbframe.column_kind([unknown, unknown]), "text")
check("a record", dbframe.column_kind([{ a: 1 }]), "text")
check("type is unchanged beside it", dbframe.column_type([{date}"2026-01-02"]), "TEXT")

' --- a frame: schema, the DDL, and the DDL run --------------------------------
df = frame.from_rows([
    { "Customer": "Ann", "Signed Up": {date}"2026-01-05", "Took": 3 days, "Fee": {USD}"25.00", "Active": true,
      "Score": 7, "Last Seen": {datetime}"2026-02-01 09:15:00", "Call At": {time}"09:30",
      "Closed": unknown },
    { "Customer": "Bo", "Signed Up": {date}"2026-01-09", "Took": 36 hours, "Fee": {USD}"30.50", "Active": false,
      "Score": 8.5, "Last Seen": unknown, "Call At": {time}"14:00",
      "Closed": {date}"2026-03-01" }
])
kinds = {}
for each c in dbframe.schema(df)
    kinds[c.column] = c.kind
end for
check("schema: customer", kinds.customer, "text")
check("schema: signed_up", kinds.signed_up, "date")
check("schema: took", kinds.took, "duration")
check("schema: fee", kinds.fee, "money(USD)")
check("schema: active", kinds.active, "boolean")
check("schema: score", kinds.score, "number")
check("schema: last_seen", kinds.last_seen, "datetime")
check("schema: call_at", kinds.call_at, "time")
' its first value is a gap, so a kind read from the first value would miss it
check("schema: closed", kinds.closed, "date")
check("schema keeps type", dbframe.schema(df)[1].type, "TEXT")

ddl = dbframe.gsql_create_sql("onboarding", df)
nl = chr(10)
check("the DDL", ddl, "create table \"onboarding\" (" + nl + "  \"customer\" TEXT," + nl + "  \"signed_up\" DATE," + nl +
      "  \"took\" DURATION," + nl + "  \"fee\" MONEY(USD)," + nl + "  \"active\" BOOLEAN," + nl + "  \"score\" NUMBER," +
      nl + "  \"last_seen\" DATETIME," + nl + "  \"call_at\" TIME," + nl + "  \"closed\" DATE" + nl + ")")

db = gsql.connect("kinds.gsql", { create: true })
gsql.exec(db, ddl)
cols = {}
for each c in gsql.columns(db, "select * from onboarding")
    cols[c.name] = c.kind
end for
check("G-SQL took the DDL: signed_up", cols.signed_up, "date")
check("G-SQL took the DDL: took", cols.took, "duration")
check("G-SQL took the DDL: fee", cols.fee, "money(USD)")
check("G-SQL took the DDL: active", cols.active, "boolean")
check("G-SQL took the DDL: call_at", cols.call_at, "time")
r = dbframe.to_gsql(df, db, "onboarding", {})
check("the frame loads into it", r.ok, true)
check("every row", r.rows, 2)
check("none refused", count(r.refused), 0)
back = gsql.query(db, "select * from onboarding order by customer")
check("a date comes back a date", back[0].signed_up, {date}"2026-01-05")
check("a duration as given", string(back[1].took), "36 hours")
check("money", back[1].fee, {USD}"30.50")
check("a boolean", back[1].active, false)
check("a fractional score", back[1].score, 8.5)
check("a gap stays unknown", is_unknown(back[1].last_seen), true)
check("a time", back[0].call_at, {time}"09:30")
check("a date after a gap", back[1].closed, {date}"2026-03-01")

on error goto next
x = dbframe.gsql_create_sql("bad name", df)
check("an unsafe table name is refused", contains(error.message, "unsafe table name: bad name"), true)
if error then
    error.clear()
end if
db.close()
print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)
