' pg.insert (docs/bulk_insert_design.md): many rows through COPY, all or
' nothing, the refused row named by its index. The oracle is Postgres read
' back with pg.query, never the module's own COPY text. Run by
' run_postgres.sh (GBASIC_POSTGRES_TEST=1, a PGDATABASE it may write).
load pg

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if string(got) = string(want) then
        return nothing
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    return nothing
end function

' the refusal's message and details, or "accepted"
function attempt(db, table, rows)
    on error goto next
    r = db.insert(table, rows)
    e = error
    if e then
        return { message: e.message, details: e.details }
    end if
    return { message: "accepted", details: {} }
end function

db = pg.connect({})
x = db.exec("drop table if exists gb_insert_test")
x = db.exec("create table gb_insert_test (id int primary key, amt numeric(12,2), note text, ok boolean, d date, at timestamptz, tags text[], n double precision)")

' --- every kind, read back by Postgres ---------------------------------------
hard = "tab" + chr(9) + "here, nl" + chr(10) + "there, cr" + chr(13) + ", back\\slash, \\N literal, 日本語 ☃"
r = db.insert("gb_insert_test", [
    { id: 1, amt: 12.34, note: hard, ok: true, d: {date}"2026-11-18", at: {datetime}"2026-11-18 21:00:00", tags: ["a", "b c", "d,e"], n: 0.1 },
    { id: 2, amt: unknown, note: unknown, ok: false, d: unknown, at: unknown, tags: unknown, n: unknown },
    { id: 3, amt: 9999999999.99, note: "", ok: true, d: {date}"1999-12-31", at: {datetime}"2000-01-01 00:00:00", tags: [], n: -1e300 }])
check("rows_affected", r.rows_affected, 3)
check("command", r.command, "INSERT")
back = db.query("select id, amt::text as amt, note, ok, d::text as d, extract(epoch from at)::bigint as at, array_to_string(tags, '|') as tags, n from gb_insert_test order by id")
check("three rows", len(back), 3)
check("a text with tab, newline, CR, backslash, a literal \\N and Unicode", back[0].note, hard)
check("an exact decimal into numeric", back[0].amt, "12.34")
check("a boolean", back[0].ok, true)
check("a date", back[0].d, "2026-11-18")
check("an instant, as UTC", back[0].at, 1795035600)
check("a text array, comma and space kept", back[0].tags, "a|b c|d,e")
check("a double", back[0].n, 0.1)
check("unknown is NULL", is_unknown(back[1].amt) or back[1].amt = nothing, true)
check("unknown text is NULL, not the empty text", back[1].note = nothing, true)
check("the empty text is the empty text", back[2].note, "")
check("the largest numeric(12,2)", back[2].amt, "9999999999.99")
check("an empty array", back[2].tags, "")

' --- refused before anything is sent -------------------------------------------
a = attempt(db, "gb_insert_test", [{ id: 10, amt: 1 }, { id: 11, note: "x" }])
check("different columns", a.message, "pg.insert: rows[1] names id, note where rows[0] names id, amt; every row names the same columns (unknown for a NULL)")
check("  named by index", a.details.index, 1)
a = attempt(db, "gb_insert_test", [{ id: 10 }, 5])
check("not a record", a.message, "pg.insert: rows[1] is number, not a record of column values")
a = attempt(db, "gb_insert_test", [{}])
check("an empty record", a.message, "pg.insert: rows[0] is an empty record, so it names no column")
a = attempt(db, "gb_insert_test", [{ id: 10, note: "a" + chr(0) + "b" }])
check("a NUL in text, refused as pg.exec refuses it, with its row and column", a.message,
      "pg.insert: rows[0].note: PostgreSQL text cannot hold an interior NUL; send the value as bytea, or strip the NUL first")
check("  and its index", a.details.index, 0)
a = attempt(db, "gb_insert_test", "rows")
check("not an array", a.message, "pg.insert: the rows are an array of records, one per row")
a = attempt(db, "no_such_table_gb", [{ id: 1 }])
check("an unknown table is named, as pg.insert's", starts_with(a.message, "pg.insert: ERROR:  relation \"no_such_table_gb\" does not exist"), true)
a = attempt(db, "gb_insert_test", [{ id: 1, colour: "red" }])
check("an unknown column is named", contains(a.message, "column \"colour\" does not exist"), true)
check("none of the refusals kept a row", len(db.query("select id from gb_insert_test")), 3)

' --- a row the database refuses undoes the call ------------------------------
a = attempt(db, "gb_insert_test", [{ id: 20 }, { id: 21 }, { id: 1 }, { id: 22 }])
check("a duplicate key names the caller's row", starts_with(a.message, "pg.insert: rows[2]: ERROR:  duplicate key value"), true)
check("  its index", a.details.index, 2)
check("  and nothing of the call was kept", len(db.query("select id from gb_insert_test where id >= 20")), 0)
a = attempt(db, "gb_insert_test", [{ id: 30, amt: 123456789012.5 }])
check("a value its column cannot hold", a.details.index, 0)

' --- inside the caller's transaction, a savepoint ----------------------------
x = db.begin()
x = db.exec("insert into gb_insert_test (id) values (100)")
a = attempt(db, "gb_insert_test", [{ id: 101 }, { id: 100 }])
check("refused inside a transaction", a.details.index, 1)
r = db.insert("gb_insert_test", [{ id: 102 }])
check("the transaction goes on after the refused call", r.rows_affected, 1)
x = db.commit()
got = []
for each row in db.query("select id from gb_insert_test where id >= 100 order by id")
    got = append(got, row.id)
end for
check("the caller's work and the later call kept, the refused call undone", got, [100, 102])

' --- an empty array ----------------------------------------------------------
check("an empty array sends nothing", db.insert("gb_insert_test", []), { command: "INSERT", rows_affected: 0 })

' --- a schema-qualified name, and a name needing quotes ----------------------
x = db.exec("drop table if exists public.\"Gb Mixed\"")
x = db.exec("create table public.\"Gb Mixed\" (\"Some Col\" int)")
r = db.insert("public.Gb Mixed", [{ "Some Col": 7 }])
check("schema.table, each part quoted", db.query("select \"Some Col\" as v from public.\"Gb Mixed\"")[0].v, 7)
x = db.exec("drop table public.\"Gb Mixed\"")

' --- the speed, as a ratio (run_postgres.sh asserts it) ----------------------
x = db.exec("truncate gb_insert_test")
rows = []
for i = 1 to 20000
    rows = append(rows, { id: i, amt: i * 1.25, note: "row " + string(i) })
end for
t0 = monotonic()
r = db.insert("gb_insert_test", rows)
t1 = monotonic()
check("20000 rows", r.rows_affected, 20000)
x = db.exec("truncate gb_insert_test")
t2 = monotonic()
x = db.begin()
for each row in rows
    x = db.exec("insert into gb_insert_test (id, amt, note) values ($1, $2, $3)", [row.id, row.amt, row.note])
end for
x = db.commit()
t3 = monotonic()
print "speed: insert " + string(t1 - t0) + " s, exec loop " + string(t3 - t2) + " s"
x = db.exec("drop table gb_insert_test")

print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)
