' PLAT-MONEY phase 1: overflow RAISES rather than wrapping.
'
' Signed overflow in C is undefined behaviour, not a defined wrap, and before
' this change `int64max + 0.01` returned the most NEGATIVE money value -- a
' sign flip, which is strictly worse than the rounding bug it would have
' accompanied. Nothing had reached it because until phase 0 no value that
' large could be constructed, and phase 2 makes the boundary 10,000x easier to
' reach: guard digits drop the safe range from ~$90tn to ~$9bn.

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

' USD's ceiling moved in phase 2: guard digits store four extra places, so the
' int64 limit fell 10,000x from about $92 quadrillion to $9.22 TRILLION. These
' are written to cent precision because authored text is capped at the minor
' unit -- the true limits are .775807 and .775808, a fraction of a cent beyond.
hi {USD}= "9223372036854.77"
lo {USD}= "-9223372036854.77"
one {USD}= "0.01"

on error goto next

x = hi + one
check("max + 0.01 raises", error.message, "money value is out of range")
error.clear()

x = lo - one
check("min - 0.01 raises", error.message, "money value is out of range")
error.clear()

x = hi * 2
check("max * 2 raises", error.message, "money value is out of range")
error.clear()

x = hi * 1.5
check("max * 1.5 raises", error.message, "money value is out of range")
error.clear()

x = hi / 0.5
check("max / 0.5 raises", error.message, "money value is out of range")
error.clear()

x = hi / 0
check("division by zero raises", error.message, "division by zero")
error.clear()

x = lo * 2
check("min * 2 raises", error.message, "money value is out of range")
error.clear()

' ---------------------------------------------- THE CONTROL, and it is vital
' Every check above would also pass on an implementation that raised on
' everything. Each of these is the nearest operation that must SUCCEED.
ok1 = hi - one
check("max - 0.01 succeeds", ok1, "9223372036854.76")
error.clear()

ok2 = lo + one
check("min + 0.01 succeeds", ok2, "-9223372036854.76")
error.clear()

ok3 = hi * 1
check("max * 1 succeeds", ok3, "9223372036854.77")
error.clear()

ok4 = hi / 2
check("max / 2 succeeds (…385 ties to even)", ok4, "4611686018427.38")
error.clear()

ok5 = hi * 0.5
check("max * 0.5 succeeds (same tie)", ok5, "4611686018427.38")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
