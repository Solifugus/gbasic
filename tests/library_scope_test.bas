' A LIBRARY'S UNQUALIFIED CALL RESOLVES TO ITS OWN FUNCTION FIRST.
'
' Until 2026-08-31 it went through the same backward scan every caller uses --
' last registration wins -- so a library's internals were rewired by whatever
' was LOADED AFTER it, and the answer depended on a load order the library does
' not control.
'
' SELF-CHECKING, because the failure returns a perfectly good string from the
' wrong function. A golden would have recorded "beta" as the expected answer to
' `alpha.outer()`.

' Defined FIRST: a top-level function registers when the walk REACHES it, so
' one written at the foot of the file does not exist yet while the checks run.
function helper()
    return "root"
end function

load alpha from "libscope/alpha.bas"
load beta from "libscope/beta.bas"
load gamma from "libscope/gamma.bas"

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

' alpha is loaded BEFORE beta here, which is the order that used to fail: the
' later registration won, so alpha.outer called beta's helper.
check("a library calls its own function, not a later library's",
      alpha.outer(), "alpha")

' A ROOT function of the same name must not capture it either -- and this is
' the case that caught an incomplete fix. There are TWO dispatch sites; fixing
' only the general resolver left this one consulting the root program directly.
check("nor the root program's function of the same name",
      alpha.outer(), "alpha")

' THE CONTROL, and it is what stops the fix from sealing a library off: a
' library calling a name it does NOT define still reaches the global table.
check("a library still reaches outside for a name it does not define",
      gamma.reach(), "root")

' Qualified calls are unaffected.
check("qualified calls still name their library", alpha.helper(), "alpha")
check("both of them", beta.helper(), "beta")

' And the root program still resolves its own unqualified call to its own
' function -- the documented shadowing behaviour, unchanged.
check("the root program gets its own function", helper(), "root")

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
