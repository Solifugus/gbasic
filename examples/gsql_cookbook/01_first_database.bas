' Recipe 1: a first database -- create it, make a table, put rows in, ask.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "shop.gsql"), { create: true })

gsql.exec(db, "create table item (id integer primary key, name text not null, price money(USD))")

' parameters: a record for :name placeholders, an array for $1, $2 ...
gsql.exec(db, "insert into item (id, name, price) values (:id, :name, :price)",
          { id: 1, name: "apple", price: {USD}"0.50" })
gsql.exec(db, "insert into item values ($1, $2, $3)", [2, "pear", {USD}"0.75"])
r = gsql.exec(db, "insert into item values (3, 'fig', cast('1.20' as money(USD))), (4, 'kiwi', cast('0.40' as money(USD)))")
print r.command + ": " + string(r.rows_affected) + " rows"

' a query answers an array of records, fields in select-list order
for each row in gsql.query(db, "select id, name, price from item where price < $1 order by price", [{USD}"1.00"])
    print string(row.id) + "  " + row.name + "  " + string(row.price)
next

gsql.close(db)
remove_tree(work)
