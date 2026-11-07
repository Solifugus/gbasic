' gsql.insert(db, table, rows): many rows in one call (gdash's A3). The rows
' are records keyed by column name; a column a row leaves out takes its
' default; one refused row undoes the whole call, which names the caller's own
' row. Every answer is written here by hand. Run by run_gsql_module.sh's
' insert tier, with GSQL_DIR set.
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

' the error, or nothing; a perturbation that stops a refusal shows as a
' MISMATCH naming it rather than ending the fixture
function attempt_insert(db, table, rows)
    on error goto next
    x = gsql.insert(db, table, rows)
    e = error
    if e then
        return e
    end if
    return x
end function

function count_of(db, table)
    return gsql.query(db, "select count(*) as n from \"" + table + "\"")[0].n
end function

function field_of(e, name)
    if type(e) = "record" and contains(keys(e), name) then
        return e[name]
    end if
    return "(none)"
end function

    dir = env("GSQL_DIR")
    db = gsql.connect(dir + "/insert", { create: true, linger: 0 })
    db.exec("create table t (id number primary key, name text default 'none', amt money(USD) not null, n number check (n > 0))")

    ' --- rows go in, with defaults for what a row leaves out ----------------
    r = attempt_insert(db, "t", [{ id: 1, amt: {USD}"1", n: 1 }, { id: 2, name: "b", amt: "2.50", n: 2 }])
    check("the answer is exec's shape", r, { command: "INSERT", rows_affected: 2 })
    got = db.query("select id, name, amt from t order by id")
    check("a column left out takes its default", got[0].name, "none")
    check("text into money converts as INSERT does", got[1].amt, {USD}"2.50")
    check("an empty array inserts nothing", attempt_insert(db, "t", []), { command: "INSERT", rows_affected: 0 })

    ' --- one refused row undoes the call, and is named as the caller's ------
    e = attempt_insert(db, "t", [{ id: 3, amt: {USD}"1", n: 1 }, { id: 1, amt: {USD}"1", n: 1 }])
    check("a duplicate key", field_of(e, "message"),
          "gsql.insert: rows[1]: t: the primary key already holds that value [DUPLICATE]")
    check("its index in details, counted as the caller's array is", field_of(field_of(e, "details"), "index"), 1)
    check("and the code", field_of(field_of(e, "details"), "code"), "DUPLICATE")
    check("nothing of the call was kept", count_of(db, "t"), 2)
    e = attempt_insert(db, "t", [{ id: 4, amt: {USD}"1", n: -1 }])
    check("a CHECK names the row too, alone in its statement", field_of(field_of(e, "details"), "index"), 0)
    e = attempt_insert(db, "t", [{ id: 5, amt: {USD}"1", n: 1 }, { id: 6, n: 1 }])
    check("NOT NULL on a column left out", field_of(e, "message"),
          "gsql.insert: rows[1]: t.amt is NOT NULL, and the value is unknown [NOT_NULL]")
    e = attempt_insert(db, "t", [{ id: 7, amt: "x", n: 1 }])
    check("a value its column cannot take", field_of(field_of(e, "details"), "column"), "amt")
    e = attempt_insert(db, "nosuch", [{ id: 1 }])
    check("a table that is not there names no row", field_of(e, "message"),
          "gsql.insert: there is no table `nosuch` [NOT_FOUND]")
    e = attempt_insert(db, "t", [{ id: 8, nme: "typo", amt: {USD}"1", n: 1 }])
    check("a column that is not there", field_of(field_of(e, "details"), "code"), "NOT_FOUND")
    check("and still nothing kept", count_of(db, "t"), 2)

    ' --- refusals before anything is sent ----------------------------------
    e = attempt_insert(db, "t", [{ id: 9, amt: {USD}"1", n: 1 }, 7])
    check("a row that is not a record", field_of(e, "message"),
          "gsql.insert: rows[1] is number, not a record of column values")
    e = attempt_insert(db, "t", [{}])
    check("an empty record", field_of(e, "message"),
          "gsql.insert: rows[0] is an empty record, so it names no column")
    e = attempt_insert(db, "t", { id: 9 })
    check("one record, not an array", field_of(e, "message"),
          "gsql.insert expects the rows as an array of records, not record")

    ' --- many chunks: a refusal in the last undoes the first ---------------
    many = []
    i = 0
    while i < 5000
        append(many, { id: 100 + i, amt: {USD}"1", n: 1 })
        i = i + 1
    end while
    append(many, { id: 100, amt: {USD}"1", n: 1 })
    e = attempt_insert(db, "t", many)
    check("the 5001st row's duplicate, several statements in", field_of(field_of(e, "details"), "index"), 5000)
    check("and the 5000 before it were undone", count_of(db, "t"), 2)
    take_last(many)
    r = attempt_insert(db, "t", many)
    check("5000 rows over several statements", field_of(r, "rows_affected"), 5000)
    check("all of them there", count_of(db, "t"), 5002)

    ' --- inside the caller's transaction it is a savepoint -----------------
    db.begin("write")
    e = attempt_insert(db, "t", [{ id: 20000, amt: {USD}"1", n: 1 }, { id: 20000, amt: {USD}"1", n: 1 }])
    check("refused inside a transaction", field_of(field_of(e, "details"), "index"), 1)
    db.exec("insert into t (id, amt, n) values (20001, 1, 1)")
    db.commit()
    check("only the call was undone, the transaction went on", count_of(db, "t"), 5003)
    db.begin("write")
    r = attempt_insert(db, "t", [{ id: 20002, amt: {USD}"1", n: 1 }])
    db.rollback()
    check("and a rollback after it undoes it", count_of(db, "t"), 5003)

    ' --- names are quoted: reserved words, spaces, a quote -----------------
    db.exec("create table \"Order \"\"Lines\"\"\" (\"select\" number primary key, \"unit price\" money(USD))")
    r = attempt_insert(db, "Order \"Lines\"", [{ select: 1, "unit price": {USD}"3.50" }])
    check("a table and columns any name can have", field_of(r, "rows_affected"), 1)
    ' --- a statement's parameters past 1024 --------------------------------
    ' they were read into 1024 slots, so 1025 or more read as none at all
    ' ("no value was given for the parameter $1"), and each reference read
    ' the whole array again; found by A3's prototype
    marks = []
    params = []
    i = 0
    while i < 500
        append(marks, "($" + string(3 * i + 1) + ", $" + string(3 * i + 2) + ", $" + string(3 * i + 3) + ")")
        append(params, 30000 + i)
        append(params, {USD}"1")
        append(params, 1)
        i = i + 1
    end while
    r = db.exec("insert into t (id, amt, n) values " + join(marks, ", "), params)
    check("an exec with 1500 parameters", r.rows_affected, 500)
    check("and the last of them reached its row", db.query("select n from t where id = 30499")[0].n, 1)

    ' --- the method form ---------------------------------------------------
    db.exec("create table m (k number primary key)")
    check("db.insert is gsql.insert", db.insert("m", [{ k: 1 }, { k: 2 }]).rows_affected, 2)

    db.close()
    print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
