' A reader may not write (docs/gsql_parallel_readers_design.md §4): with
' GBASIC_GSQL_TEST_READER_WRITE set, each reader first tries to create a tree
' and commit it, and answers what the engine said. run_gsql_readers.sh
' requires READ_ONLY. A short read, which no reader answers, still works, and
' so does the database afterwards.
load gsql

path = join_path(env("GSQL_DIR"), "write.gsql")
db = gsql.connect(path, { create: true })
x = gsql.exec(db, "create table t (a integer primary key, c integer)")
rows = []
for i = 1 to 30000
    rows = append(rows, { a: i, c: mod(i * 104729, 9973) })
end for
x = gsql.insert(db, "t", rows)

function attempt(db)
    on error goto next
    r = gsql.query(db, "select c, count(*) as n from t group by c order by c")
    e = error
    if e then
        return e.message
    end if
    return string(len(r)) + " rows"
end function

print "a long read: " + attempt(db)
print "a short read: " + string(gsql.query(db, "select c from t where a = 7")[0].c)
