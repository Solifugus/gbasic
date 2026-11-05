' dbframe.to_gsql: a frame into an EXISTING G-SQL table, every value bound,
' each row the table refuses reported and undone alone (gap G23 from the
' gBASIC books). Every answer is written here by hand. Run by
' tests/run_dbframe.sh with GBASIC_PATH=stdlib, from inside an empty directory.
load dbframe
load frame
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

function ids(db)
    out = []
    for each r in gsql.query(db, "select loan_id from loans order by loan_id")
        append(out, r.loan_id)
    end for
    return out
end function

db = gsql.connect("load.gsql", { create: true })
gsql.exec(db, "create table loans (loan_id text primary key, balance money(USD) not null, opened date not null, " +
              "rate number)")

df = frame.from_rows([
    { "Loan ID": "L1", "Balance": {USD}"1500.25", "Opened": {date}"2026-01-05", "Rate": 0.0525 },
    { "Loan ID": "L2", "Balance": {USD}"80.00", "Opened": unknown, "Rate": 0.04 },
    { "Loan ID": "O'Brien'); drop table loans;--", "Balance": {USD}"10.00", "Opened": {date}"2026-02-01",
      "Rate": unknown },
    { "Loan ID": "L1", "Balance": {USD}"9.99", "Opened": {date}"2026-03-01", "Rate": 0.01 },
    { "Loan ID": "L4", "Balance": {USD}"700.00", "Opened": {date}"2026-03-09", "Rate": 0.03 }
])

r = dbframe.to_gsql(df, db, "loans", {})
check("two rows refused, so not ok", r.ok, false)
check("three loaded", r.rows, 3)
check("  the ones the table took", ids(db), ["L1", "L4", "O'Brien'); drop table loans;--"])
got = []
for each x in r.refused
    append(got, string(x.row) + " " + x.code)
end for
check("each refused row, with the code it was refused with: a NULL date, a key twice", got,
      ["1 NOT_NULL", "3 DUPLICATE"])
check("  and what was offered", r.refused[1].values["Balance"], {USD}"9.99")
check("  the first L1's balance is the one kept, to the cent",
      gsql.query(db, "select balance from loans where loan_id = 'L1'")[0].balance, {USD}"1500.25")
check("the message says both halves", r.message, "2 row(s) refused; the other 3 were loaded")
check("the statement binds every value", r.sql,
      "insert into \"loans\" (\"loan_id\", \"balance\", \"opened\", \"rate\") values ($1, $2, $3, $4)")

' atomic: one refusal undoes the whole load
gsql.exec(db, "delete from loans")
r = dbframe.to_gsql(df, db, "loans", { atomic: true })
check("atomic: refused rows still reported", count(r.refused), 2)
check("  and nothing loaded", r.rows, 0)
check("  and the table is empty", ids(db), [])

' columns: names for a declared table whose columns are spelled differently
gsql.exec(db, "create table short (id text primary key, amt money(USD))")
r = dbframe.to_gsql(df, db, "short", { columns: { "Loan ID": "id", "Balance": "amt" } })
check("columns: only the named ones go in, under their names", r.rows, 4)
check("  the duplicate L1 still refused", r.refused[0].row, 3)
check("columns: a frame column that does not exist", dbframe.to_gsql(df, db, "short", { columns: { "Nope": "id" } }).message,
      "the frame has no column Nope")

' inside the caller's transaction it is a savepoint: the caller's rollback undoes it
gsql.exec(db, "delete from loans")
gsql.begin(db)
gsql.exec(db, "insert into loans values ('C0', 1.00, '2026-01-01', 0)")
r = dbframe.to_gsql(df, db, "loans", {})
check("inside a transaction: loaded", r.rows, 3)
check("  and the caller's row is still there", ids(db), ["C0", "L1", "L4", "O'Brien'); drop table loans;--"])
gsql.rollback(db)
check("  the caller's rollback takes it all", ids(db), [])

' refusals before anything runs
check("an unsafe table name", dbframe.to_gsql(df, db, "loans; drop", {}).ok, false)
two = frame.from_rows([{ "Rate (%)": 1, "rate": 2 }])
check("two columns becoming one identifier",
      dbframe.to_gsql(two, db, "loans", {}).message, "columns Rate (%) and rate both become \"rate\"")
r = dbframe.to_gsql(df, db, "no_such_table", {})
check("a table that is not there is refused once, not per row", r.message, "no table no_such_table: NOT_FOUND")
check("  with nothing refused row by row", count(r.refused), 0)

db.close()
print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)
