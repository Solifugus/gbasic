' The ORACLE for G-SQL's column kinds (src/gsql/table.c, docs/gsql_table_design.md
' §3.1, tests/run_gsql_table.sh). gBASIC writes every value in `serialize` version
' 3, so the type rules are held to the bytes the language really produces rather
' than to a test's own writer -- and every verdict is written HERE, BY HAND, from
' the design's table, never read back from the layer.
'
' OUTPUT, one line per value:
'   V <verdicts> <label> <hex>
' where <verdicts> has one letter per column kind, in this order:
'   number integer string boolean money money(USD) date time datetime record duration
' y = accepted, T = refused as the wrong kind. Every column here is nullable,
' so an absence is y everywhere; NOT NULL is checked by the C side.

function v(verdicts, label, value)
    print "V " + verdicts + " " + label + " " + hex_encode(serialize(value))
end function

'                 n i s b m U d t D r u
v("yyTTTTTTTTT", "exact_3", 3)
v("yTTTTTTTTTT", "inexact_3", 6 / 2)
v("yTTTTTTTTTT", "fraction", 2.5)
v("yyTTTTTTTTT", "two_53_plus_1", number("9007199254740993"))
v("yTTTTTTTTTT", "nan", number("nan"))
v("yTTTTTTTTTT", "inf", number("inf"))
v("yyTTTTTTTTT", "negative", -42)
v("TTyTTTTTTTT", "text", "text")
v("TTyTTTTTTTT", "empty_text", "")
v("TTyTTTTTTTT", "text_with_nul", "a" + chr(0) + "b")
v("TTyTTTTTTTT", "digits_as_text", "3")
v("TTTyTTTTTTT", "true", true)
v("TTTyTTTTTTT", "false", false)
v("TTTTyyTTTTT", "usd", {USD}1.25)
v("TTTTyTTTTTT", "eur", {EUR}2.00)
v("TTTTyTTTTTT", "jpy", {JPY}100)
v("TTTTTTyTTTT", "date", {date}"2026-10-29")
v("TTTTTTTTTTT", "year_month", {date}"2026-10")
v("TTTTTTTTTTT", "year", {date}"2026")
v("TTTTTTTyTTT", "time", {time}"13:45")
v("TTTTTTTyTTT", "time_seconds", {time}"13:45:10")
v("TTTTTTTTyTT", "datetime", {datetime}"2026-10-29 13:45")
v("TTTTTTTTTyT", "record", { a: 1, b: "x" })
v("TTTTTTTTTyT", "record_nested", { a: { b: [1, 2, { c: {USD}3 }] }, d: {date}"2026-10-29" })
v("TTTTTTTTTyT", "record_empty", {})
v("TTTTTTTTTyT", "record_absent_field", { a: unknown })
v("TTTTTTTTTyT", "record_holding_duration", { d: 2 days })
v("TTTTTTTTTTT", "record_holding_file", { f: {file}"/tmp/x" })
v("TTTTTTTTTTT", "array", [1, 2])
v("TTTTTTTTTTy", "duration", 2 days)
v("TTTTTTTTTTT", "file", {file}"/tmp/x")
v("TTTTTTTTTTT", "dir", {dir}"/tmp")
v("yyyyyyyyyyy", "unknown", unknown)
v("yyyyyyyyyyy", "nothing", nothing)
