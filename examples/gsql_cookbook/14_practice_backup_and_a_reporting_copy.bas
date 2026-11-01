' Recipe 14 (a practice): back up while the database is in use, and run long
' reports on the copy. A copy is a consistent snapshot, made a slice at a time
' while other programs go on reading and writing; backing up into the same
' place again refreshes it, keeping the files it already holds.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "live.gsql"), { create: true })
gsql.exec(db, "create table sale (id integer serial primary key, amount money(USD))")
gsql.begin(db)
for i = 1 to 1000
    gsql.exec(db, "insert into sale (amount) values ($1)", [{USD}"10.00"])
next
gsql.commit(db)

copy = join_path(work, "reporting.gsql")
first = gsql.backup(db, copy)
print "first backup: " + string(first.files) + " file, nothing kept: " + string(first.reused = 0)

gsql.exec(db, "insert into sale (amount) values ($1)", [{USD}"99.00"])
gsql.backup(db, copy)                    ' a refresh: as of now

' The report runs on the copy, so a long one stalls nobody on the live
' database. (This program now holds the copy open, and a copy that is open is
' not refreshed: a report runs as a program of its own, and its hold ends with
' it.)
report = gsql.connect(copy)
r = gsql.query(report, "select count(*) as n, sum(amount) as total from sale")[0]
print "the copy: " + string(r.n) + " sales, " + string(r.total)

on error goto next
gsql.backup(db, copy)
if error then
    print "refreshing it while it is open is refused: " + error.details.code
end if

gsql.close(report)
gsql.close(db)
remove_tree(work)
