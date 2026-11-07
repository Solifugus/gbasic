' gsql.close(db, { wait }): close, then wait up to `wait` for the owner to
' stop, answering whether it did (gdash's A4). A program deleting a database,
' or a test checking its scratch directory, must know the owner has stopped
' writing. Run by run_gsql_module.sh's close tier, with GSQL_DIR set.
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

function refusal(db, opts)
    on error goto next
    x = gsql.close(db, opts)
    m = error.message
    error.clear()
    return m
end function

dir = env("GSQL_DIR")

' no linger: the owner stops at the last close, and the wait sees it go
db = gsql.connect(dir + "/a", { create: true, linger: 0 })
db.exec("create table t (k number primary key)")
r = db.insert("t", [{ k: 1 }, { k: 2 }])
check("the owner stopped, and the close says so", gsql.close(db, { wait: 10 }), true)
x = remove_tree(dir + "/a")
check("so the database can be deleted at once", exists(dir + "/a"), false)

' a second connection keeps the owner: the wait runs out, and says so
db1 = gsql.connect(dir + "/b", { create: true, linger: 0 })
db2 = gsql.connect(dir + "/b")
check("an owner another connection uses is still running after the wait", gsql.close(db1, { wait: 1 }), false)
check("and stops at the last close", db2.close({ wait: 10 seconds }), true)

' the default linger: waiting nothing answers at once
db3 = gsql.connect(dir + "/c", { create: true })
check("a lingering owner, not waited for", gsql.close(db3, { wait: 0 }), false)

' without wait, close answers true as before
db4 = gsql.connect(dir + "/d", { create: true, linger: 0 })
check("close without wait", gsql.close(db4), true)

db5 = gsql.connect(dir + "/e", { create: true, linger: 0 })
check("an option it does not have", refusal(db5, { wiat: 1 }), "gsql.close has one option, `wait`")
check("a wait out of range", refusal(db5, { wait: -1 }),
      "gsql.close: `wait` is how long to wait for the owner to stop, from 0 to 3600 seconds")
check("not a record", refusal(db5, 5), "gsql.close: the options are a record, { wait: seconds }")
check("the refusals left it open", gsql.close(db5, { wait: 10 }), true)

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
