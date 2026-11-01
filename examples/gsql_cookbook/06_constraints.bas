' Recipe 6: let the database refuse bad rows -- NOT NULL, UNIQUE, CHECK and
' foreign keys -- and tell which rule said no by its code.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "orders.gsql"), { create: true })
gsql.exec(db, "create table customer (id integer primary key, email text not null unique)")
gsql.exec(db, "create table orders (id integer serial primary key, " +
              "customer integer references customer (id), " +
              "total money(USD) check (total > cast('0' as money(USD))))")
gsql.exec(db, "insert into customer values (1, 'ada@example.com')")

function try(sql)
    on error goto next
    gsql.exec(db, sql)
    if error then
        print "refused  " + error.details.code
        return nothing
    end if
    print "accepted"
    return nothing
end function

try("insert into orders (customer, total) values (1, cast('25.00' as money(USD)))")
try("insert into customer values (2, null)")                         ' NOT_NULL
try("insert into customer values (3, 'ada@example.com')")            ' DUPLICATE
try("insert into orders (customer, total) values (1, cast('0' as money(USD)))")   ' CHECK
try("insert into orders (customer, total) values (9, cast('5' as money(USD)))")   ' FOREIGN_KEY: no customer 9
try("delete from customer where id = 1")                             ' FOREIGN_KEY: an order points at it
try("insert into orders (customer, total) values (1, 5)")            ' TYPE: a number is not money(USD)
print string(gsql.query(db, "select count(*) as n from orders")[0].n) + " order kept"

gsql.close(db)
remove_tree(work)
