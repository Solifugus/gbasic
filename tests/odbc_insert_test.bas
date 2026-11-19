' odbc.insert (docs/bulk_insert_design.md §4): many rows through parameter
' arrays, all or nothing, the refused row named by its index. The oracle is
' the database read back with odbc.query. Run by run_odbc.sh against
' GBASIC_ODBC_CONNECTION (the hermetic SQLite3 driver unless set).
load odbc

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
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

function ids(db)
    got = []
    for each row in db.query("select id from gb_oi order by id")
        got = append(got, number(row.id))
    end for
    return got
end function

db = odbc.connect(env("GBASIC_ODBC_CONNECTION"))
on error goto next
x = db.exec("drop table gb_oi")
e = error
on error stop
x = db.exec("create table gb_oi (id int primary key, amt decimal(12,2), note varchar(200), ok boolean, d date, n double precision)")

' --- every kind, read back by the database -----------------------------------
hard = "tab" + chr(9) + "here, nl" + chr(10) + "there, quote ' and " + chr(34) + ", 日本語 ☃"
r = db.insert("gb_oi", [
    { id: 1, amt: {USD}"12.34", note: hard, ok: true, d: {date}"2026-11-18", n: 0.5 },
    { id: 2, amt: 7.5, note: unknown, ok: false, d: unknown, n: nothing },
    { id: 3, amt: unknown, note: "", ok: unknown, d: {date}"1999-12-31", n: -2 }])
check("rows_affected", r.rows_affected, 3)
check("command", r.command, "INSERT")
back = db.query("select id, amt, note, ok, d, n from gb_oi order by id")
check("three rows", len(back), 3)
check("money into decimal(12,2)", string(back[0].amt), "12.34")
check("an exact decimal into decimal(12,2), its scale the column's largest", number(back[1].amt), 7.5)
check("a text with tab, newline, both quotes and Unicode", back[0].note, hard)
check("a boolean true", back[0].ok = true or back[0].ok = 1, true)
check("a boolean false", back[1].ok = false or back[1].ok = 0, true)
check("a date", string(back[0].d), "2026-11-18")
check("a double", number(back[0].n), 0.5)
check("unknown is NULL", back[1].note = nothing, true)
check("nothing is NULL too", back[1].n = nothing, true)
check("the empty text is the empty text, not NULL", back[2].note, "")

' --- refused before anything is sent -------------------------------------------
a = attempt(db, "gb_oi", [{ id: 10, amt: 1 }, { id: 11, note: "x" }])
check("different columns", a.message, "odbc.insert: rows[1] names id, note where rows[0] names id, amt; every row names the same columns (unknown for a NULL)")
check("  named by index", a.details.index, 1)
a = attempt(db, "gb_oi", [{ id: 10, note: "x" }, { id: 11, note: 5 }])
check("a column of two kinds", a.message, "odbc.insert: rows[1].note is number where the column's earlier rows are text; one column is bound as one kind")
check("  named by index", a.details.index, 1)
a = attempt(db, "gb_oi", [{ id: 10, note: unknown }, { id: 11, note: "x" }, { id: 12, note: true }])
check("unknown does not decide a column's kind", a.details.index, 2)
a = attempt(db, "gb_oi", [{ id: 10 }, 5])
check("not a record", a.message, "odbc.insert: rows[1] is number, not a record of column values")
a = attempt(db, "gb_oi", [{}])
check("an empty record", a.message, "odbc.insert: rows[0] is an empty record, so it names no column")
a = attempt(db, "gb_oi", "rows")
check("not an array", a.message, "odbc.insert: the rows are an array of records, one per row")
a = attempt(db, "gb_oi", [{ id: 10, note: [1, 2] }])
check("a kind odbc cannot send", a.message, "odbc.insert: rows[0].note is array, which odbc cannot send")
a = attempt(db, "no_such_table_gb", [{ id: 1 }])
check("an unknown table is refused as odbc.insert's", starts_with(a.message, "odbc.insert: "), true)
check("none of the refusals kept a row", ids(db), [1, 2, 3])

' --- a row the database refuses undoes the call ------------------------------
a = attempt(db, "gb_oi", [{ id: 20 }, { id: 21 }, { id: 1 }, { id: 22 }])
check("a duplicate key names the caller's row", starts_with(a.message, "odbc.insert: rows[2]: "), true)
check("  its index", a.details.index, 2)
check("  and nothing of the call was kept", ids(db), [1, 2, 3])
' past one batch of 1000: the failure is in the third, and its row is exact
many = []
for i = 1000 to 3499
    many = append(many, { id: i })
end for
many[2100] = { id: 1 }
a = attempt(db, "gb_oi", many)
check("a refused row in the third batch is named exactly", a.details.index, 2100)
check("  and none of the first two batches was kept", ids(db), [1, 2, 3])
many[2100] = { id: 3100 + 2500 }
r = db.insert("gb_oi", many)
check("2500 rows over three batches", r.rows_affected, 2500)
check("  all of them there", len(db.query("select id from gb_oi where id >= 1000")), 2500)
x = db.exec("delete from gb_oi where id >= 1000")

' --- inside the caller's transaction -----------------------------------------
x = db.begin()
r = db.insert("gb_oi", [{ id: 100 }])
a = attempt(db, "gb_oi", [{ id: 101 }, { id: 1 }])
check("refused inside a transaction, the caller is told to roll back", contains(a.message, "inside your transaction: roll it back"), true)
x = db.rollback()
check("the caller's rollback undoes the call and its own work", ids(db), [1, 2, 3])
x = db.begin()
r = db.insert("gb_oi", [{ id: 200 }, { id: 201 }])
x = db.commit()
check("a call inside a committed transaction is kept", ids(db), [1, 2, 3, 200, 201])

' --- an empty array ----------------------------------------------------------
check("an empty array sends nothing", db.insert("gb_oi", []), { command: "INSERT", rows_affected: 0 })

' --- the speed, as a ratio (run_odbc.sh prints it) ---------------------------
x = db.exec("delete from gb_oi")
rows = []
for i = 1 to 20000
    rows = append(rows, { id: i, amt: i * 1.25, note: "row " + string(i) })
end for
t0 = monotonic()
r = db.insert("gb_oi", rows)
t1 = monotonic()
check("20000 rows", r.rows_affected, 20000)
x = db.exec("delete from gb_oi")
t2 = monotonic()
x = db.begin()
for each row in rows
    x = db.exec("insert into gb_oi (id, amt, note) values (?, ?, ?)", [row.id, row.amt, row.note])
end for
x = db.commit()
t3 = monotonic()
print "speed: insert " + string(t1 - t0) + " s, exec loop " + string(t3 - t2) + " s"
x = db.exec("drop table gb_oi")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
