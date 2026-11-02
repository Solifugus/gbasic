' Recipe 18: subtotals with ROLLUP -- one query answers each region's sales by
' product, each region's subtotal and the grand total, in the order a report
' reads them; GROUPING says which rows are totals.
load gsql

if exists({dir}"cookbook_rollup.gsql") then
    remove_tree({dir}"cookbook_rollup.gsql")
end if

database sales( file: "cookbook_rollup.gsql", create: true )

sql make on sales
    create table sale (id integer primary key, region text, product text, amount money(USD));
    insert into sale values
        (1, 'east', 'gripper', 1200.00), (2, 'east', 'joint', 450.00),
        (3, 'east', 'gripper', 800.00), (4, 'west', 'joint', 300.00),
        (5, 'west', 'motor', 975.50), (6, 'west', 'joint', 150.00)
end sql

' GROUP BY ROLLUP(region, product) groups by both, then by region alone, then
' not at all; a column a total leaves out reads NULL, and GROUPING(region,
' product) is 0 on a detail row, 1 on a region's subtotal and 3 on the total
sql report on sales
    select region, product, sum(amount) as total, count(*) as sales,
           grouping(region, product) as level
    from sale
    group by rollup(region, product)
end sql

make.exec()

for each r in report
    if r.level = 3 then
        print "Grand total            " + string(r.total) + "  (" + string(r.sales) + " sales)"
    else if r.level = 1 then
        print "  " + r.region + " subtotal        " + string(r.total)
    else
        print "  " + r.region + "  " + r.product + "  " + string(r.total)
    end if
next

sales.close()
remove_tree({dir}"cookbook_rollup.gsql")
