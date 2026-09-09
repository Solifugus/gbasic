' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' odbc.tables / columns / primary_keys / foreign_keys -- what the database says
' about itself, read through the DRIVER MANAGER rather than through a dialect
' matrix of `information_schema` queries.
'
' WHY THAT WAY. A discovery layer is pointed at a customer's estate, and ONE
' ORGANISATION OFTEN RUNS SEVERAL DATABASES AT ONCE -- so a catalog read has to
' work against all of them or the estate cannot be described at all. ODBC
' already normalises these four calls across every database with a driver,
' which is the argument this module was built on.
'
' SELF-CHECKING rather than golden, and forced: every defect here produces a
' PLAUSIBLE CATALOG. A filter silently ignored returns every column in the
' database instead of one table's, which still looks exactly like a catalog; a
' golden would record it as expected and defend it.

load odbc

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

function names_of(rows, field)
    out = []
    for each r in rows
        append(out, string(r[field]))
    end for
    return join(out, ",")
end function

c = odbc.connect(env("GBASIC_ODBC_CONNECTION"))
d1 = odbc.exec(c, "drop table if exists cat_orders")
d2 = odbc.exec(c, "drop table if exists cat_customers")
m1 = odbc.exec(c, "create table cat_customers (id integer primary key, name varchar(50) not null)")
m2 = odbc.exec(c, "create table cat_orders (id integer primary key, customer_id integer references cat_customers(id), total decimal(19,4))")

print "-- tables"
all_tables = names_of(odbc.tables(c, { table: "cat_%" }), "TABLE_NAME")
check("both created tables are listed", contains(all_tables, "cat_customers") and contains(all_tables, "cat_orders"), true)

print ""
print "-- columns, and THE FILTER TIER"
' THE LOAD-BEARING CHECK. An absent option must mean ANY and a supplied one
' must NARROW -- asserted as a DIFFERENCE, because a filter that is silently
' ignored returns a longer list that still looks like a catalog. Checking only
' "the columns of cat_orders are present" passes on a build that returns every
' column in the database.
ord_cols = odbc.columns(c, { table: "cat_orders" })
check("the filtered read names exactly that table's columns",
      names_of(ord_cols, "COLUMN_NAME"), "id,customer_id,total")
cust_cols = odbc.columns(c, { table: "cat_customers" })
check("and a different table gives different columns",
      names_of(cust_cols, "COLUMN_NAME"), "id,name")
check("so the filter NARROWS rather than being ignored",
      count(ord_cols) != count(odbc.columns(c, { table: "cat_%" })), true)
check("every column reports its table", ord_cols[0].TABLE_NAME, "cat_orders")
check("and its ordinal position", ord_cols[0].ORDINAL_POSITION, 1)
check("a column knows a type name", is_string(ord_cols[2].TYPE_NAME), true)

print ""
print "-- primary keys"
pk = odbc.primary_keys(c, { table: "cat_customers" })
check("the primary key column is reported", names_of(pk, "COLUMN_NAME"), "id")

print ""
print "-- foreign keys, BOTH DIRECTIONS"
' The two directions are different questions and a library conflating them
' would answer one of them wrongly and plausibly.
out_fk = odbc.foreign_keys(c, { table: "cat_orders" })
check("the key defined ON cat_orders is found", count(out_fk) >= 1, true)
check("naming the column that holds it", out_fk[0].FKCOLUMN_NAME, "customer_id")
check("and what it points at", string(out_fk[0].PKTABLE_NAME) + "." + string(out_fk[0].PKCOLUMN_NAME), "cat_customers.id")
in_fk = odbc.foreign_keys(c, { referenced_table: "cat_customers" })
check("the keys POINTING AT cat_customers are found the other way", count(in_fk) >= 1, true)
check("and it is the same edge", in_fk[0].FKTABLE_NAME, "cat_orders")

print ""
print "-- refusals, each beside its nearest legal neighbour"
on error goto next
odbc.columns(c, { tabel: "cat_orders" })
check("a misspelled option is refused BY NAME", contains(error.message, "no option 'tabel'"), true)
check("and lists what it does take", contains(error.message, "table"), true)
error.clear()
odbc.primary_keys(c, {})
check("primary_keys without a table is refused", contains(error.message, "`table` is required"), true)
check("and says why it cannot be a pattern", contains(error.message, "one table at a time"), true)
error.clear()
odbc.foreign_keys(c, {})
check("foreign_keys with neither side is refused", contains(error.message, "neither was supplied"), true)
error.clear()
odbc.columns(c, { table: 7 })
check("a non-string option is refused", contains(error.message, "must be a string"), true)
error.clear()
odbc.tables(c, "not a record")
check("options must be a record", contains(error.message, "must be a record"), true)
error.clear()
' CONTROLS: without these, "it refuses" is satisfied by refusing everything.
check("CONTROL: no options at all is legal", count(odbc.tables(c)) > 0, true)
check("CONTROL: the same option spelled right works", count(odbc.columns(c, { table: "cat_orders" })), 3)

zz = odbc.close(c)
odbc.tables(c)
check("a closed connection is refused", contains(error.message, "closed"), true)
error.clear()

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
