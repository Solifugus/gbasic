' The `gsql` module from a program (docs/gsql_module_design.md): every answer
' below is written here by hand, and each line prints `ok` or a MISMATCH naming
' both sides. Run by tests/run_gsql_module.sh, which sets GSQL_DIR to an empty
' directory and GSQL_PHASE to the part to run.
load gsql

checks = { n: 0, bad: 0 }


' `checks` is the global record: a function writing one of its fields
' reaches it, where a record passed as an argument would be a copy
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

' What a refusal said, surviving one that did not happen: a perturbation that
' stops a refusal must report a MISMATCH naming it, not end the fixture on a
' field read of `nothing` and hide the line that explains why.
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

function main_checks(dir)
    on error goto next
    db = gsql.connect(dir + "/main", { create: true })
    if error then
        print "MISMATCH connect: " + error.message
        checks.bad = checks.bad + 1
        return nothing
    end if
    on error stop
    check("type of a connection", type(db), "gsql_connection")

    ' --- exec answers what sqlite, pg and odbc answer -----------------------
    r = gsql.exec(db, "create table item (id integer primary key, name string not null, price money, seen date, ok boolean)")
    check("CREATE command", r.command, "CREATE")
    check("CREATE has no row count", r.rows_affected, nothing)
    r = gsql.exec(db, "insert into item (id, name, price, seen, ok) values (:id, :name, :price, :seen, :ok)", { id: 1, name: "apple", price: {USD}"0.50", seen: {date}"2026-10-30", ok: true })
    check("INSERT command", r.command, "INSERT")
    check("INSERT affects one row", r.rows_affected, 1)
    gsql.exec(db, "insert into item (id, name, price) values ($1, $2, $3)", [2, "pear", {JPY}"120"])
    gsql.exec(db, "insert into item (id, name) values ($1, $2)", [9007199254740993, "a" + chr(0) + "b"])

    ' --- values come back as what they were --------------------------------
    rows = gsql.query(db, "select id, name, price, seen, ok from item order by id")
    check("three rows", len(rows), 3)
    check("a row is a record", type(rows[0]), "record")
    check("row 1 id", rows[0].id, 1)
    check("row 1 name", rows[0].name, "apple")
    check("money stays money", type(rows[0].price), "money")
    check("money keeps its amount", rows[0].price * 3, {USD}"1.50")
    check("money keeps its currency", money.currency(rows[1].price), "JPY")
    check("a date stays a date", type(rows[0].seen), "datetime")
    check("the date's day", rows[0].seen.day, 30)
    check("a boolean stays a boolean", rows[0].ok, true)
    check("an absent value is unknown", type(rows[1].seen), "unknown")
    ' 2^53 + 1 is exact only if the integer crossed the wire as one: as a
    ' double it is 2^53, and adding one finds that out
    check("an exact integer past 2^53", rows[2].id + 1, 9007199254740994)
    check("a NUL inside text survives", len(rows[2].name), 3)
    check("column order is the select list's", join(keys(rows[0]), ","), "id,name,price,seen,ok")

    r = gsql.exec(db, "update item set name = name || '!' where id < 3")
    check("UPDATE affects two rows", r.rows_affected, 2)
    r = gsql.exec(db, "delete from item where id = 2")
    check("DELETE affects one row", r.rows_affected, 1)
    check("a computed column's name", gsql.query(db, "select 1 + 2 as three")[0].three, 3)

    ' --- results longer than a batch are fetched whole ---------------------
    gsql.exec(db, "create table n (k integer primary key)")
    gsql.begin(db)
    for i = 1 to 1234
        gsql.exec(db, "insert into n (k) values ($1)", [i])
    next
    gsql.commit(db)
    many = gsql.query(db, "select k from n order by k")
    check("every row of a 1234-row result", len(many), 1234)
    total = 0
    for each m in many
        total = total + m.k
    next
    check("and each once", total, 1234 * 1235 / 2)
    check("the last row is the last", many[1233].k, 1234)
    check("a descending scan", gsql.query(db, "select k from n order by k desc limit 1")[0].k, 1234)

    ' --- transactions -------------------------------------------------------
    gsql.begin(db)
    gsql.exec(db, "insert into n (k) values (5000)")
    gsql.rollback(db)
    check("a rollback leaves nothing", len(gsql.query(db, "select k from n where k = 5000")), 0)
    gsql.begin(db, "write")
    gsql.exec(db, "insert into n (k) values (5001)")
    other = gsql.connect(dir + "/main")
    check("another connection cannot see an uncommitted row", len(gsql.query(other, "select k from n where k = 5001")), 0)
    gsql.commit(db)
    check("and sees it once committed", len(gsql.query(other, "select k from n where k = 5001")), 1)
    gsql.close(other)

    ' --- refusals, each with its code -----------------------------------------
    e = attempt_query(db, "selec 1", nothing)
    check("a syntax error raises", type(e), "record")
    check("its code", code_of(e), "SYNTAX")
    check("its position", code_of(e) = "SYNTAX" and e.details.position = 1, true)
    check("its source", type(e) = "record" and e.source = "gsql", true)
    check("it names the statement", contains(message_of(e), "-- in \"selec 1\""), true)
    e = attempt_query(db, "select k from n where k = 'one'", nothing)
    check("no implicit cast", code_of(e), "EXPRESSION")
    e = attempt_query(db, "select x from nowhere", nothing)
    check("an unknown table", code_of(e), "NOT_FOUND")
    e = attempt_exec(db, "insert into n (k) values (1)", nothing)
    check("a duplicate key", code_of(e), "DUPLICATE")
    e = attempt_exec(db, "insert into n (k) values (:k)", [7])
    check(":name wants a record", contains(message_of(e), "must be a record"), true)
    e = attempt_query(db, "select k, k from n", nothing)
    check("two columns with one name", code_of(e), "DUPLICATE")
    check("the remedy is named", contains(message_of(e), "AS"), true)
    check("the connection still works after that", len(gsql.query(db, "select k from n where k = 3")), 1)
    e = attempt_exec(db, "select k from n", nothing)
    check("exec over rows", contains(message_of(e), "use gsql.query"), true)
    e = attempt_query(db, "delete from n where k = 3", nothing)
    check("query over a statement with no rows", contains(message_of(e), "it ran, and affected 1"), true)
    check("and it did run", len(gsql.query(db, "select k from n where k = 3")), 0)
    gsql.begin(db, "read")
    e = attempt_exec(db, "insert into n (k) values (6000)", nothing)
    check("a write in a read transaction", code_of(e), "READ_ONLY")
    gsql.rollback(db)
    e = attempt_commit(db)
    check("commit with nothing begun", code_of(e), "STATE")

    ' --- what the module refuses before sending anything ----------------------
    e = attempt_exec(db, "insert into n (k) values ($1)", "7")
    check("parameters must be a record or an array", contains(message_of(e), "a record (for :name) or an array"), true)
    e = attempt_connect(dir + "/absent", nothing)
    check("a database that is not there", code_of(e), "NOT_FOUND")
    check("and how to make one", contains(message_of(e), "create: true"), true)
    e = attempt_connect(dir + "/main", { creat: true })
    check("a misspelt option is refused by name", contains(message_of(e), "no option `creat`"), true)
    on error goto next
    gsql.begin(db, "readonly")
    check("a mode that is neither", contains(error.message, "\"read\" or \"write\""), true)
    if error then
    end if
    on error stop
    gsql.close(db)
    e = attempt_query(db, "select 1 as one", nothing)
    check("a closed connection", contains(message_of(e), "this connection is closed"), true)
    return nothing
end function

' --- the owner exits under a program that is using it ------------------------
function lost_checks(dir)
    db = gsql.connect(dir + "/lost", { create: true })
    gsql.exec(db, "create table t (k integer primary key)")
    gsql.exec(db, "insert into t (k) values (1)")
    write({file}(dir + "/ready"), "ready")
    ' the runner kills the owner and then says so
    while not exists({file}(dir + "/killed"))
        sleep(0.05)
    end while
    e = attempt_query(db, "select k from t", nothing)
    check("the request in flight fails", code_of(e), "LOST")
    check("saying nothing was retried", contains(message_of(e), "nothing was retried"), true)
    rows = gsql.query(db, "select k from t")
    check("the next request reaches a new owner", len(rows), 1)
    check("which recovered what was committed", rows[0].k, 1)
    gsql.close(db)
    return nothing
end function

function attempt_query(db, sql, params)
    on error goto next
    if type(params) = "nothing" then
        x = gsql.query(db, sql)
    else
        x = gsql.query(db, sql, params)
    end if
    ' bare `error` acknowledges and yields the error once; `false` after
    e = error
    if e then
        return e
    end if
    return nothing
end function

function attempt_exec(db, sql, params)
    on error goto next
    if type(params) = "nothing" then
        x = gsql.exec(db, sql)
    else
        x = gsql.exec(db, sql, params)
    end if
    ' bare `error` acknowledges and yields the error once; `false` after
    e = error
    if e then
        return e
    end if
    return nothing
end function

function attempt_commit(db)
    on error goto next
    gsql.commit(db)
    ' bare `error` acknowledges and yields the error once; `false` after
    e = error
    if e then
        return e
    end if
    return nothing
end function

function attempt_connect(path, options)
    on error goto next
    if type(options) = "nothing" then
        x = gsql.connect(path)
    else
        x = gsql.connect(path, options)
    end if
    ' bare `error` acknowledges and yields the error once; `false` after
    e = error
    if e then
        return e
    end if
    return nothing
end function

phase = env("GSQL_PHASE")
dir = env("GSQL_DIR")
if phase = "main" then
    main_checks(dir)
else
    if phase = "lost" then
        lost_checks(dir)
    end if
end if
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
