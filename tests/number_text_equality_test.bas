' A NUMBER AGAINST TEXT THAT IS A NUMBER (docs/number_text_equality_design.md).
' Self-checking; run by tests/run_equality.sh.
'
' `1 = "1"` reading the text was built 2026-11-02 (gdash S4) and reverted
' 2026-11-03 on Matthew's second ruling: gBASIC's values carry no declared
' kind, so the conversion guessed where SQL's follows a column, and it left
' `=` disagreeing with `contains` and with `[1] = ["1"]`. The comparison stays
' unequal and is SAID: warning 2122, naming the conversion.
'
' THE CONTROLS ARE THE POINT. "1 = \"1\" warns" alone is satisfied by a warning
' on every number-versus-text comparison, which `if answer = 0` after input()
' would turn into noise an author learns to ignore. Text that is no number
' must stay silent, and so must every route.
'
' Every comparison below is on its own line rather than inside a helper, so
' each is its own site. Once per site under `on warning print` is the runner's
' tier, since `goto next` hands every one over.

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

' the code of a claimed warning, or 0 for none
function code_of(w)
    if type(w) = "boolean" then
        return 0
    end if
    return w.code
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

on warning goto next

print "-- TEXT THAT IS A NUMBER: unequal, and warned"
r = 1 = "1"
w = warning
check("1 = \"1\" is false", r, false)
check("  and warns 2122", code_of(w), 2122)
check("  naming both sides", contains(w.message, "number 1") and contains(w.message, "\"1\""), true)
check("  and the remedy", contains(w.message, "number()"), true)
r = "7" = 7
check("\"7\" = 7, either side, warns", code_of(warning), 2122)
r = 1 != "1"
check("1 != \"1\" is true", r, true)
check("  and warns", code_of(warning), 2122)
r = 25 = " 25 "
check("padded text reads, so it warns", code_of(warning), 2122)
r = 1.5 = "1.5"
check("a fraction warns", code_of(warning), 2122)
r = 1000 = "1e3"
check("an exponent warns", code_of(warning), 2122)
r = 2 = "3"
check("a DIFFERENT number in text warns too (the answer is no less a guess)", code_of(warning), 2122)
r = compare(1, "=", "1")
check("compare() is the operator, and warns", code_of(warning), 2122)

print ""
print "-- TEXT THAT IS NO NUMBER: unequal and SILENT, as before"
r = 0 = "stop"
check("0 = \"stop\" is false", r, false)
check("  and says nothing", code_of(warning), 0)
r = 0 = ""
check("0 = \"\" says nothing", code_of(warning), 0)
r = 0 = "   "
check("blank text says nothing", code_of(warning), 0)
r = 16 = "0x10"
check("hex is not a number here: says nothing", code_of(warning), 0)
r = 0 = "inf"
check("inf says nothing", code_of(warning), 0)
r = 1000 = "1,000"
check("1,000 says nothing", code_of(warning), 0)
r = 12 = "1 2"
check("a space inside says nothing", code_of(warning), 0)
r = 1 = "1" + chr(0)
check("text holding a NUL says nothing", code_of(warning), 0)

print ""
print "-- ORDERING STILL REFUSES, with its own message, not the warning"
check("2 < \"10\" refuses", raises_order(2, "10"), true)
check("1 < \"stop\" refuses", raises_order(1, "stop"), true)

print ""
print "-- NOTHING ELSE WARNS"
r = true = "true"
check("a boolean against text says nothing", code_of(warning), 0)
r = 0 = false
check("0 = false still coerces", r, true)
check("  silently", code_of(warning), 0)
r = "1" = "1.0"
check("two texts compare as text", r, false)
check("  silently", code_of(warning), 0)
r = 1 = 1
check("control: two numbers", r, true)
check("  silently", code_of(warning), 0)
r = [1] = ["1"]
check("[1] = [\"1\"] is false", r, false)
check("  silently (a compound compares stored values)", code_of(warning), 0)

print ""
print "-- THE ROUTES: a membership question, never warned"
r = count(unique([1, "1"]))
check("unique([1, \"1\"]) is two values", r, 2)
check("  silently", code_of(warning), 0)
r = contains([1], "1")
check("contains([1], \"1\") is false", r, false)
check("  silently", code_of(warning), 0)
r = find(["1"], 1)
check("find([\"1\"], 1) finds nothing", r, nothing)
check("  silently", code_of(warning), 0)
r = remove_value([1, "1"], 1)
check("remove_value([1, \"1\"], 1) keeps the text", r, ["1"])
check("  silently", code_of(warning), 0)

hit = "none"
consider "1"
if 1 then
    hit = "number"
if "1" then
    hit = "text"
end consider
check("consider \"1\" passes the number branch", hit, "text")
check("  silently", code_of(warning), 0)

on warning print

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
