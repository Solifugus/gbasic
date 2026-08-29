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

hi {USD}= "92233720368547758.07"
lo {USD}= "-92233720368547758.08"
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
check("max - 0.01 succeeds", ok1, "92233720368547758.06")
error.clear()

ok2 = lo + one
check("min + 0.01 succeeds", ok2, "-92233720368547758.07")
error.clear()

ok3 = hi * 1
check("max * 1 succeeds", ok3, "92233720368547758.07")
error.clear()

ok4 = hi / 2
check("max / 2 succeeds", ok4, "46116860184273879.04")
error.clear()

ok5 = hi * 0.5
check("max * 0.5 succeeds", ok5, "46116860184273879.04")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
