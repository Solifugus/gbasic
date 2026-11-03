' A NUMBER AGAINST TEXT THAT IS A NUMBER (docs/number_text_equality_design.md).
' Self-checking; run by tests/run_equality.sh.
'
' Asked for by gdash (S4) and ruled by Matthew 2026-11-02: `=` reads text that
' is a number the way G-SQL's comparison does, text that is no number stays
' unequal, and the "same value" routes keep the kinds apart -- so
' `unique([1, "1"])` is still two values.
'
' THE CONTROLS ARE THE POINT. "1 = \"1\" is true" alone is satisfied by a
' reader that takes anything (`0 = "stop"` true again, the defect of
' 2026-09-05), and "unique keeps both" alone is satisfied by an operator that
' never converted. Each half has its other half here.

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

function raises_order(a, b)
    on error goto next
    r = a < b
    if error then
        error.clear()
        return true
    end if
    return false
end function

print "-- TEXT THAT IS A NUMBER EQUALS THAT NUMBER"
check("1 = \"1\"", 1 = "1", true)
check("\"1\" = 1, either side", "1" = 1, true)
check("0 = \"0\"", 0 = "0", true)
check("1.5 = \"1.5\"", 1.5 = "1.5", true)
check("trimmed: 1 = \" 1 \"", 1 = " 1 ", true)
check("tab and line end trimmed: 25 = \"\\t25\\r\\n\"", 25 = "\t25\r\n", true)
check("a sign: -3 = \"-3\"", -3 = "-3", true)
check("a plus sign: 5 = \"+5\"", 5 = "+5", true)
check("an exponent: 1000 = \"1e3\"", 1000 = "1e3", true)
check("2.50 is 2.5", 2.5 = "2.50", true)
check("!= follows: 1 != \"1\" is false", 1 != "1", false)
check("a different number is unequal: 1 = \"2\"", 1 = "2", false)
check("1 != \"2\"", 1 != "2", true)

print ""
print "-- AND ORDERS AS A NUMBER, NOT AS TEXT"
check("2 < \"10\" (as text \"2\" > \"10\")", 2 < "10", true)
check("\"10\" > 2", "10" > 2, true)
check("3 >= \" 3\"", 3 >= " 3", true)
check("!< too: 2 !< \"10\" is false", 2 !< "10", false)

print ""
print "-- EXACT: text past 2^53 is read exactly, as number(\"...\") reads it"
check("9007199254740993 = \"9007199254740993\"", 9007199254740993 = "9007199254740993", true)
check("  and is not 9007199254740992", 9007199254740992 = "9007199254740993", false)

print ""
print "-- TEXT THAT IS NO NUMBER: = is false, ordering refuses, as before"
check("0 = \"stop\" is false", 0 = "stop", false)
check("0 = \"\" is false", 0 = "", false)
check("0 = \"   \" is false (blank, not zero)", 0 = "   ", false)
check("0 != \"stop\" is true", 0 != "stop", true)
check("hex is not read: 16 = \"0x10\"", 16 = "0x10", false)
check("inf is not read: 0 = \"inf\"", 0 = "inf", false)
check("separators are not read: 1000 = \"1,000\"", 1000 = "1,000", false)
check("a space inside is not read: 12 = \"1 2\"", 12 = "1 2", false)
check("a sign alone: 0 = \"-\"", 0 = "-", false)
check("a NUL is not read: 1 = \"1\" + chr(0)", 1 = "1" + chr(0), false)
check("1 < \"stop\" refuses", raises_order(1, "stop"), true)
check("1 < \"0x10\" refuses", raises_order(1, "0x10"), true)
check("control: 1 < \"2\" answers", raises_order(1, "2"), false)

print ""
print "-- NOTHING ELSE CONVERTS"
check("true = \"true\" is false", true = "true", false)
check("0 = false still coerces", 0 = false, true)
check("\"1\" = \"1.0\": two texts compare as text", "1" = "1.0", false)
check("an array compares stored values: [1] = [\"1\"]", [1] = ["1"], false)
check("a record too: {a: 1} = {a: \"1\"}", { a: 1 } = { a: "1" }, false)

print ""
print "-- THE ROUTES KEEP THE KINDS APART (Matthew: do not collapse)"
check("unique([1, \"1\"]) is two values", count(unique([1, "1"])), 2)
check("contains([\"1\"], 1) is false", contains(["1"], 1), false)
check("contains([1], \"1\") is false", contains([1], "1"), false)
check("find([\"1\"], 1) finds nothing", find(["1"], 1), nothing)
check("control: find([\"1\"], \"1\") finds it at 0", find(["1"], "1"), 0)
check("remove_value([1, \"1\"], 1) keeps the text", remove_value([1, "1"], 1), ["1"])
check("control: contains([1], 1.0)", contains([1], 1.0), true)

hit = "none"
consider "1"
if 1 then
    hit = "number"
if "1" then
    hit = "text"
end consider
check("consider \"1\" passes the number branch", hit, "text")

check("compare() follows the operator", compare(1, "=", "1"), true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
