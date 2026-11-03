' Text stored into an integer or number column (Matthew's rulings 2026-11-02; gbasic-books
' volume 3, Q1; docs/gsql_store_conversion_design.md §7). Text that is a whole
' number written in digits is stored as that integer, exact; anything else in
' text is refused with its reason. Every answer below is written by hand, and
' each refused shape sits beside an accepted neighbour. Run by
' tests/run_gsql_module.sh from inside an empty directory.
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

db = gsql.connect("ints.gsql", { create: true })
db.exec("create table t (k integer primary key, n integer)")
next_k = { v: 0 }

' "stored N" or "refused CODE <reason>", the reason being the message's words
' after "the text given "
function store(v)
    next_k.v = next_k.v + 1
    on error goto next
    db.exec("insert into t (k, n) values ($1, $2)", [next_k.v, v])
    e = error
    if e then
        m = e.message
        at = find(m, "the text given ")
        if is_unknown(at) or at = nothing then
            return "refused " + e.details.code
        end if
        rest = mid(m, at + 15, len(m) - at - 15)
        cut = find(rest, ": '")
        if not (is_unknown(cut) or cut = nothing) then
            rest = mid(rest, 0, cut)
        end if
        return "refused " + e.details.code + " " + rest
    end if
    r = db.query("select n from t where k = $1", [next_k.v])
    return "stored " + string(r[0].n)
end function

check("digits", store("25"), "stored 25")
check("a minus sign", store("-7"), "stored -7")
check("a plus sign", store("+25"), "stored 25")
check("leading zeros are the number: keeping them is a string column's job", store("007"), "stored 7")
check("minus zero", store("-0"), "stored 0")
check("the largest 64-bit integer", store("9223372036854775807"), "stored 9223372036854775807")
check("the smallest", store("-9223372036854775808"), "stored -9223372036854775808")
check("one past it is refused", store("9223372036854775808"), "refused TYPE is past the 64-bit integer range")
check("a decimal", store("2.5"), "refused TYPE is not a whole number")
check("a decimal that is whole", store("2.0"), "refused TYPE is not a whole number")
check("a thousands comma", store("1,000"), "refused TYPE is not a whole number")
check("an exponent", store("1e3"), "refused TYPE is not a whole number written in digits")
check("hex", store("0x1A"), "refused TYPE is not a number written in digits")
' surrounding spaces are trimmed (Matthew, 2026-11-02: "' 25' should be seen
' as 25"); a space INSIDE the number is not
check("a leading space is trimmed", store(" 25"), "stored 25")
check("a trailing space is trimmed", store("25 "), "stored 25")
check("a tab and a line break are trimmed", store(chr(9) + "26" + chr(13) + chr(10)), "stored 26")
check("a padded decimal is still a decimal", store(" 2.5"), "refused TYPE is not a whole number")
check("a space inside the number is refused", store("2 5"), "refused TYPE has a space inside the number")
check("spaces alone are empty", store("   "), "refused TYPE is empty")
check("empty text", store(""), "refused TYPE is empty")
check("a sign alone", store("-"), "refused TYPE is a sign with no digits")
check("a word", store("abc"), "refused TYPE is not a number written in digits")
check("inf", store("inf"), "refused TYPE is not a number written in digits")
check("an underscore", store("1_000"), "refused TYPE is not a number written in digits")
check("digits from another script", store("١٢"), "refused TYPE is not a number written in digits")
check("a NUL", store("1" + chr(0) + "2"), "refused TYPE holds a NUL")
check("a number still converts as before", store(12), "stored 12")
check("and an inexact one is still refused", store(6 / 2), "refused TYPE")
check("unknown is stored as unknown", store(unknown), "stored unknown")

' the refusal names its column (docs/gsql_error_details_design.md)
on error goto next
db.exec("insert into t (k, n) values (1000, '2.5')")
e = error
check("a refusal names the table", e.details.table, "t")
check("and the column", e.details.column, "n")
' a refusal by the conversion says which row, as a refusal at the insert does
db.exec("insert into t (k, n) values (1001, '1'), (1002, 'x')")
e = error
check("a later row of VALUES is named", contains(e.message, "row 2 of VALUES: t.n is integer"), true)
on error stop

' a NUMBER column takes text that is a number written plainly, decimals and
' exponents included, trimmed; the same refusals otherwise
db.exec("create table u (k integer primary key, x number)")
function store_x(k, v)
    on error goto next
    db.exec("insert into u (k, x) values ($1, $2)", [k, v])
    e = error
    if e then
        return "refused " + e.details.code
    end if
    return "stored " + string(db.query("select x from u where k = $1", [k])[0].x)
end function
check("number: a decimal", store_x(1, "2.5"), "stored 2.5")
check("number: a padded decimal", store_x(2, " -0.25 "), "stored -0.25")
check("number: an exponent", store_x(3, "1.5e3"), "stored 1500")
check("number: a leading point", store_x(4, ".5"), "stored 0.5")
check("number: whole text is exact", store_x(5, "9007199254740993") , "stored 9007199254740993")
check("number: a decimal comma is refused, being ambiguous", store_x(6, "1,5"), "refused TYPE")
check("number: hex is refused", store_x(7, "0x10"), "refused TYPE")
check("number: inf is refused", store_x(8, "inf"), "refused TYPE")
check("number: a space inside is refused", store_x(9, "1 000"), "refused TYPE")
check("number: empty is refused", store_x(10, ""), "refused TYPE")

' UPDATE, INSERT ... SELECT and an sql block, all of which store
db.exec("update t set n = '41' where k = 1")
check("UPDATE ... SET converts", db.query("select n from t where k = 1")[0].n, 41)
db.exec("create table src (s string)")
db.exec("insert into src (s) values ('88')")
db.exec("insert into t (k, n) select 2000, s from src")
check("INSERT ... SELECT converts what it copies", db.query("select n from t where k = 2000")[0].n, 88)
db.close()

database ints( file: "ints.gsql" )
sql put on ints
    insert into t (k, n) values (:k, :n)
end sql
put.exec({ k: 3000, n: "64" })
d = gsql.connect("ints.gsql")
check("an sql block given text from a record converts", d.query("select n from t where k = 3000")[0].n, 64)
d.close()
ints.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
