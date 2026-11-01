' G-SQL's `database` and `sql` blocks (docs/gsql_module_design.md §7): every
' answer below is written here by hand, and each line prints `ok` or a MISMATCH
' naming both sides. Run by tests/run_gsql_module.sh from an empty directory,
' since a block's `file:` is a literal and a relative one is found from there.
load gsql

checks = { n: 0, bad: 0 }

database shop( file: "shop.gsql", create: true )
database spare( file: "spare.gsql", create: true )
database unused( file: "unused.gsql", create: true )
' a role: chosen at run time, its blocks given a connection when used
' (docs/gsql_block_connection_design.md)
database tenant()

' several statements in one block (docs/gsql_multi_statement_design.md)
sql m_schema on shop
    create table visit (id integer serial primary key, page text not null);
    create index visit_page on visit (page)
end sql

sql m_record_and_show on shop
    insert into visit (page) values (:page);
    select page, count(*) as n from visit group by page order by page
end sql

' a `;` inside a string ends nothing
sql m_two on shop
    insert into visit (page) values (:page);
    insert into visit (page) values ('semi; colon')
end sql

sql m_half_bad on shop
    insert into visit (page) values ('half');
    insert into no_such_table (x) values (1)
end sql

sql m_count_page on shop
    select count(*) as n from visit where page = :page
end sql

sql t_make on tenant
    create table item (id integer primary key, name string not null)
end sql

sql t_add on tenant
    insert into item (id, name) values (:id, :name)
end sql

sql t_items on tenant
    select id, name from item order by id
end sql

sql make on shop
    create table item (id integer primary key, name string not null, price money, seen date)
end sql

sql add on shop
    insert into item (id, name, price, seen) values (:id, :name, :price, :seen)
end sql

' a CASE ... END on a line of its own, which is why a block ends at `end sql`
sql cheap on shop
    select id, name, price,
           case when price < :limit then 'cheap'
                else 'dear'
           end as band
    from item
    where price < :limit
    order by id
end sql

sql everything on shop
    select id, name from item order by id
end sql

sql named on shop
    select name from item where id = :id
end sql

sql by_q on shop
    select name from item where id = :no_such_variable_q
end sql

sql twice on shop
    select id, id from item
end sql

sql spare_rows on spare
    select 1 as one
end sql

sql make_n on shop
    create table n (k integer primary key)
end sql

sql add_n on shop
    insert into n (k) values (:k)
end sql

sql all_n on shop
    select k from n order by k
end sql

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

function code_of(e)
    if type(e) = "record" then
        return e.details.code
    end if
    return "(no error)"
end function

function message_of(e)
    if type(e) = "record" then
        return e.message
    end if
    return "(no error)"
end function

' A block reads :limit from the frame it runs in, so this function's own
' parameter is what the statement sees.
function names_under(limit)
    out = []
    for each row in cheap
        append(out, row.name)
    next
    return join(out, ",")
end function

function query_cheap()
    on error goto next
    r = gsql.query(cheap)
    e = error
    return e
end function

function exec_block(b)
    on error goto next
    r = gsql.exec(b)
    e = error
    return e
end function

function exec_on(c, b)
    on error goto next
    r = gsql.exec(c, b)
    e = error
    return e
end function

function exec_on_with(c, b, p)
    on error goto next
    r = gsql.exec(c, b, p)
    e = error
    return e
end function

function on_of(c, b)
    on error goto next
    r = gsql.on(c, b)
    e = error
    return e
end function

' a tenant's rows, filled from this function's own variables
function fill(db, who, count)
    gsql.exec(db, t_make)
    for i = 1 to count
        id = i
        name = who + string(i)
        gsql.exec(db, t_add)
    next
    return nothing
end function

function names_of(bound)
    out = []
    for each row in bound
        append(out, row.name)
    next
    return join(out, ",")
end function

function query_block(b)
    on error goto next
    r = gsql.query(b)
    e = error
    return e
end function

function iterate_block(b)
    on error goto next
    for each row in b
        print "MISMATCH the loop body ran for a refused block"
        checks.bad = checks.bad + 1
    next
    e = error
    return e
end function

function query_with_params(b)
    on error goto next
    r = gsql.query(b, { limit: 1 })
    e = error
    return e
end function

' a raise inside the loop body, caught here; the loop must have ended its
' cursor on the way out
function raise_inside()
    for each row in everything
        x = row.name + nonexistent_variable
    next
    return nothing
end function

function catch_raise()
    on error goto next
    r = raise_inside()
    e = error
    return e
end function

' --- the declarations are values, and declaring connects nothing ------------
check("a database block is a connection", type(shop), "gsql_connection")
check("an sql block is a statement", type(cheap), "gsql_statement")
check("an sql block renders as its kind", string(cheap), "<gsql_statement>")
check("an sql block is itself", cheap = cheap, true)
check("two sql blocks differ", cheap = everything, false)
check("declaring a database creates no file", exists({file}"shop.gsql"), false)

' --- running blocks ---------------------------------------------------------
r = gsql.exec(make)
check("the first use creates the database", exists({file}"shop.gsql"), true)
check("a DDL block answers its command", r.command, "CREATE")
check("a DDL block has no row count", r.rows_affected, nothing)

for i = 1 to 5
    id = i
    name = "item " + string(i)
    price = {USD}"0.25" * i
    seen = {date}"2026-10-30"
    r = gsql.exec(add)
    check("an insert block affects one row (" + string(i) + ")", r.rows_affected, 1)
next

limit = {USD}"1.00"
got = []
for each row, n in cheap
    append(got, string(n) + ":" + string(row.id) + ":" + row.band)
next
check("a select block, row by row with its index", join(got, " "), "0:1:cheap 1:2:cheap 2:3:cheap")

rows = gsql.query(cheap)
check("gsql.query over a block", len(rows), 3)
check("money comes back as money", rows[2].price, {USD}"0.75")
check("a loop counter reached the table exact", rows[0].id + 9007199254740992, 9007199254740993)
check("a row's fields are the select list's", join(keys(rows[0]), ","), "id,name,price,band")

limit = {USD}"0.30"
check("the variable is read each time the block runs", len(gsql.query(cheap)), 1)
check("a function's own variable is what its block reads", names_under({USD}"0.60"), "item 1,item 2")
check("and the caller's is untouched", limit, {USD}"0.30")

' --- a block's parameter that names nothing ---------------------------------
id = nothing
e = query_block(named)
check("an absent parameter value is bound as unknown, and matches nothing", code_of(e), "(no error)")
saved = limit
limit = nothing
check("a parameter variable holding nothing is bound, not refused", len(gsql.query(cheap)), 0)
limit = saved
e = query_cheap()
check("a parameter the frame has is read", code_of(e), "(no error)")
e = query_block(by_q)
check("a parameter with no variable is refused", code_of(e), "PARAMETER")
check("naming the variable", contains(message_of(e), "there is no variable `no_such_variable_q` here"), true)

' --- refusals ----------------------------------------------------------------
' exec over a select block answers its rows, the same ones query answers
' (docs/gsql_store_conversion_design.md §2)
check("exec over a select block answers its rows", len(gsql.exec(cheap).rows), len(gsql.query(cheap)))
check("as a SELECT, with no count", type(gsql.exec(cheap).rows_affected), "nothing")

id = 6
name = "item 6"
price = {USD}"9.00"
e = query_block(add)
check("query over an insert block is refused", code_of(e), "STATE")
check("and says it ran", contains(message_of(e), "it ran, and affected 1"), true)
check("and names exec of the block", contains(message_of(e), "gsql.exec(add)"), true)
check("the refused insert did run", len(gsql.query(everything)), 6)

id = 7
e = iterate_block(add)
check("iterating an insert block is refused", code_of(e), "STATE")
check("the refused iteration ran the insert", len(gsql.query(everything)), 7)

e = query_with_params(cheap)
check("a block takes no parameters but its frame's", contains(message_of(e), "takes the block alone"), true)

e = query_block(twice)
check("two result columns with one name", code_of(e), "DUPLICATE")

' --- a long result, a batch at a time ----------------------------------------
gsql.exec(make_n)
gsql.begin(shop)
for i = 1 to 1234
    k = i
    gsql.exec(add_n)
next
gsql.commit(shop)
total = 0
count = 0
for each row in all_n
    total = total + row.k
    count = count + 1
next
check("every row of three batches", count, 1234)
check("and their sum", total, 761995)

first = nothing
for each row in all_n
    first = row.k
    break
next
check("a loop that breaks at once", first, 1)

' a loop nested in another, the inner block reading the outer row
got = []
for each outer in everything
    if outer.id <= 2 then
        id = outer.id
        for each inner in named
            append(got, inner.name)
        next
    end if
next
check("a block inside a loop over another", join(got, ","), "item 1,item 2")

e = catch_raise()
check("a raise in the loop body escapes it", contains(message_of(e), "nonexistent_variable"), true)
check("and the connection still works", len(gsql.query(everything)), 7)

' --- dynamic SQL through the same database (gsql_design.md §10G) -------------
check("gsql.query on a declared database", gsql.query(shop, "select name from item where id = 3")[0].name, "item 3")

' --- transactions around blocks ----------------------------------------------
gsql.begin(shop)
id = 50
name = "rolled back"
gsql.exec(add)
check("inside the transaction the row is there", len(gsql.query(everything)), 8)
gsql.rollback(shop)
check("rollback undoes a block's insert", len(gsql.query(everything)), 7)

' --- a database's name assigned over ------------------------------------------
spare = 5
e = query_block(spare_rows)
check("a block whose database name was reassigned", code_of(e), "STATE")
check("says what happened", contains(message_of(e), "no longer holds a database"), true)

' --- several statements in a block -----------------------------------------------
r = gsql.exec(m_schema)
check("a schema block answers its last statement", r.command, "CREATE")
check("and every statement, in order", len(r.statements), 2)
page = "home"
shown = []
for each row in m_record_and_show
    append(shown, row.page + "=" + string(row.n))
next
check("a block ending in a select runs its insert, then streams", join(shown, ","), "home=1")
check("gsql.query takes it too", len(gsql.query(m_record_and_show)), 1)
page = "about"
r = gsql.exec(m_two)
check("exec answers the last count", r.rows_affected, 1)
check("with each statement's count", r.statements[0].rows_affected, 1)
page = "semi; colon"
check("a ; in a string ended nothing", gsql.query(m_count_page)[0].n, 1)
e = exec_block(m_half_bad)
check("a block whose second statement fails is refused", code_of(e), "NOT_FOUND")
page = "half"
check("and its first is undone: all or none", gsql.query(m_count_page)[0].n, 0)
gsql.begin(shop)
page = "joined"
gsql.exec(m_two)
gsql.rollback(shop)
check("inside the program's transaction, its rollback undoes the block", gsql.query(m_count_page)[0].n, 0)
gsql.begin(shop)
gsql.exec(m_two)
gsql.commit(shop)
check("and its commit keeps it", gsql.query(m_count_page)[0].n, 1)
' exec runs any block and answers its last statement: here, rows
' (docs/gsql_store_conversion_design.md §2)
page = "home"
r = gsql.exec(m_record_and_show)
' one row a page: home, about, "semi; colon" and joined
check("exec of a block ending in a select answers its rows", len(r.rows), 4)
check("as a SELECT", r.command, "SELECT")
check("with every statement's answer", len(r.statements), 2)
check("and its insert ran", gsql.query(m_count_page)[0].n, 3)
e = query_block(m_two)
check("query of a block ending in no rows is refused before anything runs", contains(message_of(e), "nothing ran"), true)
page = "about"
check("so it inserted nothing either", gsql.query(m_count_page)[0].n, 1)

' --- a block on a database chosen at run time -----------------------------------
ta = gsql.connect("tenant_a.gsql", { create: true })
tb = gsql.connect("tenant_b.gsql", { create: true })
fill(ta, "a", 3)
fill(tb, "b", 2)
check("the same blocks on two tenants: a's rows", names_of(gsql.on(ta, t_items)), "a1,a2,a3")
check("and b's, each holding only its own", names_of(gsql.on(tb, t_items)), "b1,b2")
check("gsql.query(connection, block)", len(gsql.query(tb, t_items)), 2)
check("a bound block runs through exec too", gsql.exec(gsql.on(ta, t_add)).rows_affected, 1)
check("with :id and :name read where it ran", len(gsql.query(ta, t_items)), 4)
e = exec_block(t_add)
check("a role's block used alone", code_of(e), "STATE")
check("says how to give it a connection", contains(message_of(e), "gsql.exec(connection, t_add)"), true)
e = query_block(t_items)
check("a role's block in query alone", contains(message_of(e), "gsql.on(connection, t_items)"), true)
e = query_block(tenant)
check("a role used as a connection", contains(message_of(e), "chosen at run time"), true)
' the control: a block on a database with a file still runs there alone, and
' elsewhere when given a connection
check("a block with a file still runs alone", len(gsql.query(everything)), 7)
e = exec_on(ta, make_n)
check("and runs on another connection when given one", code_of(e), "(no error)")
check("which is where the table was made", len(gsql.query(ta, all_n)), 0)
e = exec_on_with(ta, t_add, { id: 9, name: "x" })
check("a block takes no parameters argument", contains(message_of(e), "takes the block and nothing more"), true)
e = on_of(5, t_items)
check("gsql.on wants a connection first", contains(message_of(e), "expects a gsql connection first"), true)
e = on_of(ta, "select 1")
check("gsql.on wants a block second", contains(message_of(e), "expects an sql block second"), true)
bound = gsql.on(tb, t_items)
gsql.close(tb)
e = query_block(bound)
check("a bound block whose connection was closed", contains(message_of(e), "closed"), true)
check("leaves the declared block alone", contains(message_of(query_block(t_items)), "chosen at run time"), true)
gsql.close(ta)

' --- close ---------------------------------------------------------------------
gsql.close(unused)
check("closing an unused database starts nothing", exists({file}"unused.gsql"), false)
gsql.close(shop)
e = query_block(everything)
check("a block on a closed database", contains(message_of(e), "closed"), true)

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
