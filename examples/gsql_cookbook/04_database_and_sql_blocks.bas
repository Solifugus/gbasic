' Recipe 4: declare the database and its statements instead of passing strings.
load gsql

' A `database` block's file is a literal, found from the current directory.
' This recipe cleans up after itself, before and after.
if exists({dir}"cookbook_blocks.gsql") then
    remove_tree({dir}"cookbook_blocks.gsql")
end if

database shop( file: "cookbook_blocks.gsql", create: true )

sql make on shop
    create table item (id integer primary key, name text not null, price money(USD))
end sql

sql add on shop
    insert into item (id, name, price) values (:id, :name, :price)
end sql

' a block ends at `end sql`, so SQL's own CASE ... END reads naturally
sql priced on shop
    select name, price,
           case when price < :limit then 'cheap'
                else 'dear'
           end as band
    from item
    order by id
end sql

make.exec()

' :name parameters are read from the variables in scope when the block runs
function stock(id, name, price)
    add.exec()
    return nothing
end function
stock(1, "apple", {USD}"0.50")
stock(2, "melon", {USD}"3.00")
stock(3, "plum", {USD}"0.80")

limit = {USD}"1.00"
for each row in priced          ' a statement block is iterated a batch at a time
    print row.name + "  " + string(row.price) + "  " + row.band
next

shop.close()
remove_tree({dir}"cookbook_blocks.gsql")
