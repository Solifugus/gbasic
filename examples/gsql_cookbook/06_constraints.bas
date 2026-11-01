' Recipe 6: let the database refuse bad rows -- NOT NULL, UNIQUE, CHECK and
' foreign keys -- and tell which rule said no by its code.
load gsql

if exists({dir}"cookbook_orders.gsql") then
    remove_tree({dir}"cookbook_orders.gsql")
end if

database shop( file: "cookbook_orders.gsql", create: true )

sql schema on shop
    create table customer (id integer primary key, email text not null unique);
    create table orders (id integer serial primary key,
                         customer integer references customer (id),
                         total money(USD) check (total > cast('0' as money(USD))));
    insert into customer values (1, 'ada@example.com')
end sql

sql good_order on shop
    insert into orders (customer, total) values (1, cast('25.00' as money(USD)))
end sql

sql no_email on shop
    insert into customer values (2, null)
end sql

sql same_email on shop
    insert into customer values (3, 'ada@example.com')
end sql

sql zero_total on shop
    insert into orders (customer, total) values (1, cast('0' as money(USD)))
end sql

sql no_such_customer on shop
    insert into orders (customer, total) values (9, cast('5' as money(USD)))
end sql

sql drop_ada on shop
    delete from customer where id = 1
end sql

sql plain_number on shop
    insert into orders (customer, total) values (1, 5)
end sql

sql order_count on shop
    select count(*) as n from orders
end sql

gsql.exec(schema)

function try(statement)
    on error goto next
    gsql.exec(statement)
    if error then
        print "refused  " + error.details.code
        return nothing
    end if
    print "accepted"
    return nothing
end function

try(good_order)
try(no_email)               ' NOT_NULL
try(same_email)             ' DUPLICATE
try(zero_total)             ' CHECK
try(no_such_customer)       ' FOREIGN_KEY: no customer 9
try(drop_ada)               ' FOREIGN_KEY: an order points at it
try(plain_number)           ' TYPE: a number is not money(USD)
print string(gsql.query(order_count)[0].n) + " order kept"

gsql.close(shop)
remove_tree({dir}"cookbook_orders.gsql")
