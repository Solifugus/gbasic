' Recipe 5: keys G-SQL makes for you -- counted, counted per scope, and random.
load gsql

if exists({dir}"cookbook_keys.gsql") then
    remove_tree({dir}"cookbook_keys.gsql")
end if

database keys( file: "cookbook_keys.gsql", create: true )

' a plain counter: 1, 2, 3, no gaps, and a number is never reused
' a block may hold several statements: it runs all of them or none
sql customers_in on keys
    create table customer (id integer serial primary key, name text);
    insert into customer (name) values ('Ada'), ('Grace'), ('Edsger');
    delete from customer where name = 'Edsger';
    insert into customer (name) values ('Barbara')
end sql

sql customers on keys
    select id, name from customer order by id
end sql

customers_in.exec()
for each row in customers
    print string(row.id) + " " + row.name             ' 3 is gone for good: Barbara is 4
next

' a counter per scope: each task's log lines are 1, 2, 3 ...
sql log_lines on keys
    create table task_log (task integer, n integer serial of (task), note text, primary key (task, n));
    insert into task_log (task, note) values (7, 'opened'), (8, 'opened'), (7, 'assigned'), (7, 'closed')
end sql

sql logged on keys
    select task, n, note from task_log order by task, n
end sql

log_lines.exec()
for each row in logged
    print "task " + string(row.task) + " #" + string(row.n) + " " + row.note
next

' per day of a datetime: the scope is any expression over the row
sql requests_in on keys
    create table request (at datetime, no integer serial of (cast(at as date)));
    insert into request (at) values (cast('2026-11-01 09:00' as datetime)),
                                    (cast('2026-11-01 17:30' as datetime)),
                                    (cast('2026-11-02 08:15' as datetime))
end sql

sql requests on keys
    select at, no from request order by at
end sql

requests_in.exec()
for each row in requests
    print string(row.at) + "  #" + string(row.no)
next

' random: a key nobody can guess, of a declared length and alphabet
sql members_in on keys
    create table member (id text random primary key, pin text random(6, '0123456789'), name text);
    insert into member (name) values ('Ada'), ('Grace')
end sql

sql members on keys
    select id, pin, name from member order by name
end sql

members_in.exec()
for each row in members
    ' the values differ every run, so this prints their shape, not them
    print row.name + ": id of " + string(len(row.id)) + " characters, pin of " + string(len(row.pin)) + " digits"
next

keys.close()
remove_tree({dir}"cookbook_keys.gsql")
