' A G-SQL connection's and block's methods are gsql's verbs
' (docs/handle_methods_design.md): `add.exec(row)` is `gsql.exec(add, row)`,
' `db.close()` is `gsql.close(db)`. Each method is asked beside the module
' form, and they must agree.
load gsql

function check(what, got, want)
    if string(got) = string(want) and type(got) = type(want) then
        print "ok " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function hello(x)
    return "record method " + string(x)
end function

' counts through main's record, a field write reaching the caller's frame
function pick(blocks)
    tally.calls = tally.calls + 1
    return blocks[0]
end function

program main(args)
    ' run from a scratch directory: the runner removes it
    database db( file: "methods.gsql", create: true, linger: 0 )
    sql setup on db
        create table t (id integer primary key, name text);
        insert into t values (1, 'a'), (2, 'b')
    end sql
    sql rows on db
        select * from t order by id
    end sql
    sql add on db
        insert into t values (:id, :name)
    end sql

    check("a block's exec", setup.exec().command, "INSERT")
    check("a block's query", len(rows.query()), 2)
    check("the module form agrees", string(gsql.query(rows)), string(rows.query()))
    r = add.exec({ id: 3, name: "c" })
    check("a block's exec takes its record", r.rows_affected, 1)
    check("rows through exec", len(rows.exec().rows), 3)

    ' a receiver that is an expression, evaluated once
    blocks = [rows]
    check("a block in an array", len(blocks[0].query()), 3)
    tally = { calls: 0 }
    got = pick(blocks).query()
    check("a call receiver is evaluated once", tally.calls, 1)
    check("and answers", len(got), 3)

    ' a connection's methods
    b = db.begin()
    r = add.exec({ id: 4, name: "d" })
    b = db.rollback()
    check("begin and rollback through methods", len(rows.query()), 3)
    b = db.begin()
    r = db.exec(add, { id: 5, name: "e" })
    b = db.commit()
    check("a connection's exec takes a block", len(rows.query()), 4)
    sql count_on on db
        select count(*) as n from t
    end sql
    total = 0
    for each row in db.on(count_on)
        total = row.n
    next
    check("a connection's on", total, 4)

    ' a verb that does not take the handle first refuses, in its own words
    on error goto next
    x = db.connect()
    check("connect is refused on a connection", contains(error.message, "gsql.connect"), true)
    e = error
    on error stop

    ' a record's own function field still wins over a module verb
    g = { kind: "window", read: hello }
    check("a record's method wins", g.read(7), "record method 7")
    gsql = { exec: hello }
    check("a variable named like the module wins", gsql.exec(8), "record method 8")

    c = db.close()
    print "done"
end program
