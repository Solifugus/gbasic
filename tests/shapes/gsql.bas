' docs/shapes_design.md §6.3, increment 3: G-SQL rows read into a shape, and
' a shape checked against its table (gsql.expect, ruled Q5). Every expected
' line in gsql.out was worked out by hand. The runner sets GSQL_DIR to a
' scratch directory.
load gsql

program main(args)
    db = gsql.connect(env("GSQL_DIR") + "/shapes", { create: true, linger: 0 })
    db.exec("create table customer (id integer primary key, name text, balance money(USD), opened date, active boolean, notes record, since datetime, waits duration, opens time)")
    db.exec("insert into customer (id, name, balance, opened, active, since, waits, opens) values (1, 'Ann', 12.5, '2026-01-05', true, '2026-01-05 09:30:00', '2 days', '09:00:00'), (2, 'Bo', 3, '2026-02-01', false, '2026-02-01 10:00:00', '1 hour', '10:30:00'), (3, 'Cy', 0, '2026-03-01', true, '2026-03-01 11:00:00', '3 days', '08:00:00')")

    ' the table holds the shape, so nothing is said; as a handle's method too
    gsql.expect(db, "customer", customer)
    db.expect("customer", customer)
    print "customer holds its shape"

    ' a row converts into the shape as any record does
    for each c:customer in db.query("select id, name, balance, opened, active, since, waits, opens from customer order by id")
        print string(c.id) + " " + c.name + " " + string(c.balance) + " " + string(c.opened) + " " + string(c.active)
    end for

    ' and from a path; the element is an instance, with the shape's methods
    for each r:customer in db.customer{ id, name, balance, opened, active, since, waits, opens }[.active]
        print r.name + " " + string(r.owed())
    end for

    ' a shape the table does not hold: every problem named at once
    print expect_problems(db, "customer", wrong)

    ' a shape held in a record column
    db.exec("update customer set notes = $1 where id = 1", [{ says: "vip", by: "Di" }])
    for each n:noted in db.query("select id, notes from customer where id = 1")
        print n.notes.says + " " + n.notes.by
    end for
end program

function expect_problems(db, table, shape)
    on error goto next
    gsql.expect(db, table, shape)
    e = error
    if e then
        print e.message
        return e.details.problems
    end if
    return "no problems"
end function

shape customer
    id:number
    name:text
    balance:money(USD)
    opened:date
    active:boolean = false
    since:datetime
    waits:duration
    opens:time
    function owed()
        return this.balance * 2
    end function
end shape

shape note
    says:text
    by:text
end shape

shape noted
    id:number
    notes:note
end shape

shape wrong
    name:number
    balance:money(EUR)
    gone:text = ""
    opened:note
    spot:file
    anything = nothing
end shape
