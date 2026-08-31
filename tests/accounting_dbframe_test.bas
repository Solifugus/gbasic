' THE LEDGER THROUGH A DATABASE AND BACK -- docs/accounting_design.md §6.
'
' The claim being tested is that accounting composes with the data pipeline
' rather than sitting beside it. The only way to make that a fact is to send a
' ledger out through `dbframe` into SQLite, read it back with `sqlite`, rebuild
' the statements from what came back, and require them to be IDENTICAL.
'
' WHY IT COULD FAIL QUIETLY: money is exact and a database column is not. A
' round trip through REAL loses cents; a total that is off by one cent still
' looks like a total. So the comparison is against the statements computed
' before the trip, figure for figure.

load accounting
load frame
load dbframe

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

books = accounting.chart([
  { code: "1000", name: "Cash",        kind: "asset" },
  { code: "2000", name: "Payables",    kind: "liability" },
  { code: "3000", name: "Capital",     kind: "equity" },
  { code: "4000", name: "Fees",        kind: "revenue" },
  { code: "5100", name: "Rent",        kind: "expense" }
])

function usd(t)
    m {USD}= t
    return m
end function
function day(d)
    w {date}= "2026-02-" + d
    return w
end function

lg = accounting.ledger()
lg = accounting.post(lg, accounting.entry(books, day("01"), "Capital",
      [{ account: "1000", debit: usd("5000.00") }, { account: "3000", credit: usd("5000.00") }]))
lg = accounting.post(lg, accounting.entry(books, day("07"), "Fee",
      [{ account: "1000", debit: usd("1234.56") }, { account: "4000", credit: usd("1234.56") }]))
lg = accounting.post(lg, accounting.entry(books, day("14"), "Rent on credit",
      [{ account: "5100", debit: usd("789.01") }, { account: "2000", credit: usd("789.01") }]))

before = accounting.balance_sheet(books, lg, nothing)
inc_before = accounting.income_statement(books, lg, day("01"), day("28"))

' ------------------------------------------ out: one row per ledger LINE
' The amount goes out as exact decimal TEXT via money.text, not as a number.
' A REAL column would round 1234.56 to something that still looks like money.
rows = []
n = 0
for each e in lg.entries
    for each ln in e.lines
        append(rows, { entry: n, on_date: string(e.date), memo: e.memo,
                       account: ln.account, amount: money.text(ln.amount),
                       currency: ln.currency })
    next
    n = n + 1
next
check("one row per line", count(rows), 6)

df = frame.from_rows(rows)
load sqlite
db = sqlite.connect(":memory:")
out = dbframe.to_table(df, db, "ledger_lines", { replace: true })
check("the ledger loaded into SQLite", out.ok, true)
check("all six rows landed", out.rows, 6)

' ------------------------------------------------------------ back in
read_back = sqlite.query(db, "select entry, on_date, memo, account, amount, currency from ledger_lines order by entry, account", [])
check("read back the same number of rows", count(read_back), 6)

' Rebuild the ledger from what the database returned, so the statements below
' are computed from DATABASE bytes and not from the value still in memory.
rebuilt = accounting.ledger()
grouped = { }
for each r in read_back
    key = string(r.entry)
    existing = grouped[key]
    if is_unknown(existing) then
        existing = []
    end if
    append(existing, r)
    grouped[key] = existing
next
for each key in sort(keys(grouped))
    lines = []
    memo = ""
    on_when = nothing
    for each r in grouped[key]
        amt {USD}= r.amount
        memo = r.memo
        w {date}= r.on_date
        on_when = w
        if amt > usd("0.00") then
            append(lines, { account: r.account, debit: amt })
        else
            append(lines, { account: r.account, credit: amt * -1 })
        end if
    next
    rebuilt = accounting.post(rebuilt, accounting.entry(books, on_when, memo, lines))
next
check("every entry rebuilt", count(rebuilt.entries), count(lg.entries))

' --------------------------------------------- the statements must match
after = accounting.balance_sheet(books, rebuilt, nothing)
inc_after = accounting.income_statement(books, rebuilt, day("01"), day("28"))
check("assets survived the round trip", after.assets, before.assets)
check("liabilities survived", after.liabilities, before.liabilities)
check("equity survived", after.equity, before.equity)
check("earnings survived", after.earnings, before.earnings)
check("and it still balances", after.balanced, true)
check("revenue survived", inc_after.revenue, inc_before.revenue)
check("expenses survived", inc_after.expenses, inc_before.expenses)
check("net income survived", inc_after.net, inc_before.net)

' THE CENTS ARE THE POINT. Asserting the exact figure, not just equality with
' itself -- two identically-damaged values would compare equal.
check("the awkward cents are intact", after.assets, "6234.56")
check("and the expense's", inc_after.expenses, "789.01")

sqlite.close(db)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
