' What may share a name with what. Every case here is LEGAL and must stay so:
' the duplicate-definition refusal is about ONE scope, and if it reached any of
' these it would be a ban on ordinary programs rather than a refusal of a
' mistake.
load scope_alpha from "libs/scope_alpha.bas"
load scope_beta from "libs/scope_beta.bas"

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

' TWO LIBRARIES may define one name. Different scopes; each is reachable by
' qualifying, which is what qualifying is for.
check("two libraries may share a function name", scope_alpha.shared(), "alpha")
check("  and each is reachable qualified", scope_beta.shared(), "beta")

' A LOCAL may share a name with a library function. Different scopes again, and
' the local wins unqualified -- documented, warned about, and unchanged.
function shared()
    return "local"
end function
check("a local may share a name with a library function", shared(), "local")
check("  and the library one is still reachable", scope_alpha.shared(), "alpha")

' A LOCAL may share a name with a BUILT-IN. Same rule one level over.
function len(x)
    return "shadowed"
end function
check("a local may share a name with a builtin", len("abc"), "shadowed")

' And two DIFFERENT names in one scope are, of course, fine -- the control that
' says the refusal is not simply "a second function".
function one()
    return 1
end function
function two()
    return 2
end function
check("two differently-named functions in one scope", one() + two(), 3)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
