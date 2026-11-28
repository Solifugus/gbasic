' The analytics tasks in G-SQL: `build` loads both files into a database
' (rows in batches through db.insert, each a record of the file's text, which
' the columns convert); every other task opens it and runs one query.
' usage: gbasic gsqlbench.bas DBPATH ORDERS.csv CUSTOMERS.csv TASK
load gsql

database bench()

sql make on bench
    create table orders (id integer primary key, customer integer, region text, day date, qty integer, price number);
    create table customers (id integer primary key, segment text)
end sql

sql q_filter on bench
    select count(*) as n, sum(qty * price) as total from orders where region = 'West' and qty >= 10
end sql

sql q_group on bench
    select region, count(*) as n, sum(qty * price) as total from orders group by region order by region
end sql

sql q_bycust on bench
    select customer, sum(qty * price) as total from orders group by customer order by total desc, customer limit 5
end sql

sql q_top on bench
    select id from orders order by qty * price desc, id limit 10
end sql

sql q_join on bench
    select segment, sum(qty * price) as total from orders join customers on orders.customer = customers.id
    group by segment order by segment
end sql

sql q_month on bench
    select extract(year from day) as y, extract(month from day) as m, count(*) as n from orders
    group by extract(year from day), extract(month from day) order by y, m
end sql

function load_into(db, table, path)
    batch = []
    for each r in csv.records({file}path)
        append(batch, r)
        if len(batch) = 10000 then
            db.insert(table, batch)
            batch = []
        end if
    next
    if len(batch) > 0 then
        db.insert(table, batch)
    end if
end function

function pairs(rows, a, b)
    out = []
    for each r in rows
        append(out, string(r[a]) + ":" + string(r[b]))
    next
    return join(out, " ")
end function

program main(args)
    task = args[3]
    db = gsql.connect(args[0], { create: task = "build", linger: 0, wait: 600 })
    if task = "build" then
        db.exec(make)
        load_into(db, "orders", args[1])
        load_into(db, "customers", args[2])
        print string((db.query("select count(*) as n from orders")[0]).n) + " rows"
    else if task = "filter" then
        r = db.query(q_filter)[0]
        print string(r.n) + " " + string(r.total)
    else if task = "group" then
        out = []
        for each r in db.query(q_group)
            append(out, r.region + ":" + string(r.n) + ":" + string(r.total))
        next
        print join(out, " ")
    else if task = "bycust" then
        print pairs(db.query(q_bycust), "customer", "total")
    else if task = "top" then
        out = []
        for each r in db.query(q_top)
            append(out, string(r.id))
        next
        print join(out, " ")
    else if task = "join" then
        print pairs(db.query(q_join), "segment", "total")
    else if task = "month" then
        rows = db.query(q_month)
        print string(len(rows)) + " " + string(rows[0].n) + " " + string(rows[len(rows) - 1].n)
    end if
    db.close()
end program
