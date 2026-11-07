' gsql.columns(db, sql [, parameters]): a query's result columns from its
' plan, with nothing run (gdash's A2). Every answer is written here by hand.
' Run by run_gsql_module.sh's columns tier, with GSQL_DIR set.
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

function attempt(db, sql)
    on error goto next
    x = gsql.columns(db, sql)
    e = error
    if e then
        return e.message
    end if
    return x
end function

function attempt_query(db, sql)
    on error goto next
    x = gsql.query(db, sql)
    e = error
    if e then
        return e.message
    end if
    return x
end function

dir = env("GSQL_DIR")
db = gsql.connect(dir + "/columns", { create: true, linger: 0 })
db.exec("create table t (id number primary key, name text, amt money(USD), at date)")
db.exec("insert into t (id, name, amt, at) values (1, 'a', 1, '2026-01-02')")

check("a table's columns, in order, with their kinds", db.columns("select * from t"),
      [{ name: "id", kind: "number" }, { name: "name", kind: "string" },
       { name: "amt", kind: "money(USD)" }, { name: "at", kind: "date" }])
check("a name given with AS", db.columns("select id as ident, count(*) as c from t group by id"),
      [{ name: "ident", kind: "number" }, { name: "c", kind: "integer" }])
check("an expression the planner cannot type is any", db.columns("select amt * 2 as twice from t")[0].kind, "any")
check("a parameter need not be given", db.columns("select id from t where id = $1"), [{ name: "id", kind: "number" }])
check("and may be", gsql.columns(db, "select id from t where id = $1", [5]), [{ name: "id", kind: "number" }])

' NOTHING RUNS: a query that raises when evaluated is described all the same,
' and the same text through query refuses
check("a query that would fail when run is still described", db.columns("select 1 / 0 as x from t"),
      [{ name: "x", kind: "any" }])
check("as query shows it would", contains(string(attempt_query(db, "select 1 / 0 as x from t")), "zero"), true)
check("an INSERT is refused", attempt(db, "insert into t (id) values (2)"),
      "gsql.columns: only a query that returns rows has columns to describe; this is a statement that returns none [STATE] -- in \"insert into t (id) values (2)\"")
check("and was not run", db.query("select count(*) as n from t")[0].n, 1)
check("a column that is not there is refused as the query would be",
      contains(attempt(db, "select nope from t"), "table t has no column `nope` [NOT_FOUND"), true)
db.begin("read")
check("described inside a read transaction", db.columns("select name from t"), [{ name: "name", kind: "string" }])
db.commit()
db.begin("read")
check("an INSERT described in a read transaction is refused as no query, not as a write",
      contains(attempt(db, "insert into t (id) values (3)"), "[STATE]"), true)
db.rollback()
db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"

