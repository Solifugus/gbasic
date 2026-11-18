' G-SQL paths, increment 3: WRITING by path (docs/gsql_paths_design.md §4):
' a record assigned to a path, insert and delete, through steps too. Every
' answer is written here by hand from the rows this file writes, and every
' write is read back by SQL, never by a path, so the two cannot share a
' mistake. Run by run_gsql_paths.sh with GSQL_DIR set.
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

function rows_of(sqltext)
    return shop.query(sqltext)
end function

dir = env("GSQL_DIR")
' wait: 600 -- valgrind on a slow machine; it is only how long a slow owner may take
shop = gsql.connect(dir + "/writes", { create: true, linger: 0, wait: 600 })
shop.exec("create table customer (id integer serial primary key, name text not null default 'anon', city text, credit integer)")
shop.exec("create table invoice (id integer serial primary key, customer integer references customer on delete cascade, total integer default 7, paid boolean default false)")
shop.exec("create table line (invoice integer references invoice on delete cascade, n integer, item text, primary key (invoice, n))")

' --- insert ------------------------------------------------------------------
r = shop.customer.insert({ name: "Ana", city: "Rome" })
check("insert answers the row as stored", type(r), "record")
check("...its serial key", r.id, 1)
check("...the value given", r.city, "Rome")
r = shop.customer.insert([{ name: "Bo" }, { city: "Oslo", credit: "25" }])
check("an array inserts several and answers them", rows_of("select id, name from customer where id > 1 order by id").name, ["Bo", "anon"])
check("a column a row leaves out takes its default", r[1].name, "anon")
check("text into an integer column converts as INSERT does", (rows_of("select credit from customer where id = 3")[0]).credit, 25)
function bad_rows()
    return shop.customer.insert([{ name: "Cy" }, { name: unknown }])
end function
r = refusal(bad_rows)
check("one refused row refuses the insert", r.code, "NOT_NULL")
check("...and none of its rows were kept", len(rows_of("select id from customer")), 3)
function misspelt_insert()
    return shop.customer.insert({ nmae: "Di" })
end function
r = refusal(misspelt_insert)
check("a column that is not there", r.column, "nmae")
check("...with the nearest", message_has(r, "did you mean `name`?"), true)
function insert_keyed()
    return shop.customer[1].insert({ name: "Di" })
end function
check("insert through a key is refused", message_has(refusal(insert_keyed), "insert adds rows to a table"), true)

' --- a record assigned to a key -----------------------------------------------
shop.customer[1] = { city: "Paris" }
got = rows_of("select name, city, credit from customer where id = 1")[0]
check("the fields given change", got.city, "Paris")
check("...the rest stay", got.name, "Ana")
shop.customer[20] = { name: "Twenty" }
got = rows_of("select id, name, city from customer where id = 20")
check("a key that is not there is inserted", len(got), 1)
check("...by INSERT's rules", (got[0]).name, "Twenty")
r = shop.customer.insert({ name: "Next" })
check("a serial key supplied moves the counter up", r.id, 21)
shop.customer[25] = { city: "Nowhere" }
check("...a column left out takes its default", (rows_of("select name from customer where id = 25")[0]).name, "anon")
shop.exec("delete from customer where id = 25")
function missing_required()
    shop.customer[30] = { name: unknown }
    return nothing
end function
r = refusal(missing_required)
check("...and is refused as an INSERT would be", r.code, "NOT_NULL")
check("...writing nothing", len(rows_of("select id from customer where id = 30")), 0)
function changes_key()
    shop.customer[1] = { id: 2, city: "X" }
    return nothing
end function
r = refusal(changes_key)
check("a record naming the key is refused", message_has(r, "`id` is `customer`'s key"), true)
check("...and wrote nothing", (rows_of("select city from customer where id = 1")[0]).city, "Paris")
function not_a_record()
    shop.customer[1] = "Paris"
    return nothing
end function
check("a value that is not a record", message_has(refusal(not_a_record), "with a record of its columns"), true)
function empty_record()
    shop.customer[1] = {}
    return nothing
end function
check("a record naming nothing", message_has(refusal(empty_record), "names a column of `customer`"), true)
function compound()
    shop.customer[1] += { city: "X" }
    return nothing
end function
check("a compound assignment", message_has(refusal(compound), "not a compound assignment"), true)

' --- a record assigned to many ----------------------------------------------
shop.customer[.city = "Rome"] = { credit: 5 }
check("a filter writes every row it names", rows_of("select id from customer where credit = 5 order by id").id, [])
shop.customer[is_unknown(.city)] = { credit: 5 }
check("...here three", rows_of("select id from customer where credit = 5 order by id").id, [2, 20, 21])
shop.customer[.city = "Atlantis"] = { credit: 9 }
check("a filter naming no row writes nothing and is not refused", len(rows_of("select id from customer where credit = 9")), 0)

' --- through steps ------------------------------------------------------------
r = shop.customer[1].invoice.insert({ total: 100 })
check("insert through a step fills the key from the path", r.customer, 1)
check("...and answers the row", r.total, 100)
r = shop.customer[2].invoice.insert([{ total: 1 }, { total: 2 }])
check("several through a step", rows_of("select total from invoice where customer = 2 order by id").total, [1, 2])
function names_fk()
    return shop.customer[1].invoice.insert({ customer: 2, total: 3 })
end function
check("a record naming the key the step fills", message_has(refusal(names_fk), "`customer` comes from the path"), true)
function from_many()
    return shop.customer[.city = "Paris"].invoice.insert({ total: 3 })
end function
check("a step from many inserts nothing", message_has(refusal(from_many), "a step from many rows of `customer` inserts nothing"), true)
function step_up()
    return shop.invoice[1].customer.insert({ name: "Up" })
end function
check("a step up inserts nothing", message_has(refusal(step_up), "inserts nothing: insert into `customer` itself"), true)
function no_parent()
    return shop.customer[99].invoice.insert({ total: 3 })
end function
r = refusal(no_parent)
check("a step from a row that is not there", r.code, "NOT_FOUND")
check("...inserting nothing", len(rows_of("select id from invoice where total = 3")), 0)

shop.line.insert({ invoice: 1, n: 1, item: "pen" })
shop.line[1, 1] = { item: "pad" }
check("a composite key written", (rows_of("select item from line where invoice = 1 and n = 1")[0]).item, "pad")
shop.line[1, 2] = { item: "ink" }
check("a composite key not there is inserted", rows_of("select item from line order by n").item, ["pad", "ink"])
shop.customer[2].invoice[.total < 2] = { paid: true }
check("a filter through a step", rows_of("select id from invoice where paid = true").id, [2])
shop.invoice[1].customer = { city: "Lyon" }
check("a step up from one writes its parent", (rows_of("select city from customer where id = 1")[0]).city, "Lyon")
shop.customer[2].invoice[3] = { paid: true }
check("a key through a step writes that row", rows_of("select id from invoice where paid = true order by id").id, [2, 3])
function stepped_missing()
    shop.customer[2].invoice[1] = { paid: true }
    return nothing
end function
r = refusal(stepped_missing)
check("a key through a step that is not that parent's: NOT_FOUND", r.code, "NOT_FOUND")
check("...and nothing is inserted or written", (rows_of("select paid from invoice where id = 1")[0]).paid, false)
function parent_missing()
    shop.invoice[99].customer = { city: "X" }
    return nothing
end function
check("a parent that is not there", refusal(parent_missing).code, "NOT_FOUND")

' --- delete -------------------------------------------------------------------
check("delete answers how many", shop.invoice[.paid = true].delete(), 2)
check("...they are gone", rows_of("select id from invoice order by id").id, [1])
check("delete of a key not there answers 0", shop.invoice[99].delete(), 0)
check("delete through a step with nothing to delete", shop.customer[2].invoice.delete(), 0)
shop.customer[2].invoice.insert({ total: 4 })
check("delete through a step", shop.customer[2].invoice.delete(), 1)
check("delete cascades as the key says", shop.customer[1].delete(), 1)
check("...taking its invoices", rows_of("select count(*) as n from invoice")[0].n, 0)
check("...and their lines", rows_of("select count(*) as n from line")[0].n, 0)

' --- in a transaction ---------------------------------------------------------
shop.begin()
shop.customer[2] = { city: "Rolled" }
shop.customer.insert({ name: "Gone" })
check("a write is seen in its transaction", (rows_of("select city from customer where id = 2")[0]).city, "Rolled")
shop.rollback()
check("...and undone with it", type((rows_of("select city from customer where id = 2")[0]).city), "unknown")
check("...the insert too", len(rows_of("select id from customer where name = 'Gone'")), 0)

' --- values are parameters ------------------------------------------------------
odd = "x'); delete from customer; --"
shop.customer[2] = { name: odd }
check("a value is stored as itself", (rows_of("select name from customer where id = 2")[0]).name, odd)
check("...and nothing else happened", len(rows_of("select id from customer")) > 2, true)

' --- the attribute is still the attribute -----------------------------------------
shop.max_recursion = 5000
check("max_recursion is assigned, not written as a table", shop.max_recursion, 5000)

' --- keys held go stale under a write ----------------------------------------------
other = gsql.connect(dir + "/writes", { wait: 600 })
shop.exec("insert into invoice (id, customer, total) values (50, 2, 1)")
check("the keys are fetched by a step", (shop.invoice[50].customer).id, 2)
other.exec("alter table invoice drop constraint invoice_customer_fkey")
other.exec("alter table customer add column fav integer")
other.exec("alter table customer add constraint fav foreign key (fav) references invoice")
other.exec("update customer set fav = 50 where id in (2, 20)")
shop.invoice[50].customer = { credit: 77 }
check("a write the stale keys called one, now many: every row it names", rows_of("select id from customer where credit = 77 order by id").id, [2, 20])
other.close()

shop.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
