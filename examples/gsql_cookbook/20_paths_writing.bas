' Recipe 20: write by path. Assigning a record writes the columns it names;
' a key of one table that names no row inserts it, by INSERT's rules. insert
' answers the rows as stored, and through a step the foreign key comes from
' the path. delete answers how many it removed, cascading as the keys say.
load gsql

if exists({dir}"cookbook_paths_write.gsql") then
    remove_tree({dir}"cookbook_paths_write.gsql")
end if

database shop( file: "cookbook_paths_write.gsql", create: true )

sql make on shop
    create table customer (id integer serial primary key, name text not null, city text);
    create table invoice (id integer serial primary key, customer integer references customer on delete cascade, total money(USD), paid boolean default false)
end sql

make.exec()

ana = shop.customer.insert({ name: "Ana", city: "Rome" })
print "inserted customer " + string(ana.id)

shop.customer[ana.id] = { city: "Paris" }               ' the row is there: an UPDATE
shop.customer[7] = { name: "Gus" }                      ' it is not: an INSERT
print (shop.customer[1]).city + ", and customer 7 is " + (shop.customer[7]).name

' through a step from one customer, the invoice's customer is filled in
shop.customer[1].invoice.insert([{ total: "20.00" }, { total: "35.50" }])
first = shop.customer[7].invoice.insert({ total: "9.99" })
print "Gus's invoice " + string(first.id) + " belongs to customer " + string(first.customer)

shop.customer[1].invoice[.total < 30] = { paid: true }  ' every row the path names
print "paid: " + string(shop.invoice[.paid = true].count())

print "deleted " + string(shop.customer[7].delete()) + " customer, and its invoices with it"
print "invoices left: " + string(shop.invoice.count())

shop.close()
remove_tree({dir}"cookbook_paths_write.gsql")
