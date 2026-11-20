' G-SQL paths, increment 1 (docs/gsql_paths_design.md §3): one table read by
' path -- a key, filters, a result run when it is used, count and sql. Every
' answer is written here by hand from the rows this file inserts, never read
' back from the path compiler. Run by run_gsql_paths.sh with GSQL_DIR set.
load gsql

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

' as print shows it: a record holding unknown cannot be compared with =
function check_text(label, got, want)
    return check(label, string(got), want)
end function

' the refusal a call makes, as { code, message, table, column }, or "no refusal"
function refusal(f)
    on error goto next
    x = f()
    e = error
    if not e then
        return "no refusal"
    end if
    d = e.details
    out = { message: e.message }
    if type(d) = "record" then
        if contains(keys(d), "code") then
            out.code = d.code
        end if
        if contains(keys(d), "table") then
            out.table = d.table
        end if
        if contains(keys(d), "column") then
            out.column = d.column
        end if
    end if
    return out
end function

function message_has(r, text)
    if type(r) != "record" then
        return false
    end if
    return contains(r.message, text)
end function

dir = env("GSQL_DIR")
' wait: 600 -- valgrind on a slow machine; it is only how long a slow owner may take
shop = gsql.connect(dir + "/paths", { create: true, linger: 0, wait: 600 })

' a database chosen at run time, so its block runs on `shop`
database store()
sql make on store
    create table customer (id integer primary key, name text not null, email text, city text, active boolean, credit integer, spent integer);
    create table line (invoice integer, n integer, item text, primary key (invoice, n));
    create table note (body text primary key)
end sql
shop.exec(make)

' ids inserted out of order: a path answers in primary key order (§3.3)
shop.exec("insert into customer values (3, 'Cy', 'cy@x', 'Rome', true, 100, 40)")
shop.exec("insert into customer values (1, 'Ana', 'ana@x', 'Rome', false, 50, 70)")
shop.exec("insert into customer values (4, 'Di', null, 'Paris', true, null, 10)")
shop.exec("insert into customer values (2, 'Bo', null, 'Oslo', true, 80, 80)")
shop.exec("insert into line values (18, 1, 'pad'), (17, 2, 'ink'), (17, 1, 'pen')")

' --- a key: one row, read where the path ends ------------------------------
c = shop.customer[2]
check("a key answers the row's record", type(c), "record")
check("the row's name", c.name, "Bo")
check("its id", c.id, 2)
check_text("the whole record, unknown kept", c, "{\"id\":2,\"name\":\"Bo\",\"email\":unknown,\"city\":\"Oslo\",\"active\":true,\"credit\":80,\"spent\":80}")
check("a key no row has answers unknown", type(shop.customer[99]), "unknown")
k = 3
check("a key from a variable", (shop.customer[k]).name, "Cy")
check("a key from an expression", (shop.customer[k - 2]).name, "Ana")
check("parentheses end the path: the field of the row", (shop.customer[4]).city, "Paris")
check("a composite key, in the key's order", (shop.line[17, 2]).item, "ink")
check("the other row of that invoice", (shop.line[17, 1]).item, "pen")
check("a composite key no row has", type(shop.line[17, 3]), "unknown")

function got_record(r)
    return r
end function
check("a key read before a function sees it", (got_record(shop.customer[1])).name, "Ana")

' --- many: a result, in primary key order ----------------------------------
all = shop.customer.query()
check("every row", len(all), 4)
check("primary key order, not the order inserted", all.id, [1, 2, 3, 4])
check("count of the table", shop.customer.count(), 4)
names = []
for each r in shop.customer
    names = append(names, r.name)
next
check("for each reads it in key order", names, ["Ana", "Bo", "Cy", "Di"])
check("the composite table in key order", shop.line.query().item, ["pen", "ink", "pad"])

' --- filters -----------------------------------------------------------------
check("=", shop.customer[.city = "Rome"].query().id, [1, 3])
check("!=", shop.customer[.city != "Rome"].query().id, [2, 4])
check("<", shop.customer[.id < 3].query().id, [1, 2])
check(">", shop.customer[.id > 3].query().id, [4])
check("<=", shop.customer[.id <= 1].query().id, [1])
check(">=", shop.customer[.id >= 3].query().id, [3, 4])
check("a comma is and", shop.customer[.city = "Rome", .active = true].query().id, [3])
check("and", shop.customer[.city = "Rome" and .active = true].query().id, [3])
check("or", shop.customer[.city = "Oslo" or .city = "Paris"].query().id, [2, 4])
check("not", shop.customer[not (.city = "Rome")].query().id, [2, 4])
check("parentheses group", shop.customer[(.city = "Rome" or .city = "Oslo"), .active = true].query().id, [2, 3])
check("a boolean column alone", shop.customer[.active].query().id, [2, 3, 4])
check("a column against a column", shop.customer[.spent < .credit].query().id, [3])
check("arithmetic on a column", shop.customer[.credit - .spent = 0].query().id, [2])
check("arithmetic: * and /", shop.customer[.id * 2 = 6, .credit / 2 = 50].query().id, [3])
check("negation", shop.customer[-.id = -4].query().id, [4])
wanted = "Oslo"
check("the right side is the program's: a variable", shop.customer[.city = wanted].query().id, [2])
check("the right side is the program's: a call", shop.customer[.name = upper("bo")].query().id, [])
check("...and its answer", shop.customer[.name = left("Bob", 2)].query().id, [2])
check("several brackets are and", shop.customer[.city = "Rome"][.active = false].query().id, [1])

' unknown is SQL's NULL, three-valued: `= unknown` matches nothing, is_unknown matches
check("= unknown matches no row", shop.customer[.email = unknown].count(), 0)
check("is_unknown() finds the rows without one", shop.customer[is_unknown(.email)].query().id, [2, 4])
check("not is_unknown()", shop.customer[not is_unknown(.email)].query().id, [1, 3])
check("an unknown credit is not > 0", shop.customer[.credit > 0].query().id, [1, 2, 3])

check("a filter after a key is still one row", (shop.customer[3][.active = true]).name, "Cy")
check("...and none when it fails", type(shop.customer[1][.active = true]), "unknown")
check("part of a composite key is a filter", shop.line[.invoice = 17].query().item, ["pen", "ink"])

' --- a result runs when it is used, each time --------------------------------
rome = shop.customer[.city = "Rome"]
check("a result before the insert", rome.count(), 2)
shop.exec("insert into customer values (5, 'Ed', 'ed@x', 'Rome', false, 0, 0)")
check("the same result after it, read again", rome.count(), 3)
check("a result goes on being a path", rome[.active = false].query().id, [1, 5])
' printed, a result says what it walks and that it has not run (the
' 2026-11-20 sweep: it printed `<gsql_statement>`)
check("a result printed says it is an unrun path", string(rome),
      "<gsql path customer, not run yet: for each, .query() or count runs it, .sql() shows its SQL>")
t = shop.customer
check("t[3] is customer 3, not the table's fourth row", (t[3]).name, "Cy")
check("count of a stored table path", t.count(), 5)

' --- inside a transaction: the path is a statement in it ---------------------
shop.begin()
shop.exec("insert into customer values (6, 'Fa', null, 'Rome', true, 1, 1)")
check("a path sees the transaction's own row", (shop.customer[6]).name, "Fa")
shop.rollback()
check("and not once it is rolled back", type(shop.customer[6]), "unknown")

' --- values are parameters, never SQL ---------------------------------------
odd = ["it's", "a;b", "-- not a comment", ":name", "$1", "\"quoted\"", "x'); drop table customer; --"]
for each body in odd
    shop.exec("insert into note values ($1)", [body])
next
for each body in odd
    check("read back by path: " + body, (shop.note[body]).body, body)
    check("found by filter: " + body, shop.note[.body = body].count(), 1)
next
check("the table is still there", shop.customer.count(), 5)

' --- what a path is ---------------------------------------------------------
check("sql(): a key", shop.customer[3].sql(), "SELECT * FROM \"customer\" WHERE \"id\" = $1 ORDER BY \"id\"\n$1 = 3")
check("sql(): a filter", shop.customer[.city = "Rome", .credit > 10].sql(), "SELECT * FROM \"customer\" WHERE ((\"city\" = $1) AND (\"credit\" > $2)) ORDER BY \"id\"\n$1 = \"Rome\"\n$2 = 10")
check("sql(): a composite key", shop.line[17, 2].sql(), "SELECT * FROM \"line\" WHERE \"invoice\" = $1 AND \"n\" = $2 ORDER BY \"invoice\", \"n\"\n$1 = 17\n$2 = 2")
check("sql(): is_unknown and not", shop.customer[not is_unknown(.email)].sql(), "SELECT * FROM \"customer\" WHERE (NOT (\"email\" IS NULL)) ORDER BY \"id\"")
check("sql(): the whole table", shop.customer.sql(), "SELECT * FROM \"customer\" ORDER BY \"id\"")
check("sql() runs nothing: a value that would refuse", shop.customer[.credit / 0 = 1].sql(), "SELECT * FROM \"customer\" WHERE ((\"credit\" / $1) = $2) ORDER BY \"id\"\n$1 = 0\n$2 = 1")
check("gsql.sql(path) is the method", gsql.sql(shop.customer), "SELECT * FROM \"customer\" ORDER BY \"id\"")
check("gsql.count(path) is the method", gsql.count(shop.customer[.active]), 3)
check("names are matched as SQL folds them", (shop.Customer[2]).name, "Bo")

' --- refusals, each beside the path that is allowed --------------------------
function bad_table()
    return shop.custmer[1]
end function
r = refusal(bad_table)
check("a table that is not there: NOT_FOUND", r.code, "NOT_FOUND")
check("...naming it", r.table, "custmer")
check("...with the nearest", message_has(r, "did you mean `customer`?"), true)

function bad_column()
    return shop.customer[.emial = 1].query()
end function
r = refusal(bad_column)
check("a column that is not there: NOT_FOUND", r.code, "NOT_FOUND")
check("...naming the column", r.column, "emial")
check("...with the nearest", message_has(r, "did you mean `email`?"), true)

function key_count()
    return shop.customer[1, 2]
end function
r = refusal(key_count)
check("two values for a one-column key", message_has(r, "primary key is 1 column (id), and the path gave 2 values"), true)
function key_short()
    return shop.line[17]
end function
r = refusal(key_short)
check("one value for a two-column key", message_has(r, "primary key is 2 columns (invoice, n), and the path gave 1 value"), true)

function mixed()
    return shop.customer[3, .id = 1]
end function
check("a key and a filter in one bracket", message_has(refusal(mixed), "this one mixes the two"), true)

function two_keys()
    return shop.customer[3][4]
end function
check("a second key", message_has(refusal(two_keys), "already names one row by its key"), true)

function called()
    return shop.customer[len(.name) > 2].query()
end function
check("a column given to a function", message_has(refusal(called), "anything more is a sql block's"), true)

function column_alone()
    return .name
end function
check("a column outside a path", message_has(refusal(column_alone), "`.name` names a row's field"), true)

function array_list()
    a = [1, 2, 3]
    return a[0, 1]
end function
check("a list in an array's brackets", message_has(refusal(array_list), "an array or a record takes one index"), true)

function stepped()
    return shop.customer[3].name
end function
check("a dot after a key is a step to a table", message_has(refusal(stepped), "has no table `name`"), true)

function counted_block()
    return make.count()
end function
check("count() is a path's", message_has(refusal(counted_block), "counts a path's rows"), true)
check("sql() of a block is its text", contains(make.sql(), "create table note"), true)

function role_path()
    return store.customer[1]
end function
check("a database chosen at run time has no tables", message_has(refusal(role_path), "start the path from the connection"), true)

check("the attribute is still the attribute", shop.max_recursion > 0, true)

shop.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
