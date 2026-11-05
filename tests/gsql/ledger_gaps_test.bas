' Two small gaps from the gBASIC books' volume 3 ledger, each answer written by
' hand. Run by tests/run_gsql_module.sh from inside an empty directory.
'   G9  CREATE VIEW IF NOT EXISTS, as tables take it
'   G7  a database in a folder that does not exist names the folder
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

function refusal(db, sql)
    on error goto refused
    r = gsql.exec(db, sql)
    return "ok"
refused:
    c = error.details.code + " " + error.message
    error.clear()
    return c
end function

function connect_message(path)
    on error goto refused
    db = gsql.connect(path, { create: true })
    return "connected"
refused:
    m = error.message
    error.clear()
    return m
end function

db = gsql.connect("gaps.gsql", { create: true })
gsql.exec(db, "create table t (k integer primary key)")
gsql.exec(db, "insert into t values (1), (2)")

' --- G9 ---------------------------------------------------------------------
check("CREATE VIEW IF NOT EXISTS makes it", refusal(db, "create view if not exists v as select k from t"), "ok")
check("  and a second one is not refused", refusal(db, "create view if not exists v as select 0 as k"), "ok")
check("  and keeps the first definition", count(gsql.query(db, "select k from v")), 2)
check("a TABLE of the name is still refused", starts_with(refusal(db, "create view if not exists t as select 1"),
      "EXISTS"), true)
check("OR REPLACE with IF NOT EXISTS is refused, naming both",
      contains(refusal(db, "create or replace view if not exists w as select 1"), "OR REPLACE and IF NOT EXISTS"), true)
check("tables take it as they did", refusal(db, "create table if not exists t (k integer)"), "ok")

' --- G7 ---------------------------------------------------------------------
m = connect_message("no_such_folder/inner/x.gsql")
check("a missing folder is named", contains(m, "the folder no_such_folder/inner does not exist"), true)
check("  beside the engine's own word", contains(m, "the engine answered IO"), true)

db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
