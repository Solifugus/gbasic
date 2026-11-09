' gsql.space (gdash B3): what a database's directory holds, printed for
' run_gsql_module's space tier to hold against `stat`. Swaps a table in and
' out, as a dashboard's refresh does: load `stage`, then drop `live` and
' rename `stage` to it in one transaction.
load gsql
dir = env("GSQL_DIR") + "/space"
db = gsql.connect(dir, { create: true, linger: 0 })
for round = 1 to 6
    gsql.exec(db, "CREATE TABLE stage (id number PRIMARY KEY, a text, b text)")
    rows = []
    for i = 1 to 5000
        append(rows, { id: i, a: "row " + string(i) + " of round " + string(round), b: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" })
        if len(rows) = 1000 then
            gsql.insert(db, "stage", rows)
            rows = []
        end if
    next
    gsql.begin(db)
    if round > 1 then
        gsql.exec(db, "DROP TABLE live")
    end if
    gsql.exec(db, "ALTER TABLE stage RENAME TO live")
    gsql.commit(db)
next
s = gsql.space(db)
print "OPEN " + string(s.total) + " " + string(s.logs) + " " + string(s.tables)
print "ROWS " + string(gsql.query(db, "SELECT count(*) AS n FROM live")[0].n)
print "STOPPED " + string(gsql.close(db, { wait: 10 }))
s = gsql.space(dir)
print "CLOSED " + string(s.total) + " " + string(s.logs) + " " + string(s.tables)
r = try gsql.space(env("GSQL_DIR"))
print "NOTDB " + string(r)
on error goto next
gsql.space(env("GSQL_DIR"))
if error then
    print "NOTDB_MESSAGE " + error.message
end if
gsql.space(42)
if error then
    print "KIND_MESSAGE " + error.message
end if
