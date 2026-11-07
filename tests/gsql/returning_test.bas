' INSERT ... RETURNING and `?` parameters through the real owner (Transward's
' ask: the id a serial gave a row, which a program had no way to learn). The
' rows come back from gsql.query, from gsql.exec beside the count, from a
' `sql` block, and inside a transaction; every answer is written here by
' hand. Run by run_gsql_module.sh's returning tier, with GSQL_DIR set.
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

function code_of(e)
    if type(e) = "record" and contains(keys(e), "details") then
        return e.details.code
    end if
    return "(no error)"
end function

dir = env("GSQL_DIR")
db = gsql.connect(dir + "/ret", { create: true, linger: 0 })
db.exec("create table rule (id integer serial primary key, channel integer not null, event text, on_ text default 'yes')")

' --- the id given, by query and by exec ---------------------------------------
rows = db.query("insert into rule (channel, event) values (?, ?) returning id", [7, "start"])
check("query answers the new row's id", rows, [{ id: 1 }])
r = db.exec("insert into rule (channel, event) values (?, ?), (?, ?) returning id, on_", [7, "a", 8, "b"])
check("exec answers the command", r.command, "INSERT")
check("and counts the rows it returned", r.rows_affected, 2)
check("and the rows, defaults filled in", r.rows, [{ id: 2, on_: "yes" }, { id: 3, on_: "yes" }])
check("exec with no RETURNING is unchanged", db.exec("insert into rule (channel) values (?)", [9]),
      { command: "INSERT", rows_affected: 1 })

' --- a block, its :name parameters from variables -----------------------------
database rdb()
sql add_rule on rdb
    insert into rule (channel, event) values (:channel, :event) returning id, event
end sql
channel = 4
event = "stop"
r = gsql.exec(db, add_rule)
check("a block's exec answers its rows", r.rows, [{ id: 5, event: "stop" }])
got = []
for each row in gsql.on(db, add_rule)
    append(got, row.id)
next
check("and for each iterates them", got, [6])

' --- inside a transaction: the rows come back, and a rollback takes them ------
gsql.begin(db)
rows = db.query("insert into rule (channel) values (?) returning id", [1])
check("inside a transaction", rows, [{ id: 7 }])
gsql.rollback(db)
check("rolled back", db.query("select count(*) as n from rule where id = 7")[0].n, 0)
check("and the serial is not used up", db.query("insert into rule (channel) values (1) returning id"), [{ id: 7 }])

' --- refusals ------------------------------------------------------------------
on error goto next
db.exec("select id from rule where id = ? and channel = $1", [1])
check("? beside $n is refused", code_of(error), "SYNTAX")
db.exec("update rule set event = 'x' returning id")
check("RETURNING after UPDATE is refused", code_of(error), "UNSUPPORTED")
db.query("insert into rule (event) values ('no channel') returning id")
check("a refused row returns nothing and raises", code_of(error), "NOT_NULL")
on error stop
check("nothing was kept", db.query("select count(*) as n from rule")[0].n, 7)

db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
