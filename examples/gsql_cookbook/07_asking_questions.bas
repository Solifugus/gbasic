' Recipe 7: the queries a report is made of -- joins, grouping, DISTINCT,
' subqueries and WITH.
load gsql

if exists({dir}"cookbook_sales.gsql") then
    remove_tree({dir}"cookbook_sales.gsql")
end if

database sales( file: "cookbook_sales.gsql", create: true )

sql setup on sales
    create table region (id integer primary key, name text);
    create table sale (id integer primary key, region integer, rep text, amount money(USD));
    insert into region values (1, 'North'), (2, 'South'), (3, 'West');
    insert into sale values
        (1, 1, 'kim', cast('120' as money(USD))), (2, 1, 'lee', cast('80' as money(USD))),
        (3, 2, 'kim', cast('200' as money(USD))), (4, 2, 'max', cast('50' as money(USD))),
        (5, 1, 'kim', cast('30' as money(USD)))
end sql

sql by_region on sales
    select r.name, count(s.id) as sales, coalesce(sum(s.amount), cast('0' as money(USD))) as total
    from region r left join sale s on s.region = r.id
    group by r.name
    order by r.name
end sql

sql roaming_reps on sales
    select rep from sale group by rep having count(distinct region) > 1
end sql

sql above_average on sales
    select id, rep, amount from sale s
    where amount > (select avg(amount) from sale t where t.region = s.region)
    order by id
end sql

sql above_average_in_steps on sales
    with avgs as (select region, avg(amount) as mean from sale group by region)
    select s.id from sale s join avgs a on a.region = s.region
    where s.amount > a.mean
    order by s.id
end sql

setup.exec()

print "-- totals by region, joined to its name, regions with nothing sold included"
for each row in by_region
    print row.name + "  " + string(row.sales) + "  " + string(row.total)
next

print "-- reps who sold in more than one region"
for each row in roaming_reps
    print row.rep
next

print "-- each sale above its region's average, by a correlated subquery"
for each row in above_average
    print string(row.id) + " " + row.rep + " " + string(row.amount)
next

print "-- the same question asked in steps, with WITH"
for each row in above_average_in_steps
    print row.id
next

sales.close()
remove_tree({dir}"cookbook_sales.gsql")
