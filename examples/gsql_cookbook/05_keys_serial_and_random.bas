' Recipe 5: keys G-SQL makes for you -- counted, counted per scope, and random.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "keys.gsql"), { create: true })

' a plain counter: 1, 2, 3, no gaps, and a number is never reused
gsql.exec(db, "create table customer (id integer serial primary key, name text)")
gsql.exec(db, "insert into customer (name) values ('Ada'), ('Grace'), ('Edsger')")
gsql.exec(db, "delete from customer where name = 'Edsger'")
gsql.exec(db, "insert into customer (name) values ('Barbara')")
for each row in gsql.query(db, "select id, name from customer order by id")
    print string(row.id) + " " + row.name             ' 3 is gone for good: Barbara is 4
next

' a counter per scope: each task's log lines are 1, 2, 3 ...
gsql.exec(db, "create table task_log (task integer, n integer serial of (task), note text, primary key (task, n))")
gsql.exec(db, "insert into task_log (task, note) values (7, 'opened'), (8, 'opened'), (7, 'assigned'), (7, 'closed')")
for each row in gsql.query(db, "select task, n, note from task_log order by task, n")
    print "task " + string(row.task) + " #" + string(row.n) + " " + row.note
next

' per day of a datetime: the scope is any expression over the row
gsql.exec(db, "create table request (at datetime, no integer serial of (cast(at as date)))")
gsql.exec(db, "insert into request (at) values (cast('2026-11-01 09:00' as datetime)), " +
              "(cast('2026-11-01 17:30' as datetime)), (cast('2026-11-02 08:15' as datetime))")
for each row in gsql.query(db, "select at, no from request order by at")
    print string(row.at) + "  #" + string(row.no)
next

' random: a key nobody can guess, of a declared length and alphabet
gsql.exec(db, "create table member (id text random primary key, pin text random(6, '0123456789'), name text)")
gsql.exec(db, "insert into member (name) values ('Ada'), ('Grace')")
for each row in gsql.query(db, "select id, pin, name from member order by name")
    ' the values differ every run, so this prints their shape, not them
    print row.name + ": id of " + string(len(row.id)) + " characters, pin of " + string(len(row.pin)) + " digits"
next

gsql.close(db)
remove_tree(work)
