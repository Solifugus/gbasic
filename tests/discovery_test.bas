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

zz = odbc.close(c)
print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
