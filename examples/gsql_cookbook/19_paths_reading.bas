' Recipe 19: read tables by path. Dots name tables, a bracket is a key or a
' filter whose columns are dotted, braces choose columns, and a dot after a
' path steps across the foreign key joining two tables. Each path is one SQL
' statement; .sql() shows it, and here each one is printed beside the sql
' block it equals.
load gsql

if exists({dir}"cookbook_paths.gsql") then
    remove_tree({dir}"cookbook_paths.gsql")
end if

database shop( file: "cookbook_paths.gsql", create: true )

sql make on shop
    create table customer (id integer primary key, name text, city text);
    create table invoice (id integer primary key, customer integer references customer, total money(USD), paid boolean);
    insert into customer values (1, 'Ana', 'Rome'), (2, 'Bo', 'Oslo'), (3, 'Cy', 'Rome');
    insert into invoice values (10, 1, 120.00, true), (11, 3, 40.50, false), (12, 3, 75.00, true)
end sql

sql unpaid_in on shop
    select i.* from invoice i join customer c on i.customer = c.id
    where c.city = :town and i.paid = false order by i.id
end sql

make.exec()

' a key: the row, read where the path ends
c = shop.customer[3]
print c.name + " lives in " + c.city

' a filter: a result, read when it is used
for each r in shop.customer[.city = "Rome"]
    print "in Rome: " + r.name
next

' a step from a parent to its children, and from a child to its parent
for each inv in shop.customer[3].invoice
    print "Cy's invoice " + string(inv.id) + ": " + string(inv.total)
next
print "invoice 10 is " + (shop.invoice[10].customer).name + "'s"

' a step from many, filtered at both ends, against the sql block it equals
town = "Rome"
by_path = shop.customer[.city = town].invoice[.paid = false].query()
print "unpaid in Rome: " + string(len(by_path)) + ", the same as the sql block: " + string(string(by_path) = string(unpaid_in.query()))

' braces: the columns, and a join when they come before a step
for each r in shop.customer[.city = "Rome"]{ name }.invoice{ id, total }
    print r.name + "  #" + string(r.id) + "  " + string(r.total)
next

' what a path is
print shop.customer[3].invoice.sql()

shop.close()
remove_tree({dir}"cookbook_paths.gsql")
