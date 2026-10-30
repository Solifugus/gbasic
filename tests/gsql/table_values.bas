' The ORACLE for G-SQL's column kinds (src/gsql/table.c, docs/gsql_table_design.md
' §3.1, tests/run_gsql_table.sh). gBASIC writes every value in `serialize` version
' 3, so the type rules are held to the bytes the language really produces rather
' than to a test's own writer -- and every verdict is written HERE, BY HAND, from
' the design's table, never read back from the layer.
'
' OUTPUT, one line per value:
'   V <verdicts> <label> <hex>
' where <verdicts> has one letter per column kind, in this order:
'   number integer string boolean money money(USD) date time datetime record
' y = accepted, T = refused as the wrong kind. Every column here is nullable,
' so an absence is y everywhere; NOT NULL is checked by the C side.

function v(verdicts, label, value)
    print "V " + verdicts + " " + label + " " + hex_encode(serialize(value))
end function

'                 n i s b m U d t D r
v("yyTTTTTTTT", "exact_3", 3)
v("yTTTTTTTTT", "inexact_3", 6 / 2)
v("yTTTTTTTTT", "fraction", 2.5)
v("yyTTTTTTTT", "two_53_plus_1", number("9007199254740993"))
v("yTTTTTTTTT", "nan", number("nan"))
v("yTTTTTTTTT", "inf", number("inf"))
v("yyTTTTTTTT", "negative", -42)
v("TTyTTTTTTT", "text", "text")
v("TTyTTTTTTT", "empty_text", "")
v("TTyTTTTTTT", "text_with_nul", "a" + chr(0) + "b")
v("TTyTTTTTTT", "digits_as_text", "3")
v("TTTyTTTTTT", "true", true)
v("TTTyTTTTTT", "false", false)
v("TTTTyyTTTT", "usd", {USD}1.25)
v("TTTTyTTTTT", "eur", {EUR}2.00)
v("TTTTyTTTTT", "jpy", {JPY}100)
v("TTTTTTyTTT", "date", {date}"2026-10-29")
v("TTTTTTTTTT", "year_month", {date}"2026-10")
v("TTTTTTTTTT", "year", {date}"2026")
v("TTTTTTTyTT", "time", {time}"13:45")
v("TTTTTTTyTT", "time_seconds", {time}"13:45:10")
v("TTTTTTTTyT", "datetime", {datetime}"2026-10-29 13:45")
v("TTTTTTTTTy", "record", { a: 1, b: "x" })
v("TTTTTTTTTy", "record_nested", { a: { b: [1, 2, { c: {USD}3 }] }, d: {date}"2026-10-29" })
v("TTTTTTTTTy", "record_empty", {})
v("TTTTTTTTTy", "record_absent_field", { a: unknown })
v("TTTTTTTTTT", "record_holding_duration", { d: 2 days })
v("TTTTTTTTTT", "record_holding_file", { f: {file}"/tmp/x" })
v("TTTTTTTTTT", "array", [1, 2])
v("TTTTTTTTTT", "duration", 2 days)
v("TTTTTTTTTT", "file", {file}"/tmp/x")
v("TTTTTTTTTT", "dir", {dir}"/tmp")
v("yyyyyyyyyy", "unknown", unknown)
v("yyyyyyyyyy", "nothing", nothing)
