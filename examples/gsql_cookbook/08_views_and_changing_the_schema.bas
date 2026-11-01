' Recipe 8: views, and changing a table under them. A change is never
' blocked; what it did to a view is a warning, which a careful program can
' turn into an error and roll back.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "shop.gsql"), { create: true })
gsql.exec(db, "create table item (id integer primary key, name text, price money(USD))")
gsql.exec(db, "insert into item values (1, 'apple', cast('0.50' as money(USD))), (2, 'melon', cast('3.00' as money(USD)))")
gsql.exec(db, "create view cheap as select name from item where price < cast('1' as money(USD))")
print gsql.query(db, "select * from cheap")

' rebuild the table differently: build the new one, copy, drop, rename
gsql.exec(db, "create table item_new (id integer primary key, name text, price money(USD), stocked boolean default true)")
gsql.exec(db, "insert into item_new (id, name, price) select id, name, price from item")

function drop_old()
    on warning goto next
    gsql.exec(db, "drop table item")
    if warning then
        print "warned " + string(warning.code) + ": " + warning.message
    end if
    return nothing
end function
drop_old()                                        ' the view now reads nothing
gsql.exec(db, "alter table item_new rename to item")
print gsql.query(db, "select * from cheap")       ' and reads again, once item is back

' the careful version: under `on warning stop`, inside a transaction, the
' warning is an error before anything commits
function careful()
    on error goto next
    on warning stop
    gsql.begin(db)
    gsql.exec(db, "drop table item")
    if error then
        gsql.rollback(db)
        return "rolled back: " + string(error.code)
    end if
    gsql.commit(db)
    return "committed"
end function
print careful()
print gsql.query(db, "select * from cheap")

gsql.close(db)
remove_tree(work)
