' Recipe 8: views, and changing a table under them. A change is never
' blocked; what it did to a view is a warning, which a careful program can
' turn into an error and roll back.
load gsql

if exists({dir}"cookbook_views.gsql") then
    remove_tree({dir}"cookbook_views.gsql")
end if

database shop( file: "cookbook_views.gsql", create: true )

sql setup on shop
    create table item (id integer primary key, name text, price money(USD));
    insert into item values (1, 'apple', cast('0.50' as money(USD))), (2, 'melon', cast('3.00' as money(USD)));
    create view cheap as select name from item where price < cast('1' as money(USD))
end sql

sql read_cheap on shop
    select * from cheap
end sql

' rebuild the table differently: build the new one, copy, drop, rename
sql build_new on shop
    create table item_new (id integer primary key, name text, price money(USD), stocked boolean default true);
    insert into item_new (id, name, price) select id, name, price from item
end sql

sql drop_item on shop
    drop table item
end sql

sql rename_new on shop
    alter table item_new rename to item
end sql

setup.exec()
print read_cheap.query()

build_new.exec()

function drop_old()
    on warning goto next
    drop_item.exec()
    if warning then
        print "warned " + string(warning.code) + ": " + warning.message
    end if
    return nothing
end function
drop_old()                                        ' the view now reads nothing
rename_new.exec()
print read_cheap.query()                      ' and reads again, once item is back

' the careful version: under `on warning stop`, inside a transaction, the
' warning is an error before anything commits
function careful()
    on error goto next
    on warning stop
    shop.begin()
    drop_item.exec()
    if error then
        shop.rollback()
        return "rolled back: " + string(error.code)
    end if
    shop.commit()
    return "committed"
end function
print careful()
print read_cheap.query()

shop.close()
remove_tree({dir}"cookbook_views.gsql")
