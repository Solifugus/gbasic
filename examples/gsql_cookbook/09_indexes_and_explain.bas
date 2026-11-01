' Recipe 9: an index makes a lookup cost the size of the answer, not of the
' table. EXPLAIN says which path a query takes.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "log.gsql"), { create: true })
gsql.exec(db, "create table event (id integer primary key, user_id integer, kind text, at date)")
gsql.begin(db)
for i = 1 to 2000
    gsql.exec(db, "insert into event values ($1, $2, $3, $4)", [i, mod(i, 50), "click", {date}"2026-11-01"])
next
gsql.commit(db)

function plan(sql)
    for each row in gsql.query(db, "explain " + sql)
        print "    " + row.plan
    next
    return nothing
end function

print "by the primary key:"
plan("select * from event where id = 42")
print "by a column with no index -- every row is read:"
plan("select * from event where user_id = 7")
gsql.exec(db, "create index event_user on event (user_id)")
print "the same, with an index on it:"
plan("select * from event where user_id = 7")
print "an index narrows; every condition is still checked:"
plan("select * from event where user_id = 7 and kind = 'click'")
print string(gsql.query(db, "select count(*) as n from event where user_id = 7")[0].n) + " events for user 7"

gsql.close(db)
remove_tree(work)
