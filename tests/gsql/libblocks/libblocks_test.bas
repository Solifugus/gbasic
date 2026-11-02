' `database` and `sql` blocks inside a library (docs/gsql_library_blocks_design.md;
' gbasic-books volume 3, gap G3). Run by tests/run_gsql_module.sh from inside an
' empty directory, with this file's directory holding store.bas and depot.bas.
'
' The program declares its OWN `shop` and `make`, so a library function that
' reached the program's instead of its own answers wrongly rather than passing.
load gsql
load store
load depot
load store as shelf

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

database shop( file: "program.gsql", create: true )

sql make on shop
    create table note (body string)
end sql

sql notes on shop
    select body from note
end sql

sql add_note on shop
    insert into note (body) values (:body)
end sql

make.exec()
body = "the program's own"
add_note.exec()

store.setup()
store.put("pear", 4)
store.put("apple", 3)
store.put_record({ name: "fig", price: 9 })
check("a library function runs its own blocks by bare name", store.names(), ["apple", "fig", "pear"])
check("a parameter is read from the function that runs the block", store.under(5), ["apple", "pear"])

depot.setup()
depot.put("B-2")
depot.put("A-1")
check("a second library's `shop` is its own database", depot.all_codes(), ["A-1", "B-2"])
check("and the first library's is untouched by it", store.names(), ["apple", "fig", "pear"])

' the program's own `shop` and `make` were not the libraries'
rows = notes.query()
check("the program's own database holds only the program's row", count(rows), 1)
check("with its own text", rows[0].body, "the program's own")

' from outside, qualified
got = []
for each row in store.all_items
    append(got, row.name)
end for
check("a program reaches a library's block qualified, in a loop", got, ["apple", "fig", "pear"])
r = gsql.exec(store.cheaper, { limit: 4 })
check("and through gsql.exec, with a record of parameters", count(r.rows), 1)
check("the record's parameter is the one used", r.rows[0].name, "apple")
check("a qualified block is an sql block", type(store.all_items), "gsql_statement")

' an alias is the same library, so the same blocks and the same database
check("an aliased library's functions reach the same blocks", shelf.names(), ["apple", "fig", "pear"])

' a library's block named the same as a program variable cannot be reached by
' the program bare: the bare name is the program's
check("the program's bare `make` is still the program's block", type(make), "gsql_statement")
check("and the program's bare `shop` its own database", shop = store.shop, false)

store.close()
shop.close()

' WHERE THE ROWS WENT, read from each file directly. Without this a library
' whose statements ran on the PROGRAM's `shop` passed every check above, since
' its tables have names of their own -- measured, by pointing every block's
' database lookup at the global frame and watching this fixture stay green.
function tables_in(path)
    db = gsql.connect(path)
    out = []
    for each t in ["item", "bin", "note"]
        on error goto next
        r = db.query("select count(*) as n from " + t)
        if error then
            r = []
        else
            append(out, t)
        end if
        on error stop
    end for
    db.close()
    return out
end function
check("store.gsql holds the store library's table and no other", tables_in("store.gsql"), ["item"])
check("depot.gsql holds the depot library's", tables_in("depot.gsql"), ["bin"])
check("program.gsql holds only the program's", tables_in("program.gsql"), ["note"])
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
