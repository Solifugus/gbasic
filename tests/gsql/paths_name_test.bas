' G-SQL paths: `db["name"]`, a table named by a value (docs/gsql_paths_design.md
' §3.1). The text in the brackets names the table `db.name` would, so a name a
' connection's attribute takes (`ef_search`, `max_recursion`), one that is no
' identifier, and one computed at run time are all reached. Every answer is
' written here by hand from the rows this file inserts, and each write is read
' back by SQL. Run by run_gsql_paths.sh with GSQL_DIR set.
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

' the message a case's refusal carries, or "no refusal"
' (not named `f`: a function of the parameter's name would be called instead)
function refusal(case_fn)
    on error goto next
    x = case_fn()
    e = error
    if not e then
        return "no refusal"
    end if
    return e.message
end function

dir = env("GSQL_DIR")
' wait: 600 -- valgrind on a slow machine; it is only how long a slow owner may take
db = gsql.connect(dir + "/names", { create: true, linger: 0, wait: 600 })
db.exec("create table customer (id integer primary key, name text, city text)")
db.exec("create table invoice (id integer primary key, customer integer references customer, total integer)")
db.exec("create table ef_search (id integer primary key, beam integer)")
db.exec("create table max_recursion (id integer primary key, depth integer)")
db.exec("create table \"select\" (id integer primary key, v text)")
db.exec("create table \"order lines\" (id integer primary key, v text)")
db.exec("insert into customer values (2, 'Bo', 'Oslo'), (1, 'Ana', 'Rome'), (3, 'Cy', 'Rome')")
db.exec("insert into invoice values (10, 1, 5), (11, 3, 7), (12, 1, 9)")
db.exec("insert into ef_search values (1, 40)")
db.exec("insert into max_recursion values (1, 7)")
db.exec("insert into \"select\" values (1, 's')")
db.exec("insert into \"order lines\" values (1, 'ol')")

function rows_of(text)
    return db.query(text)
end function

' --- the same path as a dot ---------------------------------------------------
check("a literal name, every row in key order", db["customer"].query().name, ["Ana", "Bo", "Cy"])
check("the same rows as the dot", db["customer"].query(), db.customer.query())
check("the same SQL as the dot", db["customer"].sql(), db.customer.sql())
check("a key after it: one row", (db["customer"][2]).name, "Bo")
check("a key no row has: unknown", type(db["customer"][9]), "unknown")
check("a filter after it", db["customer"][.city = "Rome"].query().id, [1, 3])
check("count", db["customer"].count(), 3)
n = 0
for each r in db["customer"]
    n = n + r.id
next
check("for each over it", n, 6)

' --- computed at run time -----------------------------------------------------
t = "cust" + "omer"
check("a name from an expression", db[t].query().id, [1, 2, 3])
check("a key on a computed name", (db[t][3]).city, "Rome")
totals = 0
for each name in ["customer", "invoice"]
    totals = totals + db[name].count()
next
check("each name in a list, in turn", totals, 6)
function first_name(conn, table)
    return (conn[table][1]).name
end function
check("a connection and a name passed to a function", first_name(db, "customer"), "Ana")
check("a name in upper case, folded as SQL stores it", db["CUSTOMER"].count(), 3)

' --- names a dot cannot reach -------------------------------------------------
check("db.ef_search is the attribute", db.ef_search, 40)
check("db[\"ef_search\"] is the table", db["ef_search"].query(), [{ id: 1, beam: 40 }])
check("db.max_recursion is the attribute, not 7", db.max_recursion = 7, false)
check("db[\"max_recursion\"] is the table", (db["max_recursion"][1]).depth, 7)
check("a reserved word", (db["select"][1]).v, "s")
check("a name with a space", db["order lines"].query().v, ["ol"])

' --- braces, and a step -------------------------------------------------------
check("braces straight after the bracket", db["customer"]{name}.query(), [{ name: "Ana" }, { name: "Bo" }, { name: "Cy" }])
check("braces after a filter", db["customer"][.id = 2]{name, city}.query(), [{ name: "Bo", city: "Oslo" }])
check("braces on an attribute's table", db["ef_search"]{beam}.query(), [{ beam: 40 }])
check("a step down from a named table", db["customer"][1].invoice.query().total, [5, 9])
check("a step up from a named table", (db["invoice"][11].customer).name, "Cy")

' --- writes -------------------------------------------------------------------
db["ef_search"][1] = { beam: 80 }
check("a write by key to an attribute's table", (rows_of("select beam from ef_search")[0]).beam, 80)
check("...and the attribute is untouched", db.ef_search, 40)
db[t][4] = { name: "Di", city: "Paris" }
check("a key no row has is inserted", (rows_of("select name from customer where id = 4")[0]).name, "Di")
r = db["order lines"].insert({ id: 2, v: "two" })
check("insert through a named table", r.v, "two")
check("...read back", rows_of("select v from \"order lines\" order by id").v, ["ol", "two"])
db["customer"][.city = "Paris"].delete()
check("delete through a named table", rows_of("select id from customer order by id").id, [1, 2, 3])

' --- refused, each beside its working neighbour above -------------------------
function by_number()
    return db[3]
end function
check("a number: tables have no order", refusal(by_number),
      "a connection's bracket takes a table's name, as in db[\"customer\"], not a number: tables have no order")
function by_number_braced()
    return db[3]{name}
end function
check("a number before braces too", refusal(by_number_braced),
      "a connection's bracket takes a table's name, as in db[\"customer\"], not a number: tables have no order")
function by_filter()
    return db[.name = "customer"]
end function
check("a filter is kept free", refusal(by_filter),
      "a connection's bracket names a table, as in db[\"customer\"]; a filter is written on the table, db[\"customer\"][.city = \"Rome\"]")
function by_list()
    return db["customer", "invoice"]
end function
check("two names", refusal(by_list),
      "a connection's bracket names one table, as in db[\"customer\"]; a key or filters follow it, db[\"line\"][17, 2]")
function by_empty()
    return db[""]
end function
check("an empty text", refusal(by_empty),
      "a connection's bracket takes a table's name, as in db[\"customer\"], and this text is empty")
function by_nul()
    return db["customer" + chr(0) + "x"]
end function
check("a NUL byte, never cut at it", refusal(by_nul),
      "a connection's bracket takes a table's name, as in db[\"customer\"], and this text holds a NUL byte, which no table's name can")
function by_nothing()
    return db[nothing]
end function
check("nothing", refusal(by_nothing),
      "a connection's bracket takes a table's name, as in db[\"customer\"], not nothing")
function by_unknown()
    return db[unknown]
end function
check("unknown", refusal(by_unknown),
      "a connection's bracket takes a table's name, as in db[\"customer\"], not unknown")
function by_record()
    return db[{ table: "customer" }]
end function
check("a record", refusal(by_record),
      "a connection's bracket takes a table's name, as in db[\"customer\"], not a record")
function misspelled()
    return db["custmer"].query()
end function
check("a table no database has, with the nearest name", refusal(misspelled),
      "path custmer: the database has no table `custmer` -- did you mean `customer`? [NOT_FOUND]")

db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
