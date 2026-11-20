' G-SQL paths, increment 4: COLUMNS IN BRACES (docs/gsql_paths_design.md
' §3.5): `path{ name, email }` reads those columns; braces before a step make
' a join, one row per row reached, carrying every braced table's columns.
' Every answer is written here by hand from the rows this file inserts. The
' last section holds the forms the brace already had -- a modifier clause, an
' inline modifier, a comparison's lens, a record -- to what they were.
' Run by run_gsql_paths.sh with GSQL_DIR set.
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
shop = gsql.connect(dir + "/braces", { create: true, linger: 0, wait: 600 })
shop.exec("create table customer (id integer primary key, name text, phone text, city text)")
shop.exec("create table purchases (id integer primary key, customer integer references customer, item text, price integer)")
shop.exec("create table piece (purchase integer references purchases, n integer, part text, primary key (purchase, n))")
shop.exec("insert into customer values (1, 'Ana', '555', 'Rome'), (2, 'Bo', '666', 'Oslo'), (3, 'Cy', '777', 'Rome')")
shop.exec("insert into purchases values (11, 3, 'ink', 5), (10, 3, 'pen', 2), (12, 1, 'pad', 3), (13, null, 'cap', 1)")
shop.exec("insert into piece values (10, 1, 'nib'), (10, 2, 'barrel'), (12, 1, 'paper')")

' --- one table ---------------------------------------------------------------
check("a key's chosen columns, as a record", shop.customer[3]{ name, phone }, { name: "Cy", phone: "777" })
check("the order the braces give", keys(shop.customer[3]{ phone, name }), ["phone", "name"])
check("one column, its field read after the parentheses", (shop.customer[3]{ name }).name, "Cy")
check("a key not there", type(shop.customer[9]{ name }), "unknown")
check("many: each row's columns", shop.customer[.city = "Rome"]{ name }.query(), [{ name: "Ana" }, { name: "Cy" }])
check("a filter after the braces", shop.customer{ name }[.city = "Oslo"].query(), [{ name: "Bo" }])
check("count with braces", shop.customer{ name }.count(), 3)

' --- braces before a step: a join (Matthew's example) ------------------------
check("a customer and each purchase", shop.customer[3]{ id, name, phone }.purchases{ item, price }.query(), [{ id: 3, name: "Cy", phone: "777", item: "pen", price: 2 }, { id: 3, name: "Cy", phone: "777", item: "ink", price: 5 }])
check("no purchases, no rows: an inner join", shop.customer[2]{ name }.purchases{ item }.query(), [])
check("many customers, each purchase with its customer", shop.customer[.city = "Rome"]{ name }.purchases{ item }.query(), [{ name: "Cy", item: "pen" }, { name: "Cy", item: "ink" }, { name: "Ana", item: "pad" }])
check("a step up from braces: one row per child", shop.purchases[.price > 2]{ item }.customer{ name }.query(), [{ item: "pad", name: "Ana" }, { item: "ink", name: "Cy" }])
check("a child whose key is unknown joins nothing", type(shop.purchases[13]{ item }.customer{ name }), "unknown")
check("one child up to its parent is one row", shop.purchases[11]{ item }.customer{ name }, { item: "ink", name: "Cy" })
check("a table passed through gives nothing", shop.customer[3]{ name }.purchases.piece{ part }.query(), [{ name: "Cy", part: "nib" }, { name: "Cy", part: "barrel" }])
check("braces on the last table only: each row once", shop.customer[.city = "Rome"].purchases{ item }.query(), [{ item: "pen" }, { item: "ink" }, { item: "pad" }])
check("count over a join", shop.customer[.city = "Rome"]{ name }.purchases{ item }.count(), 3)
check("a join's filters on each table", shop.customer{ name }[.city = "Rome"].purchases{ item }[.price < 4].query(), [{ name: "Cy", item: "pen" }, { name: "Ana", item: "pad" }])
check("sql(): the join", shop.customer[3]{ name }.purchases{ item }.sql(), "SELECT \"t0\".\"name\" AS \"name\", \"t1\".\"item\" AS \"item\" FROM \"customer\" AS \"t0\" JOIN \"purchases\" AS \"t1\" ON \"t1\".\"customer\" = \"t0\".\"id\" WHERE \"t0\".\"id\" = $1 ORDER BY \"t1\".\"id\", \"t0\".\"id\"\n$1 = 3")
check("sql(): one table", shop.customer[3]{ name }.sql(), "SELECT \"name\" AS \"name\" FROM \"customer\" WHERE \"id\" = $1 ORDER BY \"id\"\n$1 = 3")

' --- refusals ------------------------------------------------------------------
function same_name()
    return shop.customer{ id }.purchases{ id }.query()
end function
check("two tables giving one name", message_has(refusal(same_name), "`customer` and `purchases` both give `id` -- leave one out"), true)
function twice()
    return shop.customer{ name, name }.query()
end function
check("a name twice", message_has(refusal(twice), "`name` is named twice"), true)
function misspelt()
    return shop.customer{ nmae }.query()
end function
r = refusal(misspelt)
check("a column that is not there", message_has(r, "did you mean `name`?"), true)
function braces_twice()
    return shop.customer{ name }[.city = "Rome"]{ phone }.query()
end function
check("braces twice on one table", message_has(refusal(braces_twice), "already chosen"), true)
function write_braced()
    shop.customer[3]{ name } = { name: "X" }
    return nothing
end function
' `{ name } =` is a modifier clause by its shape (the `=` after the group), so
' a path refuses it as one
check("a write through braces", message_has(refusal(write_braced), "with no modifier clause"), true)
function delete_braced()
    return shop.customer[3]{ name }.delete()
end function
check("a delete through braces", message_has(refusal(delete_braced), "a path with braces chooses columns to read"), true)
' BRACES ON PLAIN DATA choose a record's fields since 2026-11-20 (the same
' spelling, run_projection's braces tier), so a record is no longer refused;
' a value with no fields still is
rec = { name: "Ana", city: "Rome" }
check("braces on a record choose its fields", rec{ name }, { name: "Ana" })
function on_a_number()
    n = 5
    return n{ name }
end function
check("braces on a number", message_has(refusal(on_a_number), "expects a record"), true)

' --- what the brace already meant ----------------------------------------------
d {date}= "2026-03-04"
check("a modifier clause after a name", string(d), string({date}"2026-03-04"))
parts = []
parts {split ","}= "a,b"
check("a clause with arguments after a name", parts, ["a", "b"])
function priced()
    return {USD}"10.00"
end function
check("an inline modifier after a keyword", string(priced()), string({USD}"10.00"))
word = "ab"
word{upper} += "cd"
check("a compound clause after a name, no space before the brace", word, "ABCD")
k = [1, 2]
k[0] {number}= "7"
check("a clause after a bracket", k[0], 7)
r = { a: 1, b: { c: 2 } }
check("a record literal", r.b.c, 2)

shop.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
