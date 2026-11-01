' How long an owner lives, from programs (docs/gsql_owner_lifetime_design.md),
' driven by tests/run_gsql_module.sh's lifetime tier. GSQL_ROLE says which
' part this run plays; GSQL_DB is the database, GSQL_GATE a file to wait for.
'
'   starter    starts the owner, says "ready", waits for the gate, exits
'   other      connects to the starter's owner, asks, waits for the gate,
'              asks again: THE DEFECT, since the starter exits in between
'   quick      connects with linger: 0, writes, closes, exits
'   block      the same through a `database` block's `linger:`
'   refusals   the option's refusals, each beside its legal neighbour
load gsql

database quickblock( file: "lifetime-block.gsql", create: true, linger: 0 )

program main()
    role = env("GSQL_ROLE")
    dbp = env("GSQL_DB")
    gate = env("GSQL_GATE")
    if role = "starter" then
        db = gsql.connect(dbp, { create: true })
        gsql.exec(db, "create table t (n integer)")
        gsql.exec(db, "insert into t (n) values (1)")
        print "ready"
        wait_for(gate)
        print "starter exits"
    else if role = "other" then
        db = gsql.connect(dbp)
        print "first " + string(gsql.query(db, "select count(*) as c from t")[0].c)
        wait_for(gate)
        gsql.exec(db, "insert into t (n) values (2)")
        print "after the starter exited " + string(gsql.query(db, "select count(*) as c from t")[0].c)
    else if role = "quick" then
        db = gsql.connect(dbp, { create: true, linger: 0 })
        gsql.exec(db, "create table if not exists q (n integer)")
        gsql.close(db)
        print "quick done"
    else if role = "block" then
        gsql.exec(quickblock, "create table if not exists q (n integer)")
        gsql.close(quickblock)
        print "block done"
    else if role = "refusals" then
        refusals(dbp)
    end if
end program

function wait_for(gate)
    while not exists({file}gate)
        sleep(0.05)
    end while
    return nothing
end function

function refused(path, opts, phrase)
    on error goto next
    db = gsql.connect(path, opts)
    if not error then
        gsql.close(db)
        print "MISMATCH accepted " + string(opts) + ", wanted a refusal naming " + phrase
        return nothing
    end if
    if contains(error.message, phrase) then
        print "ok refused " + string(opts)
    else
        print "MISMATCH " + string(opts) + " refused saying: " + error.message
    end if
    return nothing
end function

function refusals(dbp)
    refused(dbp, { create: true, linger: -1 }, "from 0 to 86400")
    refused(dbp, { create: true, linger: "3" }, "from 0 to 86400")
    refused(dbp, { create: true, linger: 100000 }, "from 0 to 86400")
    refused(dbp, { create: true, lingr: 3 }, "its options are `create` and `linger`")
    ' the legal neighbours: a whole and a fractional linger both connect
    db = gsql.connect(dbp, { create: true, linger: 2 })
    gsql.close(db)
    db = gsql.connect(dbp, { linger: 0.25 })
    gsql.close(db)
    print "ok accepted linger: 2 and linger: 0.25"
    return nothing
end function
