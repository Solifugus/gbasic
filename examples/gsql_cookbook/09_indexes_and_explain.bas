' Recipe 9: an index makes a lookup cost the size of the answer, not of the
' table. EXPLAIN says which path a query takes.
load gsql

if exists({dir}"cookbook_log.gsql") then
    remove_tree({dir}"cookbook_log.gsql")
end if

database log( file: "cookbook_log.gsql", create: true )

sql make_event on log
    create table event (id integer primary key, user_id integer, kind text, at date)
end sql

sql add_event on log
    insert into event values (:id, :user_id, :kind, :at)
end sql

sql index_user on log
    create index event_user on event (user_id)
end sql

sql by_id on log
    explain select * from event where id = 42
end sql

sql by_user on log
    explain select * from event where user_id = 7
end sql

sql by_user_and_kind on log
    explain select * from event where user_id = 7 and kind = 'click'
end sql

sql user_count on log
    select count(*) as n from event where user_id = 7
end sql

make_event.exec()
log.begin()
kind = "click"
at = {date}"2026-11-01"
for i = 1 to 2000
    id = i
    user_id = mod(i, 50)
    add_event.exec()
next
log.commit()

function plan(explained)
    for each row in explained
        print "    " + row.plan
    next
    return nothing
end function

print "by the primary key:"
plan(by_id)
print "by a column with no index -- every row is read:"
plan(by_user)
index_user.exec()
print "the same, with an index on it:"
plan(by_user)
print "an index narrows; every condition is still checked:"
plan(by_user_and_kind)
print string(user_count.query()[0].n) + " events for user 7"

log.close()
remove_tree({dir}"cookbook_log.gsql")
