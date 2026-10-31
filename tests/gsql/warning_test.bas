' A G-SQL warning reaches the program as a gBASIC warning
' (docs/gsql_warning_design.md). Run by tests/run_gsql_module.sh with GSQL_DIR
' set to an empty directory. Lines that must warn carry a `' WARNS` marker and
' lines that must not carry `' QUIET`: the runner finds each one's line number
' in this file and reads stderr for it, since a printed warning goes nowhere
' else. The other modes are asserted here, in the language.
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

db = gsql.connect(env("GSQL_DIR") + "/w.gsql", { create: true })
gsql.exec(db, "create table t (id integer primary key, x number)")
gsql.exec(db, "insert into t values (1, 0)")

' printed, at the program's own line; the answer still arrives
r = gsql.query(db, "select 9223372036854775807 + 1 as big") ' WARNS
check("the answer arrives with its warning", r[0].big > 9223372036854775000, true)

' the same warning in a loop is said once, at its site
for i = 1 to 3
    gsql.query(db, "select 9223372036854775807 + 2 as big") ' WARNS ONCE
next

' one site, two different warnings: both are said, once each
for each q in ["select 9223372036854775807 + 1 as big", "select 9223372036854775807 * 2 as big"]
    gsql.query(db, q) ' WARNS TWICE
next

' a statement that warns and is then refused: the refusal is what arrives, and
' its warning does not leak into the next statement's reply (ORDER BY makes it
' fail while it starts, sorting, rather than in its first batch)
function refused()
    on error goto next
    gsql.query(db, "select 9223372036854775807 + 7 as big, 1 / (id - id) as bad from t order by 1")
    if error then
        return "refused"
    end if
    return "answered"
end function
check("a refused statement raises its refusal", refused(), "refused")

' a reply with no warning says nothing
gsql.query(db, "select 1 as one") ' QUIET

function asked()
    on warning goto next
    gsql.query(db, "select 9223372036854775807 + 3 as big")
    if warning then
        return warning.code
    end if
    return 0
end function
check("on warning goto next reads the code", asked(), 2112)

function silenced()
    on warning ignore
    gsql.query(db, "select 9223372036854775807 + 4 as big") ' QUIET
    return nothing
end function
silenced()

' strict: under `on warning stop` an UPDATE whose SET overflows raises before
' the commit, and the program rolls it back
function strict()
    on error goto next
    on warning stop
    gsql.begin(db)
    gsql.exec(db, "update t set x = 9223372036854775807 + 5 where id = 1")
    if error then
        gsql.rollback(db)
        return error.code
    end if
    gsql.commit(db)
    return 0
end function
check("on warning stop raises the warning's code", strict(), 2112)
check("and the rollback left the row as it was", gsql.query(db, "select x from t where id = 1")[0].x, 0)

' a warning from a query's rows, as `for each` reads them
gsql.exec(db, "insert into t values (2, 9223372036854775807), (3, 9223372036854775807)")
function batches()
    on warning goto next
    n = 0
    for each row in gsql.query(db, "select x + 1 as y from t order by id")
        n = n + 1
    next
    if warning then
        return warning.code
    end if
    return 0
end function
check("a warning from the rows reaches the loop", batches(), 2112)

' a change under a view (docs/gsql_view_design.md §3): warned at the statement
' that made it; under `on warning stop` inside a transaction, an error before
' the commit, so the drop is rolled back and the view still runs
gsql.exec(db, "create table base (id integer primary key, v integer)")
gsql.exec(db, "insert into base values (1, 10)")
gsql.exec(db, "create view seen as select v from base")
function careful()
    on error goto next
    on warning stop
    gsql.begin(db)
    gsql.exec(db, "drop table base")
    if error then
        gsql.rollback(db)
        return error.code
    end if
    gsql.commit(db)
    return 0
end function
check("a drop that breaks a view is an error under stop", careful(), 2118)
check("and rolled back, the view still runs", gsql.query(db, "select v from seen")[0].v, 10)

' a foreign key's refusal carries its code by name (docs/gsql_fkey_design.md)
gsql.exec(db, "create table par (id integer primary key)")
gsql.exec(db, "insert into par values (1)")
gsql.exec(db, "create table kid (id integer primary key, par integer references par)")
function orphan()
    on error goto next
    gsql.exec(db, "insert into kid values (1, 9)")
    if error then
        return error.details.code
    end if
    return "(no error)"
end function
check("a row with no parent is refused as FOREIGN_KEY", orphan(), "FOREIGN_KEY")
' dropping a referenced parent warns (2119); under stop, rolled back
function careful_fk()
    on error goto next
    on warning stop
    gsql.begin(db)
    gsql.exec(db, "drop table par")
    if error then
        gsql.rollback(db)
        return error.code
    end if
    gsql.commit(db)
    return 0
end function
check("a drop that unchecks a foreign key is an error under stop", careful_fk(), 2119)
check("and rolled back, the parent is still there", gsql.query(db, "select count(*) as n from par")[0].n, 1)

gsql.close(db)
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
