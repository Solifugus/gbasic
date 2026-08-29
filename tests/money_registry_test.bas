' PLAT-MONEY phase 2: registering currencies, and why removal is not offered.

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

' A currency the ISO list will never carry: loyalty points, whole units only.
code = money.register("PTS", 0)
check("a registered code sits above ISO's range", code >= 1000, true)
p {PTS}= "1500"
check("and is usable as a modifier", p, "1500")
check("with its own exponent", p * 3, "4500")

' Four decimals, for an internal unit cost.
money.register("SCRIP", 4)
s {SCRIP}= "1.2345"
check("a four-decimal registered currency", s, "1.2345")

' Registering twice UPDATES rather than duplicating, so setup code is safe to
' re-run -- the same rule watchers already follow for re-declaration.
before = count(money.currencies())
money.register("PTS", 0)
check("re-registering does not duplicate", count(money.currencies()), before)

' Registered currencies are still currencies: mismatch rules apply.
on error goto next
u {USD}= "5.00"
x = p + u
check("a registered currency cannot be added to USD", error.message, "cannot add money in different currencies (PTS and USD)")
error.clear()

' ------------------------------------------------- retire, do not remove
' REMOVAL IS NOT OFFERED. Removing a currency does not unmake the values that
' already exist, and archived data is exactly what money is for. Retiring
' refuses NEW values and keeps reading old ones.
money.retire("PTS")
check("an existing value still computes after retirement", p * 2, "3000")
check("and still displays", p, "1500")

fresh {PTS}= "5"
check("but a new value is refused", error.message, "PTS is a historical currency: existing values still read, but new ones cannot be created")
error.clear()

' THE BUILT-IN TABLE IS CURRENT CURRENCIES ONLY. iso-codes ships the live ISO
' 4217 list, so withdrawn currencies -- ITL, DEM, FRF, HRK -- are simply not
' there. Working with historical data therefore means registering them, which
' is the clearest justification registration has.
on error stop
on error goto next
gone {ITL}= "1000"
check("a withdrawn currency is not built in", error.message, "assign modifier not found: ITL")
error.clear()

money.register("ITL", 0)
lira {ITL}= "1000"
check("registering it makes historical data expressible", lira, "1000")
error.clear()

' A BUILT-IN currency can be retired too -- a program should not need a new
' interpreter to mark one defunct.
money.retire("AED")
aed {AED}= "10.00"
check("a built-in currency can be retired", error.message, "AED is a historical currency: existing values still read, but new ones cannot be created")
error.clear()

' Re-registering a retired code brings it back, which is what makes retirement
' a policy rather than a one-way door.
money.register("PTS", 0)
revived {PTS}= "7"
check("re-registering revives a retired currency", revived, "7")
error.clear()

' ------------------------------------------------------------- refusals
r = money.register("pts", 2)
check("a lower-case code is refused", error.message, "a currency code must be upper-case letters")
error.clear()
r = money.register("X", 2)
check("a one-character code is refused", error.message, "a currency code is 2 to 6 characters")
error.clear()
r = money.register("TOOLONGCODE", 2)
check("an over-long code is refused", error.message, "a currency code is 2 to 6 characters")
error.clear()
r = money.register("ABC", 99)
check("an absurd exponent is refused", error.message, "a currency exponent is a whole number from 0 to 8")
error.clear()
r = money.register("ABC", 1.5)
check("a fractional exponent is refused", error.message, "a currency exponent is a whole number from 0 to 8")
error.clear()
money.retire("NOSUCH")
check("retiring an unknown currency is refused", error.message, "no such currency: NOSUCH")
error.clear()
r = money.nonsense()
check("an unknown verb is refused", error.message, "invalid function call: money.nonsense")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
