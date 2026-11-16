' `wait:` on gsql.connect and in a database block's head: how long to wait for
' the database's owner to answer (or one this connect started to say it is
' ready), 10 s by default. Run by tests/run_gsql_module.sh, which holds the
' database's lock from outside (so no owner can ever answer) and times each
' mode from outside too:
'
'   gbasic wait_test.bas DB connect SECONDS   gsql.connect(DB, { wait: SECONDS })
'   gbasic wait_test.bas DB default           gsql.connect(DB), the 10 s
'   gbasic wait_test.bas DB options           the refusals and their neighbours
'
' Each connect prints REFUSED <code>: <message> or CONNECTED.
load gsql

function attempt(db, opts)
    on error goto bad
    if is_unknown(opts) then
        c = gsql.connect(db)
    else
        c = gsql.connect(db, opts)
    end if
    gsql.close(c)
    return "CONNECTED"
bad:
    out = "REFUSED " + default(error.details.code, "no code") + ": " + error.message
    error.clear()
    return out
end function

function refused(db, opts, phrase)
    got = attempt(db, opts)
    if contains(got, phrase) then
        print "ok refused " + string(opts)
    else
        print "MISMATCH " + string(opts) + ": " + got
    end if
    return nothing
end function

program main(args)
    db = args[0]
    mode = args[1]
    if mode = "connect" then
        print attempt(db, { wait: number(args[2]) })
    else if mode = "default" then
        print attempt(db, unknown)
    else if mode = "options" then
        refused(db, { create: true, wait: 0 }, "more than 0 and up to 3600 seconds")
        refused(db, { create: true, wait: -1 }, "more than 0 and up to 3600 seconds")
        refused(db, { create: true, wait: 3601 }, "more than 0 and up to 3600 seconds")
        refused(db, { create: true, wait: "5" }, "more than 0 and up to 3600 seconds")
        refused(db, { create: true, wiat: 5 }, "its options are `create`, `linger` and `wait`")
        ' the legal neighbours: seconds, a fraction, a duration, the top
        ok = attempt(db, { create: true, wait: 5 }) = "CONNECTED"
        ok = ok and attempt(db, { wait: 0.25 }) = "CONNECTED"
        ok = ok and attempt(db, { wait: 30 seconds }) = "CONNECTED"
        ok = ok and attempt(db, { wait: 3600 }) = "CONNECTED"
        if ok then
            print "ok accepted wait: 5, 0.25, 30 seconds and 3600"
        else
            print "MISMATCH a legal wait was refused"
        end if
    end if
end program
