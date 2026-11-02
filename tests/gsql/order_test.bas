' JOIN ORDER BY ROW COUNTS (docs/gsql_rowcount_design.md). Self-checking:
' each case asks EXPLAIN which table a join reads first and how it reaches the
' next, against an order written here by hand; the estimate against the true
' count. The time a reordered join saves is sql_test's `order cost` tier,
' since gBASIC's clock stops at seconds.
load gsql

function check(what, got, want)
    state.checks = state.checks + 1
    if string(got) = string(want) then
        print "ok " + what
    else
        state.mismatches = state.mismatches + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' the access lines of a plan, after its "Nested Loop" head
function plan(db, q)
    out = []
    for each l in db.query("explain " + q)
        append(out, trim(l.plan))
    next
    return out
end function

' the number in "(a table of about N rows)", or -1
function about(line)
    at = find(line, "about ")
    if is_unknown(at) then
        return -1
    end if
    rest = mid(line, at + 6, len(line))
    return number(left(rest, find(rest, " ")))
end function

program main(args)
    state = { checks: 0, mismatches: 0 }
    db = gsql.connect(args[0], { create: true, linger: 0 })
    r = db.exec("create table c (id integer primary key, name text)")
    r = db.exec("create table o (oid integer primary key, cust integer, amt integer)")
    r = db.exec("create index o_cust on o (cust)")
    r = db.exec("create table big (k integer, v integer)")
    r = db.exec("create table small (k integer, w integer)")
    b = db.begin()
    for i = 1 to 200
        r = db.exec("insert into c values ($1, $2)", [i, "c" + string(i)])
    next
    for i = 1 to 20000
        r = db.exec("insert into o values ($1, $2, $3)", [i, mod(i, 200) + 1, i])
    next
    for i = 1 to 3000
        r = db.exec("insert into big values ($1, $2)", [mod(i, 30), i])
    next
    for i = 1 to 30
        r = db.exec("insert into small values ($1, $2)", [i, i])
    next
    b = db.commit()

    ' written large-first with an index on the join column: the small table
    ' outer, the large one sought
    p = plan(db, "select c.name, o.amt from o join c on o.cust = c.id")
    check("a chosen order says so", p[0], "Nested Loop (order by estimated rows)")
    check("the small table is read first", starts_with(p[1], "Scan on c "), true)
    check("the large one is sought by its index", starts_with(p[2], "Index Seek on o using o_cust"), true)
    check("the estimate of the small table", about(p[1]), 200)
    check("the estimate of the large table", about(p[2]), 20000)

    ' two tables with no index: the larger outer, the SMALLER hashed
    p = plan(db, "select * from small join big on big.k = small.k")
    check("unindexed: the larger table is read first", starts_with(p[1], "Scan on big "), true)
    check("unindexed: the smaller is the one hashed", starts_with(p[2], "Hash Scan on small"), true)
    p = plan(db, "select * from big join small on small.k = big.k")
    check("unindexed, written well: FROM's order kept", p[0], "Nested Loop")
    check("unindexed, written well: the smaller hashed", starts_with(p[2], "Hash Scan on small"), true)

    ' written well already: run as written
    p = plan(db, "select c.name, o.amt from c join o on o.cust = c.id")
    check("written well: FROM's order, no word of estimates", p[0], "Nested Loop")

    ' a LEFT join and a derived table keep FROM's order
    p = plan(db, "select c.name, o.amt from o left join c on o.cust = c.id")
    check("a LEFT join keeps FROM's order", starts_with(p[1], "Scan on o"), true)
    p = plan(db, "select * from (select * from o) d join c on d.cust = c.id")
    check("a derived table keeps FROM's order", starts_with(p[1], "Derived Scan on d"), true)

    ' the same rows either way
    q1 = "select o.oid, c.name from o join c on o.cust = c.id order by o.oid"
    q2 = "select o.oid, c.name from o left join c on o.cust = c.id order by o.oid"
    check("reordered and FROM's order answer alike", string(db.query(q1)) = string(db.query(q2)), true)

    c = db.close()
    print "ok " + string(state.checks) + " checks, " + string(state.mismatches) + " mismatches"
end program
