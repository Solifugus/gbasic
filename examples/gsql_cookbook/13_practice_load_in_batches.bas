' Recipe 13 (a practice): load in batches. Each commit costs one sync to disk,
' and on slower storage the sync is most of the cost: measured at 10 ms a sync,
' one row per commit wrote 93 rows a second, and 1 000 rows per commit 67 739
' (docs/gsql_scale_measurements.md). So group rows into transactions of a few
' thousand -- big enough to share the sync, small enough to stay light.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "load.gsql"), { create: true })
' create indexes BEFORE loading: building one over rows already there is the
' slow path today (docs/gsql_practices.md)
gsql.exec(db, "create table reading (id integer primary key, sensor integer, value number)")
gsql.exec(db, "create index reading_sensor on reading (sensor)")

batch = 1000
commits = 0
gsql.begin(db)
for i = 1 to 2500
    gsql.exec(db, "insert into reading values ($1, $2, $3)", [i, mod(i, 7), i / 4])
    if mod(i, batch) = 0 then
        gsql.commit(db)
        commits = commits + 1
        gsql.begin(db)
    end if
next
gsql.commit(db)                 ' the last, partial batch
commits = commits + 1

r = gsql.query(db, "select count(*) as n from reading")[0]
print string(r.n) + " rows in " + string(commits) + " commits"
print string(gsql.query(db, "select count(*) as n from reading where sensor = 3")[0].n) + " readings from sensor 3"

gsql.close(db)
remove_tree(work)
