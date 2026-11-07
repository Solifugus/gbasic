' A LIBRARY'S OWN LOADS ARE ITS OWN (docs/library_private_loads_design.md):
' alpha, beta and this program each load a different library declared
' `util`, and each `util.who()` reaches its own. Run by run_alias.sh's
' private tier. Before 2026-11-07 the second load was refused.
load util from "own_util.bas"
load alpha from "alpha.bas"
load beta from "beta.bas"

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

check("the program's util is its own", util.who(), "the program's util")
check("alpha's util is alpha's", alpha.ask(), "alpha's util")
check("beta's util is beta's", beta.ask(), "beta's util")
f = alpha.handed()
check("a function value from alpha's util calls alpha's, wherever it is called", f(), "alpha's util")
g = util.who
check("and the program's own, as a value", g(), "the program's util")
on error goto next
x = alpha.boom()
check("an error from alpha's util names the library as written", contains(error.message, "raised in alpha's util"), true)
libs = []
for each fr in error.trace
    append(libs, fr.library)
end for
check("its trace names alpha's util as written, with its file", contains(libs, "util (tests/private_loads/alpha_util.bas)"), true)
error.clear()
on error stop
c = library_collisions()
names = []
for each r in c
    if r.name = "who" then
        names = r.libraries
    end if
end for
check("library_collisions reports all three", count(names), 3)
shown = true
for each n in names
    if contains(n, "#") then
        shown = false
    end if
end for
check("and shows no hidden identity", shown, true)
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
