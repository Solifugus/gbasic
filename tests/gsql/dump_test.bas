' gsql.dump and gsql.restore (docs/gsql_dump_design.md). Every answer is
' written here by hand, and each line prints `ok` or a MISMATCH naming both
' sides. Run by tests/run_gsql_module.sh with GSQL_DIR an empty directory.
'
' THE ORACLE IS THE STORED VALUES, NOT THE DUMP'S TEXT: a dump of a restore
' being the same text as the dump it came from passes on a printer that loses
' information, since a value printed short restores short and prints the same
' way twice. So every table of the source and of the restored database is read
' back and compared row by row as `serialize` bytes, which tell an exact 3 from
' 3.0 and TIME '10:30' from TIME '10:30:00'.
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
    if type(e) != "record" then
        return "(no error)"
    end if
    if type(e.details) = "record" then
        if has(e.details, "code") then
            return e.details.code
        end if
    end if
    return "(no code)"
end function

function message_of(e)
    if type(e) = "record" then
        return e.message
    end if
    return "(no error)"
end function

' a refusal: its code, and a phrase of its sentence
function refused(label, e, code, phrase)
    check(label + " is refused " + code, code_of(e), code)
    check(label + " says why", contains(message_of(e), phrase), true)
    return nothing
end function

' every row of `table` in both databases, the same bytes
function same_rows(a, b, table, order)
    q = "select * from " + table + order
    ra = gsql.query(a, q)
    rb = gsql.query(b, q)
    check(table + ": the same number of rows", len(rb), len(ra))
    differ = 0
    n = len(ra)
    if len(rb) < n then
        n = len(rb)
    end if
    for i = 0 to n - 1
        if serialize(ra[i]) != serialize(rb[i]) then
            differ = differ + 1
            if differ <= 3 then
                print "MISMATCH " + table + " row " + string(i) + ": " + string(rb[i]) + ", was " + string(ra[i])
            end if
        end if
    next
    check(table + ": every row the same bytes", differ, 0)
    return nothing
end function

function build(db)
    ' --- one column per kind, and the awkward values of each -------------
    gsql.exec(db, "create table v (id integer primary key, n number, s text, m money, mu money(USD), d date, dt datetime, t time, b boolean, r record)")
    nums = [9007199254740993, 6 / 2, 0 - 0.0, number("nan"), number("inf"), number("-inf"), 0.1, 1 / 3, number("1e300"), number("5e-324"), 0 - 9223372036854775807 - 1, 9223372036854775807, 0]
    texts = ["", "a'b", "x; y -- z /* w */", "two" + chr(10) + "lines", "z" + chr(0) + "z", "back\\slash", "nul\\" + chr(0), "日本", "😀", "U&'not an escape'"]
    moneys = [{USD}"1.234567", {EUR}"-2.5", {JPY}"120", {KWD}"1.234", {USD}"-92233720.368547"]
    times = [{time}"10", {time}"10:30", {time}"10:30:05"]
    id = 0
    for each x in nums
        id = id + 1
        gsql.exec(db, "insert into v (id, n) values ($1, $2)", [id, x])
    next
    for each x in texts
        id = id + 1
        gsql.exec(db, "insert into v (id, s) values ($1, $2)", [id, x])
    next
    for each x in moneys
        id = id + 1
        gsql.exec(db, "insert into v (id, m) values ($1, $2)", [id, x])
    next
    for each x in times
        id = id + 1
        gsql.exec(db, "insert into v (id, t) values ($1, $2)", [id, x])
    next
    gsql.exec(db, "insert into v values (100, 1, 'all', $1, $2, $3, $4, $5, true, $6)", [{EUR}"3", {USD}"0.000001", {date}"2026-02-28", {datetime}"1999-12-31 23:59:59", {time}"00:00", { a: 1, b: [1, 2.5, "q'uote"], c: { d: unknown } }])
    gsql.exec(db, "insert into v (id, b) values (101, false)")
    gsql.exec(db, "insert into v (id) values (102)")

    ' --- names that must be quoted, a table with no key, a column added late
    gsql.exec(db, "create table " + chr(34) + "Order" + chr(34) + " (" + chr(34) + "select" + chr(34) + " integer primary key, " + chr(34) + "Mixed Case" + chr(34) + " text)")
    gsql.exec(db, "insert into " + chr(34) + "Order" + chr(34) + " values (1, 'x')")
    gsql.exec(db, "create table h (x text)")
    gsql.exec(db, "insert into h values ('b'), ('a'), (null), ('c')")
    gsql.exec(db, "create table g (id integer primary key)")
    gsql.exec(db, "insert into g values (1), (2)")
    gsql.exec(db, "alter table g add column c text default 'dflt'")
    gsql.exec(db, "insert into g values (3, 'set')")
    gsql.exec(db, "create table old (id integer primary key, u integer)")
    gsql.exec(db, "alter table old rename to renamed")
    gsql.exec(db, "create unique index renamed_u on renamed (u)")
    gsql.exec(db, "insert into renamed values (1, null), (2, null), (3, 7)")
    gsql.exec(db, "create index renamed_both on renamed (u desc, id)")

    ' --- generators: a serial whose top rows were deleted, a scoped one, random
    gsql.exec(db, "create table item (id integer serial primary key, name text not null unique)")
    gsql.exec(db, "insert into item (name) values ('a'), ('b'), ('c'), ('d')")
    gsql.exec(db, "delete from item where id >= 3")
    gsql.exec(db, "create table line (item integer not null, n integer serial of (item), qty integer check (qty > 0), primary key (item, n), foreign key (item) references item)")
    gsql.exec(db, "insert into line (item, qty) values (1, 5), (1, 6), (1, 7), (2, 1)")
    gsql.exec(db, "delete from line where item = 1 and n = 3")
    gsql.exec(db, "create table rnd (id text random primary key, tok integer random(12), n integer)")
    gsql.exec(db, "insert into rnd (n) values (1), (2), (3)")
    gsql.exec(db, "create table soft (id integer primary key, item integer, constraint soft_item foreign key (item) references item not enforced)")
    gsql.exec(db, "insert into soft values (1, 99)")

    ' --- views on views; a view and a key left without what they read ------
    gsql.exec(db, "create view cheap as select id, s from v where id < 5")
    gsql.exec(db, "create view cheaper (k) as select id from cheap where id > 1")
    gsql.exec(db, "create table gone (id integer primary key)")
    gsql.exec(db, "create table orphan (id integer primary key, g integer, constraint orphan_g foreign key (g) references gone)")
    gsql.exec(db, "create view broken as select id from gone")
    on warning ignore
    gsql.exec(db, "drop table gone")
    on warning stop

    ' --- more rows than a slice and than an INSERT statement holds ---------
    gsql.exec(db, "create table big (id integer primary key, v text)")
    gsql.begin(db)
    for i = 1 to 5000
        gsql.exec(db, "insert into big values ($1, $2)", [i, "row " + string(i)])
    next
    gsql.commit(db)
    return nothing
end function

function text_of(path)
    return read({file}path)
end function

' a dump's text without the lines naming what it could not write: the
' restored database never had those, so its own dump does not name them
function written_part(text)
    out = []
    for each l in split(text, chr(10))
        if not starts_with(l, "-- not written: ") then
            append(out, l)
        end if
    next
    return join(out, chr(10))
end function

function round_trip(dir)
    a = gsql.connect(dir + "/a.gsql", { create: true })
    build(a)
    full = dir + "/a.sql"
    r = gsql.dump(a, full)
    check("dump: tables", r.tables, 11)
    check("dump: rows", r.rows, 5054)
    check("dump: the broken view and the unbound key are not written", r.skipped, 2)
    check("dump: no partial file is left", exists({file}(full + ".partial")), false)
    text = text_of(full)
    check("the broken view is named in the file", contains(text, "-- not written: view broken"), true)
    check("text holding a NUL is written U&", contains(text, "U&'z\\0000z'"), true)
    check("a time keeps its precision", contains(text, "TIME '10:30'") and contains(text, "TIME '10'"), true)
    check("money is written at its storage scale", contains(text, "CAST('1.234567' AS MONEY(USD))"), true)
    check("a counter above every row is written", contains(text, "ALTER TABLE item ALTER COLUMN id RESTART WITH 4;"), true)

    b = gsql.connect(dir + "/b.gsql", { create: true })
    rr = gsql.restore(b, full)
    check("restore: rows", rr.rows, 5054)

    same_rows(a, b, "v", " order by id")
    same_rows(a, b, chr(34) + "Order" + chr(34), "")
    same_rows(a, b, "h", "")
    same_rows(a, b, "g", " order by id")
    same_rows(a, b, "renamed", " order by id")
    same_rows(a, b, "item", " order by id")
    same_rows(a, b, "line", " order by item, n")
    same_rows(a, b, "rnd", " order by n")
    same_rows(a, b, "soft", "")
    same_rows(a, b, "orphan", "")
    same_rows(a, b, "big", " order by id")
    same_rows(a, b, "cheaper", " order by k")

    ' the dump of the restore is the dump it came from
    gsql.dump(b, dir + "/b.sql")
    check("the restored database dumps the same text", text_of(dir + "/b.sql") = written_part(text), true)
    check("and the source's dump names exactly two things it did not write", len(split(text, "-- not written: ")) - 1, 2)
    gsql.dump(a, dir + "/a.schema", { schema_only: true })
    gsql.dump(b, dir + "/b.schema", { schema_only: true })
    st = text_of(dir + "/a.schema")
    check("schema only: the same text both ways", text_of(dir + "/b.schema") = written_part(st), true)
    check("schema only: no rows in it", contains(st, "INSERT"), false)
    check("schema only: no counters in it", contains(st, "RESTART"), false)

    ' a serial issues the same next number in both, never one already issued
    gsql.exec(a, "insert into item (name) values ('e')")
    gsql.exec(b, "insert into item (name) values ('e')")
    check("the next serial", gsql.query(b, "select max(id) as m from item")[0].m, 5)
    check("the same in the source", gsql.query(a, "select max(id) as m from item")[0].m, 5)
    gsql.exec(b, "insert into line (item, qty) values (1, 9)")
    check("the next scoped serial skips the deleted 3", gsql.query(b, "select max(n) as m from line where item = 1")[0].m, 4)

    ' the restored constraints are in force
    on error goto next
    gsql.exec(b, "insert into line (item, qty) values (1, 0)")
    refused("a CHECK carried by the restore", error, "CHECK", "line_qty_check")
    gsql.exec(b, "insert into line (item, qty) values (42, 1)")
    refused("a foreign key carried by the restore", error, "FOREIGN_KEY", "line")
    gsql.exec(b, "insert into item (name) values ('a')")
    refused("a UNIQUE carried by the restore", error, "DUPLICATE", "")
    gsql.exec(b, "insert into soft values (2, 98)")
    check("NOT ENFORCED carried by the restore", code_of(error), "(no error)")
    gsql.exec(b, "alter table item alter column id restart with 2")
    refused("RESTART WITH lowering a counter", error, "SERIAL", "never issued again")
    gsql.exec(b, "alter table item alter column id restart with 50")
    check("RESTART WITH raising it", code_of(error), "(no error)")
    on error stop
    gsql.close(a)
    gsql.close(b)
    return nothing
end function

function write_file(path, text)
    write({file}path, text)
    return nothing
end function

function refusals(dir)
    db = gsql.connect(dir + "/r.gsql", { create: true })
    gsql.exec(db, "create table rd (id integer primary key, r record)")
    gsql.exec(db, "insert into rd values (1, $1)", [{ when: {date}"2026-01-01" }])
    on error goto next
    gsql.dump(db, dir + "/rd.sql")
    refused("a record holding a date", error, "TYPE", "cannot write as text")
    check("no file is left by a refused dump", exists({file}(dir + "/rd.sql")), false)
    check("nor a partial one", exists({file}(dir + "/rd.sql.partial")), false)
    gsql.exec(db, "delete from rd")
    gsql.exec(db, "create table ms (c money, n integer serial of (c), primary key (c, n))")
    gsql.exec(db, "insert into ms (c) values ($1)", [{USD}"1"])
    gsql.dump(db, dir + "/ms.sql")
    refused("a serial numbered within money", error, "TYPE", "money")
    gsql.exec(db, "drop table ms")
    gsql.dump(db, dir + "/o.sql", { schemaonly: true })
    refused("an unknown option", error, "(no code)", "the one option is `schema_only`")
    gsql.dump(db, dir + "/o.sql", { schema_only: 1 })
    refused("schema_only that is not a boolean", error, "(no code)", "must be true or false")

    ' a restore runs a dump's statements, onto tables it made, and nothing else
    gsql.exec(db, "create table kept (id integer primary key)")
    gsql.exec(db, "insert into kept values (1)")
    write_file(dir + "/del.sql", "create table t1 (id integer primary key);" + chr(10) + "delete from kept;" + chr(10))
    gsql.restore(db, dir + "/del.sql")
    e = error
    refused("a DELETE in the file", e, "STATE", "only CREATE TABLE")
    check("the DELETE is reported at its line", contains(message_of(e), "line 2 of"), true)
    write_file(dir + "/ins.sql", "insert into kept values (2);")
    gsql.restore(db, dir + "/ins.sql")
    refused("an INSERT into a table already there", error, "STATE", "did not create")
    write_file(dir + "/rep.sql", "create or replace view vv as select 1 as x;")
    gsql.restore(db, dir + "/rep.sql")
    refused("CREATE OR REPLACE VIEW", error, "STATE", "replace a view")
    write_file(dir + "/sel.sql", "create table t2 (id integer primary key);" + chr(10) + "insert into t2 select id from kept;")
    gsql.restore(db, dir + "/sel.sql")
    refused("INSERT ... SELECT", error, "STATE", "INSERT ... SELECT")
    write_file(dir + "/open.sql", "create table t3 (s text);" + chr(10) + "insert into t3 values ('never closed);")
    gsql.restore(db, dir + "/open.sql")
    refused("a string never closed", error, "SYNTAX", "ends inside a string")
    write_file(dir + "/bad.sql", "create table t4 (id integer primary key);" + chr(10) + chr(10) + "-- a comment" + chr(10) + "insert into t4 values (1), (1);")
    gsql.restore(db, dir + "/bad.sql")
    refused("a duplicate in the rows", error, "DUPLICATE", "line 4 of")
    on error stop
    check("kept is untouched by every refusal", gsql.query(db, "select count(*) as n from kept")[0].n, 1)
    check("the refused INSERT's batch was rolled back", gsql.query(db, "select count(*) as n from t4")[0].n, 0)
    ' a restore into a database holding other tables: they are not touched
    write_file(dir + "/ok.sql", "create table t5 (id integer primary key); insert into t5 values (1), (2); -- the end")
    r = gsql.restore(db, dir + "/ok.sql")
    check("a restore beside other tables", r.rows, 2)
    gsql.close(db)
    return nothing
end function

dir = env("GSQL_DIR")
round_trip(dir)
refusals(dir)
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
