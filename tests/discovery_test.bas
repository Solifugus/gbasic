' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' discovery -- declared facts only. See docs/discovery_design.md.
'
' SELF-CHECKING rather than golden, and forced twice over. Every defect here
' produces A PLAUSIBLE CATALOG: an id missing its qualifier still looks like an
' id, a column list in the wrong order still looks like a column list. And the
' EXPECTED VALUES DIFFER BY DATABASE -- `sales.orders` on SQLite,
' `sales.gbasic_test.public.orders` on PostgreSQL -- so a golden could only
' ever have pinned one of the four.
'
' THE SAME FIXTURE RUNS AGAINST ALL FOUR, which is the point: every identity
' rule it asserts was MEASURED across four drivers, and each is invisible from
' any single one.

load discovery

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function table_id_for(cat, suffix)
    for each k in keys(cat.tables)
        if ends_with(k, suffix) then
            return k
        end if
    end for
    return ""
end function

c = odbc.connect(env("GBASIC_ODBC_CONNECTION"))
d1 = odbc.exec(c, "drop table if exists disc_orders")
d2 = odbc.exec(c, "drop table if exists disc_customers")
m1 = odbc.exec(c, "create table disc_customers (id integer primary key, name varchar(50) not null)")
m2 = odbc.exec(c, "create table disc_orders (id integer primary key, customer_id integer references disc_customers(id), total decimal(19,4))")

cat = discovery.scan(c, { source: "sales", table: "disc_%" })

print "-- what was declared is what came back"
check("the source is carried", cat.source, "sales")
check("both tables are found", count(keys(cat.tables)), 2)
orders = table_id_for(cat, "disc_orders")
custs = table_id_for(cat, "disc_customers")
check("the orders table has an id", len(orders) > 0, true)

print ""
print "-- IDENTITY: source leads, qualifiers are optional"
' THE LOAD-BEARING TIER. The qualifier is in TABLE_CAT on MariaDB, TABLE_SCHEM
' on PostgreSQL and SQL Server, and NEITHER on SQLite -- so an id built as
' `schema.table` yields `.disc_orders` on MariaDB, silently. Asserted
' PORTABLY: whatever the qualifiers are, the id must START with the source and
' END with the object, and the two tables must differ.
check("the id starts with the source", starts_with(orders, "sales."), true)
check("and ends with the table name", ends_with(orders, "disc_orders"), true)
check("the two tables have different ids", orders = custs, false)
' And an EMPTY qualifier must not leave a hole: `sales..disc_orders` is what a
' naive join produces on MariaDB and SQLite, and it is a different string from
' the one anything else will build.
check("no empty segment in the id", contains(orders, ".."), false)

print ""
print "-- columns, in ORDINAL order"
' Not the order keys() happened to yield: a column list whose order varies
' between reads cannot be compared to a later read, and comparing two reads is
' how "did the schema change" gets answered.
cols = discovery.columns_of(cat, orders)
names = []
for each col in cols
    append(names, col.column)
end for
check("every declared column is present", count(cols), 3)
check("in the order the table declares them", join(names, ","), "id,customer_id,total")
check("each column belongs to its table", starts_with(discovery.id_of(cols[0]), orders + "."), true)

' THE ORDERING IS ASSERTED AGAINST A HAND-BUILT CATALOG, not against the live
' read. MEASURED: removing the sort entirely left every live check passing,
' because the driver returns columns in ordinal order and `keys()` returns them
' in insertion order, so the two agree by accident on this data. An assertion
' that cannot fail is not an assertion -- so this one inserts them BACKWARDS
' and requires them to come back forwards.
hand = { source: "s", tables: {}, columns: {}, primary_keys: [], edges: [] }
hand.tables["s.t"] = { source: "s", catalog: "", schema: "", table: "t", column: "" }
for each nm, ix in ["third", "second", "first"]
    col = { source: "s", catalog: "", schema: "", table: "t", column: nm,
            position: 3 - ix, type_name: "int", data_type: 4, nullable: 1 }
    hand.columns["s.t." + nm] = col
end for
back = []
for each col in discovery.columns_of(hand, "s.t")
    append(back, col.column)
end for
check("columns come back in ordinal order, not insertion order", join(back, ","), "first,second,third")
check("and carries a type name the driver gave", len(cols[0].type_name) > 0, true)

print ""
print "-- the declared primary key"
check("the key column is recognised", discovery.is_primary_key(cat, discovery.id_of(cols[0])), true)
check("and a non-key column is not", discovery.is_primary_key(cat, discovery.id_of(cols[2])), false)

print ""
print "-- the declared foreign key, and it is the ONLY kind this increment emits"
check("one edge was found", count(cat.edges), 1)
check("it is marked enforced, because the database checks it", cat.edges[0].kind, "enforced")
check("from the column that holds it", ends_with(cat.edges[0].from, "disc_orders.customer_id"), true)
check("to the column it points at", ends_with(cat.edges[0].to, "disc_customers.id"), true)
' NOTHING IS INFERRED. `total` and `name` share no relationship, and a library
' that guessed would have emitted one; an inferred edge needs a null model and
' this increment has none, so the count above is the whole assertion.
check("and nothing was invented beyond it", count(cat.edges), 1)

print ""
print "-- an estate is several sources"
' The whole reason `source` is part of every identity: one organisation runs
' several databases, and an id that does not name the database cannot tell two
' `orders` tables apart. Simulated here with one connection read twice under
' different names, which is enough to prove the KEYING -- the ids must not
' collide even though the underlying tables are identical.
second = discovery.scan(c, { source: "finance", table: "disc_%" })
est = discovery.estate([cat, second])
check("both sources are recorded", join(est.sources, ","), "sales,finance")
check("and neither source's tables were lost to the other", count(keys(est.tables)), 4)
check("edges from both are kept", count(est.edges), 2)

on error goto next
discovery.estate([cat, cat])
check("two catalogs claiming one source name are refused", contains(error.message, "must identify one database"), true)
error.clear()
discovery.scan(c, {})
check("a scan without a source is refused", contains(error.message, "needs a `source`"), true)
error.clear()
discovery.scan(c, { source: "a.b" })
check("a source name containing the separator is refused", contains(error.message, "key separator"), true)
error.clear()
check("CONTROL: a legal source name scans", discovery.scan(c, { source: "ok", table: "disc_orders" }).source, "ok")

print ""
print "-- reading SQL for the objects it touches"
' PORTABLE AND DIALECT-FREE: these are pure string checks, and they are the
' bulk of the value. Each case exists because a plausible implementation gets
' it wrong -- a `from` inside a comment or a string literal, a CTE name, a
' subquery, a MERGE whose `update set` looks like a table called `set`.
function refs_of(sql, side)
    r = discovery.references(sql)
    return join(r[side], ",")
end function

check("a plain select reads its table", refs_of("select a from t1", "reads"), "t1")
check("insert..select reads and writes", refs_of("insert into tgt select * from src", "writes"), "tgt")
check("and the source is a read", refs_of("insert into tgt select * from src", "reads"), "src")
' DELETE FROM is the one place `from` does not mean a read.
check("delete from is a WRITE, not a read", refs_of("delete from gone where 1=1", "writes"), "gone")
check("and delete reads nothing", refs_of("delete from gone where 1=1", "reads"), "")
check("a join is a read", refs_of("select 1 from a join b on 1=1", "reads"), "a,b")
check("merge writes its target", refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "writes"), "d")
check("and USING is the source read", refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "reads"), "s")
' A KEYWORD IS NOT A NAME. Found by testing: MERGE's `update set d.x` emitted a
' table called `set`, and a phantom object in a lineage graph is reported with
' exactly the same confidence as a real one.
check("`set` is never mistaken for a table", contains(refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "writes"), "set"), false)
check("a CTE name is not a table", refs_of("with cte as (select * from real_t) select * from cte", "reads"), "real_t")
check("a subquery is not a table", refs_of("select * from (select id from inner_t) x", "reads"), "inner_t")
check("a comment is not SQL", refs_of("-- insert into ghost select * from nowhere" + chr(10) + "select 1 from real_t", "reads"), "real_t")
check("a string literal is not SQL", refs_of("select 'from ghost' from real_t", "reads"), "real_t")
check("delimited names survive", refs_of("select * from [Odd Name]", "reads"), "Odd Name")
check("a qualified name keeps its parts", refs_of("select * from a.b.c", "reads"), "a.b.c")
' THE GAP IS THE LOAD-BEARING OUTPUT. Dynamic SQL is a hop that CANNOT be read,
' and a tracer that drops it silently produces a lineage graph that is
' confidently incomplete -- worse than one naming its own holes.
check("dynamic SQL is reported as a gap", count(discovery.references("exec ('select * from ' + @t)").gaps) > 0, true)
check("and ordinary SQL reports none", count(discovery.references("select * from t").gaps), 0)

print ""
print "-- modules, and a traced chain"
' A TWO-HOP VIEW CHAIN, which every database has -- SQLite has no stored
' procedures at all, so a portable fixture cannot rest on them.
e1 = odbc.exec(c, "drop view if exists disc_v2")
e2 = odbc.exec(c, "drop view if exists disc_v1")
e3 = odbc.exec(c, "create view disc_v1 as select id, total from disc_orders")
e4 = odbc.exec(c, "create view disc_v2 as select id from disc_v1")
info = odbc.info(c)
cat2 = discovery.scan(c, { source: "s", table: "disc_%" })
mods = discovery.modules(c, { source: "s" })
tr = discovery.trace(cat2, mods, info.dbms_name)

function edge_between(tr, mod_suffix, kind, obj_suffix)
    for each e in tr.edges
        if ends_with(e.module, mod_suffix) and e.kind = kind and ends_with(e.object, obj_suffix) then
            return true
        end if
    end for
    return false
end function

check("the first view is recorded as reading its table", edge_between(tr, "disc_v1", "reads", "disc_orders"), true)
check("every edge names a module and an object", len(tr.edges[0].module) > 0 and len(tr.edges[0].object) > 0, true)
check("the dbms identified itself", len(info.dbms_name) > 0, true)

zz = odbc.close(c)
print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
