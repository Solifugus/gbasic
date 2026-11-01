' Recipe 1: a first database -- create it, make a table, put rows in, ask.
load gsql

' A `database` block names the file, found from the current directory; it
' opens itself the first time a statement uses it. This recipe cleans up
' after itself, before and after.
if exists({dir}"cookbook_first.gsql") then
    remove_tree({dir}"cookbook_first.gsql")
end if

database shop( file: "cookbook_first.gsql", create: true )

sql make on shop
    create table item (id integer primary key, name text not null, price money(USD))
end sql

' :name parameters are read from the variables of those names where the
' statement runs; nothing is ever pasted into the SQL
sql add on shop
    insert into item (id, name, price) values (:id, :name, :price)
end sql

sql add_two on shop
    insert into item values (3, 'fig', cast('1.20' as money(USD))), (4, 'kiwi', cast('0.40' as money(USD)))
end sql

sql under on shop
    select id, name, price from item where price < :limit order by price
end sql

gsql.exec(make)

id = 1
name = "apple"
price = {USD}"0.50"
gsql.exec(add)
id = 2
name = "pear"
price = {USD}"0.75"
gsql.exec(add)
r = gsql.exec(add_two)
print r.command + ": " + string(r.rows_affected) + " rows"

' a select block is iterated a row at a time, fields in select-list order
limit = {USD}"1.00"
for each row in under
    print string(row.id) + "  " + row.name + "  " + string(row.price)
next

gsql.close(shop)
remove_tree({dir}"cookbook_first.gsql")
