' OPERATOR PRECEDENCE, and in particular where `not` sits.
' Self-checking; run by tests/run_precedence.sh.
'
' `not` USED TO SIT AT THE UNARY LEVEL beside `-`, which is C's `!` precedence,
' so `not a = b` meant `(not a) = b` and answered a perfectly plausible FALSE.
' Silent, and the opposite of what the author wrote. The reference documented it
' accurately, which did not help -- a reader who has to consult a precedence
' table to learn that the obvious reading is wrong has already been caught.
'
' Every BASIC puts NOT between comparison and AND: QBasic, VB, VB.NET,
' FreeBASIC. So do Python, SQL, Pascal and Ada. gBASIC does now (DOGFOOD 15).
'
' MEASURED BEFORE CHANGING IT: of 3,760 uses of `not` across stdlib, examples
' and tests, ZERO were `not X = Y` in code position -- so nothing depended on
' the old reading, which is the evidence this is a fix and not a migration.
'
' SELF-CHECKING RATHER THAN A GOLDEN, and forced: every defect in a precedence
' chain produces a PERFECTLY ORDINARY VALUE. A golden would have recorded
' `false` for `not a = b` as the expected answer and defended it.

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

a = 1
b = 2

' --- THE CASE THAT MOVED ----------------------------------------------------
check("not a = b means not (a = b)", not a = b, true)
check("and agrees with the explicit form", not (a = b), true)
check("while the other grouping still differs", (not a) = b, false)
check("not a = a", not a = a, false)
check("not with !=", not a != b, false)
check("not with <", not a < b, false)
check("not with >", not a > b, true)

' --- AND THE CONTROLS, which are most of this file. A precedence change is
'     only safe if everything else stayed where it was, and "not moved" is a
'     claim about every other level.
check("not on a plain value", not true, false)
check("not on a false value", not false, true)
check("not not", not not true, true)
check("not binds looser than arithmetic", not 1 + 1 = 2, false)
check("not a and b groups as (not a) and b", not false and true, true)
check("not a or b groups as (not a) or b", not true or true, true)
check("a and not b", true and not false, true)
check("unary minus is untouched", 0 - -1, 1)
check("not of a negative", not -1, false)

' multiplication before addition, comparison after both
check("2 * 3 + 4", 2 * 3 + 4, 10)
check("2 + 3 * 4", 2 + 3 * 4, 14)
check("arithmetic before comparison", 1 + 1 = 2, true)
check("comparison before and", 1 = 1 and 2 = 2, true)
check("and before or", false and false or true, true)
check("or is loosest", true or false and false, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
