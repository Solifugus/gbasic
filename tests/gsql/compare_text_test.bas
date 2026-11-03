' A comparison reads text as the other side's kind (docs/gsql_compare_conversion_
' design.md; gdash S3/S4, Matthew's ruling 2026-11-02). Run by
' tests/run_gsql_module.sh from inside an empty directory.
'
' THE SEEK MUST AGREE WITH THE SCAN: every condition is asked of `ix`, whose
' columns are indexed, and of `sc`, the same rows with no index, and the two
' answers must be the same -- refusals included. A seek that read the text one
' way and a scan another would differ here. Some answers are also written by
' hand, so the two cannot agree on something wrong.
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

db = gsql.connect("cmp.gsql", { create: true })
for each tbl in ["ix", "sc"]
    db.exec("create table " + tbl + " (k integer primary key, n integer, x number, d date, at datetime, amt money(USD), s string)")
    db.exec("insert into " + tbl + " values (1, 10, 2.5, '2026-09-30', '2026-09-30 23:00', 999.99, '10'), (2, 20, 1000, '2026-10-01', '2026-10-01 00:00', 1000, '20'), (3, 30, -1, '2026-10-02', '2026-10-02 08:30', 1250.7512, 'x')")
end for
for each c in ["n", "x", "d", "at", "amt"]
    db.exec("create index ix_" + c + " on ix (" + c + ")")
end for

' the keys of the rows matching, or "refused CODE"
function keys_where(tbl, cond, params)
    on error goto next
    r = db.query("select k from " + tbl + " where " + cond + " order by k", params)
    e = error
    if e then
        return "refused " + e.details.code
    end if
    out = []
    for each row in r
        append(out, row.k)
    end for
    return string(out)
end function

conds = ["k = '2'", "k = ' 2 '", "k = '2.0'", "k = '2.5'", "k = 'two'", "k > '1'", "k between '1' and '2'", "k in ('1', '3')",
         "n = '20'", "n >= '15'", "n < ' 25'", "x = '2.5'", "x > '1e2'", "x = '-1'",
         "d = '2026-10-01'", "d >= '2026-10-01'", "d between '2026-09-30' and '2026-10-01'", "d = '2026-13-01'", "d = 5",
         "at >= '2026-10-01 00:00'", "at < '2026-10-01'",
         "amt > 1000", "amt >= 1000", "amt = 1250.7512", "amt > '1000'", "amt = 'lots'",
         "k < 3 and s = 10", "s > 15", "'2' = k", "1000 < amt", "amt = cast('5' as money(EUR))"]
for each c in conds
    check("seek and scan agree: " + c, keys_where("ix", c, []), keys_where("sc", c, []))
end for

' by hand
check("an integer key against its text", keys_where("ix", "k = '2'", []), "[2]")
check("padded", keys_where("ix", "k = ' 2 '", []), "[2]")
check("a decimal is a number, and not 2", keys_where("ix", "k = '2.5'", []), "[]")
check("text that is no number is refused, never seeks to nothing", keys_where("ix", "k = 'two'", []), "refused EXPRESSION")
check("a date against its text", keys_where("ix", "d >= '2026-10-01'", []), "[2,3]")
check("a date that is no date is refused", keys_where("ix", "d = '2026-13-01'", []), "refused EXPRESSION")
check("a date against a number stays refused", keys_where("ix", "d = 5", []), "refused EXPRESSION")
check("money against a number is money in its currency", keys_where("ix", "amt > 1000", []), "[3]")
check("and the cents beyond count", keys_where("ix", "amt = 1250.7512", []), "[3]")
check("a string column against a number reads each string", keys_where("sc", "k < 3 and s = 10", []), "[1]")
check("and refuses once a row's string is no number", keys_where("sc", "s > 15", []), "refused EXPRESSION")
check("a parameter, as a form gives it", keys_where("ix", "k = $1", ["3"]), "[3]")
check("a parameter for a date", keys_where("ix", "d < $1", ["2026-10-01"]), "[1]")
check("money in another currency is not equal", keys_where("ix", "amt = cast('5' as money(EUR))", []), "[]")
check("and has no order", keys_where("ix", "amt > cast('5' as money(EUR))", []), "refused EXPRESSION")
check("a boolean against text stays refused", keys_where("ix", "(k = 1) = 'true'", []), "refused EXPRESSION")
check("arithmetic does not convert", keys_where("ix", "k + '1' = 2", []), "refused EXPRESSION")

' the refusal says what the text was compared with
on error goto next
r = db.query("select k from ix where d = 'soon'")
e = error
check("the refusal names the kind and the text", contains(e.message, "= compares a date with text that does not read as a date: 'soon'"), true)
on error stop
db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
