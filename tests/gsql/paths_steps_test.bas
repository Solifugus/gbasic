' G-SQL paths, increment 2: STEPS between tables across foreign keys
' (docs/gsql_paths_design.md §3.1, §3.2, §5, §8.2). Every answer is written
' here by hand from the rows this file inserts. The last section changes the
' schema from a second connection, so the first one's foreign keys are stale,
' and shows that costs a round trip and never a wrong answer. Run by
' run_gsql_paths.sh with GSQL_DIR set.
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
shop = gsql.connect(dir + "/steps", { create: true, linger: 0, wait: 600 })
shop.exec("create table customer (id integer primary key, name text, city text)")
shop.exec("create table invoice (id integer primary key, customer integer references customer, total integer, paid boolean)")
shop.exec("create table line (invoice integer references invoice on delete cascade, n integer, item text, primary key (invoice, n))")
shop.exec("create table part (id integer primary key, invoice integer, n integer, maker text, foreign key (invoice, n) references line)")
shop.exec("create table shipment (id integer primary key, bill_to integer references customer, ship_to integer references customer)")
shop.exec("create table employee (id integer primary key, name text, boss integer references employee)")
shop.exec("create table note (body text primary key)")
shop.exec("insert into customer values (1, 'Ana', 'Rome'), (2, 'Bo', 'Oslo'), (3, 'Cy', 'Rome'), (4, 'Di', 'Paris')")
shop.exec("insert into invoice values (12, 3, 70, true), (10, 1, 100, true), (11, 3, 50, false), (13, 2, 20, false), (14, null, 5, false)")
shop.exec("insert into line values (10, 1, 'pen'), (11, 2, 'pad'), (11, 1, 'ink'), (12, 1, 'cap')")
shop.exec("insert into part values (100, 11, 2, 'acme'), (101, 11, 2, 'zeta'), (102, 10, 1, 'acme')")
shop.exec("insert into shipment values (1, 1, 2)")
shop.exec("insert into employee values (1, 'A', null), (2, 'B', 1)")

' --- from a parent to its children: many ------------------------------------
check("a customer's invoices", shop.customer[3].invoice.query().id, [11, 12])
check("a step from many: Rome's invoices", shop.customer[.city = "Rome"].invoice.query().id, [10, 11, 12])
check("a filter on the step's table", shop.customer[3].invoice[.paid = false].query().id, [11])
check("a key on the step's table: one row", (shop.customer[3].invoice[12]).total, 70)
check("...unknown when that row is not this customer's", type(shop.customer[3].invoice[10]), "unknown")
check("a customer with no invoices", shop.customer[4].invoice.query(), [])
check("two steps down", shop.customer[3].invoice.line.query().item, ["ink", "pad", "cap"])
check("count over two steps", shop.customer[.city = "Rome"].invoice.line.count(), 4)

' --- from a child to its parent: one row from one ---------------------------
check("an invoice's customer is read where the path ends", type(shop.invoice[11].customer), "record")
check("...and is that customer", (shop.invoice[11].customer).name, "Cy")
check("a child whose key is unknown references nothing", type(shop.invoice[14].customer), "unknown")
check("a child that is not there has no parent", type(shop.invoice[99].customer), "unknown")
check("two steps up, still one", (shop.line[11, 2].invoice.customer).name, "Cy")
check("up then down: an invoice's siblings", shop.invoice[11].customer.invoice.query().id, [11, 12])
check("parents of many, each once, in key order", shop.invoice[.paid = false].customer.query().id, [2, 3])
check("count of distinct parents", shop.invoice[.total > 0].customer.count(), 3)

' --- a foreign key of two columns -------------------------------------------
check("down across a two-column key", shop.line[11, 2].part.query().maker, ["acme", "zeta"])
check("up across a two-column key", (shop.part[102].line).item, "pen")
check("up from many across it", shop.part[.maker = "acme"].line.query().item, ["pen", "pad"])

' --- a result is not read until used ----------------------------------------
r = shop.customer[3].invoice
check("a step from a parent is a result", type(r), "gsql_statement")
shop.exec("insert into invoice values (15, 3, 1, false)")
check("...read when used, with the row added since", r.count(), 3)

' --- what a step is -----------------------------------------------------------
check("sql(): a step from one row is sought by its key", shop.invoice[11].customer.sql(), "SELECT * FROM \"customer\" AS \"t1\" WHERE \"t1\".\"id\" = (SELECT \"t0\".\"customer\" FROM \"invoice\" AS \"t0\" WHERE \"t0\".\"id\" = $1) ORDER BY \"t1\".\"id\"\n$1 = 11")
check("sql(): a step from many is EXISTS", shop.customer[.city = "Rome"].invoice.sql(), "SELECT * FROM \"invoice\" AS \"t1\" WHERE EXISTS (SELECT 1 FROM \"customer\" AS \"t0\" WHERE \"t1\".\"customer\" = \"t0\".\"id\" AND (\"t0\".\"city\" = $1)) ORDER BY \"t1\".\"id\"\n$1 = \"Rome\"")

' --- steps that cannot be taken, each beside one that can ---------------------
function two_keys()
    return shop.customer[1].shipment.query()
end function
r = refusal(two_keys)
check("two foreign keys between the tables", message_has(r, "are joined by 2 foreign keys"), true)
check("...naming them", message_has(r, "shipment_bill_to_fkey"), true)
check("...refused as NOT_FOUND", r.code, "NOT_FOUND")
check("the filter it points to works", shop.shipment[.bill_to = 1].query().id, [1])

function itself()
    return shop.employee[2].employee.query()
end function
r = refusal(itself)
check("a table joined to itself", message_has(r, "references itself by employee_boss_fkey"), true)
check("...saying which column to filter by", message_has(r, "`.boss = ...`"), true)
check("the filter it points to works", shop.employee[.boss = 1].query().name, ["B"])

function unjoined()
    return shop.customer[1].note.query()
end function
check("tables no foreign key joins", message_has(refusal(unjoined), "are not joined by a foreign key"), true)

function misspelt()
    return shop.customer[1].invoce.query()
end function
r = refusal(misspelt)
check("a table that is not there", message_has(r, "has no table `invoce`"), true)
check("...with the nearest", message_has(r, "did you mean `invoice`?"), true)

' --- the keys this connection holds go stale -----------------------------------
other = gsql.connect(dir + "/steps", { wait: 600 })
other.exec("create table coupon (id integer primary key, customer integer references customer)")
other.exec("insert into coupon values (1, 3)")
check("a foreign key made since the keys were fetched is found", shop.customer[3].coupon.query().id, [1])

' invoice -> customer was a step UP, one row; turn it around, so the keys
' `shop` holds say one and the database says many
other.exec("alter table invoice drop constraint invoice_customer_fkey")
other.exec("alter table customer add column fav integer")
other.exec("alter table customer add constraint fav foreign key (fav) references invoice")
other.exec("update customer set fav = 11 where id = 2")
x = shop.invoice[11].customer
check("a step the keys held called one, now many: a result, not a row", type(x), "gsql_statement")
check("...holding what the database now says", x.query().id, [2])
check("and the keys held are current after it", type(shop.invoice[11].customer), "gsql_statement")
check("a step up the new key is one", (shop.customer[2].invoice).total, 50)
other.close()

shop.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
