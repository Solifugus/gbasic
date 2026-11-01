' Recipe 14 (a practice): back up while the database is in use, and run long
' reports on the copy. A copy is a consistent snapshot, made a slice at a time
' while other programs go on reading and writing; backing up into the same
' place again refreshes it, keeping the files it already holds.
load gsql

if exists({dir}"cookbook_live.gsql") then
    remove_tree({dir}"cookbook_live.gsql")
end if

database live( file: "cookbook_live.gsql", create: true )
' the copy is wherever the backup put it, so its statements run on a
' connection made at run time
database reporting()

sql make on live
    create table sale (id integer serial primary key, amount money(USD))
end sql

sql sell on live
    insert into sale (amount) values (:amount)
end sql

sql sales_total on reporting
    select count(*) as n, sum(amount) as total from sale
end sql

gsql.exec(make)
gsql.begin(live)
amount = {USD}"10.00"
for i = 1 to 1000
    gsql.exec(sell)
next
gsql.commit(live)

work = temp_dir()
copy = join_path(work, "reporting.gsql")
first = gsql.backup(live, copy)
print "first backup: " + string(first.files) + " file, nothing kept: " + string(first.reused = 0)

amount = {USD}"99.00"
gsql.exec(sell)
gsql.backup(live, copy)                  ' a refresh: as of now

' The report runs on the copy, so a long one stalls nobody on the live
' database. (This program now holds the copy open, and a copy that is open is
' not refreshed: a report runs as a program of its own, and its hold ends with
' it.)
report = gsql.connect(copy)
r = gsql.query(report, sales_total)[0]
print "the copy: " + string(r.n) + " sales, " + string(r.total)

on error goto next
gsql.backup(live, copy)
if error then
    print "refreshing it while it is open is refused: " + error.details.code
end if

gsql.close(report)
gsql.close(live)
remove_tree(work)
remove_tree({dir}"cookbook_live.gsql")
