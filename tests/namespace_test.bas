' `library_collisions()` and `password_hash_cost()` -- the two audit builtins
' added for the gdash session's GDASH-4 step 0.
'
' SELF-CHECKING. A collision report that silently returned an empty array would
' be indistinguishable from a clean namespace, which is exactly the false
' reassurance this builtin exists to remove -- so the fixture asserts the
' POSITIVE case (a collision it planted) as well as the negative.

load alpha from "namespace/alpha.bas"
load beta from "namespace/beta.bas"

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

' ------------------------------------------------ the collision is reported
found = library_collisions()
check("exactly one collision between these two libraries", count(found), 1)
check("and it names the shared function", found[0].name, "ensure_dir")
check("naming both libraries", join(sort(found[0].libraries), ","), "alpha,beta")

' THE CONTROL, and it is the point: names defined by only ONE library must not
' appear. Without this a builtin that returned every imported name would pass
' every check above.
names = []
for each c in found
    append(names, c.name)
next
check("a name unique to alpha is not reported", contains(names, "only_alpha"), false)
check("nor one unique to beta", contains(names, "only_beta"), false)

' NOT CALL-TRIGGERED: nothing above calls `ensure_dir` unqualified, which is
' precisely the case the override WARNING stays silent for. That difference is
' the whole reason this builtin exists.
check("reported without any unqualified call", count(found), 1)

' Calling it qualified still works and changes nothing.
check("alpha's version still reachable", alpha.ensure_dir("x"), "alpha:x")
check("beta's version still reachable", beta.ensure_dir("x"), "beta:x")
check("and the report is unchanged", count(library_collisions()), 1)

' ------------------------------------------------------ password_hash_cost
cost = password_hash_cost()
check("a cost is reported as a number", type(cost.ms), "number")
check("and it is positive", cost.ms > 0, true)
check("the prefix names the algorithm and parameters", starts_with(cost.prefix, "$y$"), true)
' The prefix must be the PARAMETER field, not the salt -- a salt differs every
' call and would make two deployments incomparable, which is the whole use.
check("the prefix is stable across calls", password_hash_cost().prefix, cost.prefix)
check("and a real hash carries it", starts_with(password_hash("secret"), cost.prefix), true)

' ------------------------------------------------------------- refusals
on error goto next
x = library_collisions("extra")
check("library_collisions takes no arguments", error.message,
      "library_collisions expects no arguments")
error.clear()
y = password_hash_cost(1)
check("password_hash_cost takes no arguments", error.message,
      "password_hash_cost expects no arguments")
error.clear()
on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
