' PLAT-EQ, THE SCALAR HALF. Self-checking; run by tests/run_equality.sh.
'
' PLAT-EQ (2026-08-14) fixed compound comparison: `{x:1} = {y:2}` was TRUE
' because both sides fell through to the numeric coercion at the end of
' eval_comparison's chain, where a record becomes 0 -- and 0 = 0. It routed
' arrays and records away from that fallthrough.
'
' SCALARS WERE STILL IN IT. `value_number_or_zero` returns 0.0 for every kind
' that is not a number or a boolean, so every STRING was zero:
'
'     0 = "stop"  -> true        1 = "1"    -> false
'     0 = ""      -> true        1 > "stop" -> true, an ANSWER
'
' Not "numeric strings compare as numbers" -- `1 = "1"` was false. Every
' string was 0, so only the number 0 equalled one, and the ordering operators
' answered instead of refusing. Found 2026-09-05 measuring an actor pool: a
' worker whose stop sentinel was "stop" exited on the message 0, and the
' parent hung in receive() forever.
'
' MEASURED BEFORE FIXING, whole gate instrumented: 1,500 mismatched-kind
' comparisons reach the fallthrough. 1,472 are number-versus-boolean, which is
' a real coercion and is ASSERTED HERE AS STILL WORKING -- it is the control
' without which this fixture is satisfied by refusing every mixed comparison.
' The other 28 are number-versus-string, and one was already wrong: a
' corruption counter in run_xlsx.sh asks `c.value = "#VALUE!"` and would have
' counted a cell holding 0 as corrupt.
'
' SELF-CHECKING, not golden: every defect here is a plausible BOOLEAN, and a
' golden would have recorded `true` for `0 = "stop"` as expected and defended
' it -- which is exactly how it survived from the type's introduction.

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

' --- THE DEFECT: a number is not any string, and zero is not special ---------
check("0 = \"stop\" is false", 0 = "stop", false)
check("0 = \"\" is false -- the empty string is not zero either", 0 = "", false)
check("0 = \"abc\" is false", 0 = "abc", false)
check("and the reverse order", "stop" = 0, false)
check("0 != \"stop\" is true", 0 != "stop", true)
check("a nonzero number was already false, and stays false", 1 = "abc", false)

' NO NUMERIC-STRING COERCION IS INTRODUCED. `1 = "1"` was false before and is
' false now; making it true would be a NEW rule, not a fix. The pair below is
' what says so -- without it, "0 = \"0\" is false" reads as an oversight.
check("1 = \"1\" is false -- no coercion, as before", 1 = "1", false)
check("0 = \"0\" is false -- consistently, not specially", 0 = "0", false)

' --- ORDERING REFUSES, which is the documented idiom ------------------------
' reference.md: "Equality answers while ordering refuses." Money says it, the
' compounds say it, and this is the same sentence for a string and a number.
on error goto next
r = 1 > "stop"
raised = false
message = ""
if error then
    raised = true
    message = error.message
    error.clear()
end if
check("1 > \"stop\" RAISES rather than answering", raised, true)
check("  and names both kinds", contains(message, "string") and contains(message, "number"), true)

r = 0 < "stop"
raised = false
if error then
    raised = true
    error.clear()
end if
check("0 < \"stop\" raises too", raised, true)

r = "a" >= 5
raised = false
if error then
    raised = true
    error.clear()
end if
check("and in the reverse order", raised, true)
on error stop

' --- CONTROL: string-to-string ordering is UNTOUCHED -------------------------
' Without this, "ordering refuses" would be satisfied by refusing all of it.
check("\"a\" < \"b\" still orders", "a" < "b", true)
check("\"b\" > \"a\" still orders", "b" > "a", true)
check("\"a\" = \"a\" still equal", "a" = "a", true)

' --- CONTROL: number-versus-boolean, the 1,472 uses -------------------------
' The measurement said this is what the tree actually does at this fallthrough.
' It is a real coercion and it keeps working.
check("0 = false is still true", 0 = false, true)
check("1 = true is still true", 1 = true, true)
check("0 = true is still false", 0 = true, false)
check("1 > false still orders", 1 > false, true)

' --- CONTROL: nothing and unknown are unchanged (their own branch above) -----
check("0 = nothing is false", 0 = nothing, false)
check("0 = unknown is false", 0 = unknown, false)
check("\"\" = nothing is false", "" = nothing, false)

' --- THE SHAPE THAT FOUND IT: a sentinel in a message loop ------------------
' A worker comparing each message against a string sentinel must not stop on
' the number 0. This is the actor-pool loop, without the actor.
function is_stop(m)
    return m = "stop"
end function
check("a numeric message is not the stop sentinel", is_stop(0), false)
check("  nor is any other number", is_stop(7), false)
check("  and the sentinel still is", is_stop("stop"), true)

' --- THE SHAPE ALREADY WRONG IN THE TREE ------------------------------------
' run_xlsx.sh counts corrupted cells with `c.value = "#VALUE!"`. A cell holding
' the number 0 was counted as corrupt; the suite passed only because no cell in
' that fixture is zero.
cells = [0, 12, "#VALUE!", 0]
corrupt = 0
for each v in cells
    if v = "#VALUE!" then
        corrupt = corrupt + 1
    end if
end for
check("a corruption counter counts 1, not 3", corrupt, 1)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
