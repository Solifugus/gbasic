' PLAT-MONEY: the lossless EXIT, and the round trip it closes.
'
' Construction became exact below the minor unit on 2026-08-29 (the authored
' threshold moved from the minor unit to the storage scale). That left the
' mirror defect: `string(m)` renders at the minor unit and `number(m)` refuses
' money outright, so an exact sub-cent value could be put IN and never got
' back OUT. `money.text` is the exit.
'
' SELF-CHECKING rather than golden, for the reason the rest of this phase is:
' every failure here is a plausible number. A money.text that quietly rendered
' at the minor unit would return "0.10" for a rate of 0.10432 -- perfectly
' ordinary text, and a golden would record it as the expectation.

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

' ------------------------------------------------- lossless by default
' The default is the STORAGE scale, which is the currency's minor unit plus
' four guard digits -- so it varies by currency and is not a constant 6.
a {USD}= "0.10432"
check("USD renders at its storage scale", money.text(a), "0.104320")

b {JPY}= "1995"
check("JPY renders at its storage scale", money.text(b), "1995.0000")

c {KWD}= "1.2345678"
check("KWD renders at its storage scale", money.text(c), "1.2345678")

' ----------------------------------------------- THE DEFINING PROPERTY
' Out and back in, unchanged. This is the property the exit exists for, and
' it is checkable in-language rather than as a list of expected digits.
d {USD}= money.text(a)
check("USD round-trips through text exactly", d = a, true)

e {KWD}= money.text(c)
check("KWD round-trips through text exactly", e = c, true)

' A computed value carrying guard digits must survive the trip too -- this is
' the case a construction-only test cannot reach.
f {USD}= "100.00"
g = f / 3
h {USD}= money.text(g)
check("a computed third round-trips", h = g, true)
check("and is still exact when multiplied back", h * 3, "100.00")

' ------------------------------------------- THE CONTROL: not just string()
' Without this, a money.text implemented as string() would pass every tier
' above except the round trips -- and would pass those for any value that
' happens to be whole cents. The two must DIFFER on a sub-cent value.
check("string() rounds to the minor unit", string(a), "0.10")
check("money.text does not", money.text(a) != string(a), true)

' --------------------------------------------------- an explicit width
check("an explicit width renders there", money.text(a, 2), "0.10")
check("zero places is legal", money.text(a, 0), "0")
check("a width wider than the scale pads", money.text(a, 8), "0.10432000")

' Rounding at an explicit width is half-even, like everything else here.
i {USD}= "0.125"
check("half-even rounds 0.125 to 0.12", money.text(i, 2), "0.12")
j {USD}= "0.135"
check("half-even rounds 0.135 to 0.14", money.text(j, 2), "0.14")

' ------------------------------------------------------- sign and zero
k {USD}= "-0.0001"
check("a small negative keeps its sign", money.text(k), "-0.000100")
l {USD}= "0.00"
check("zero renders without a sign", money.text(l), "0.000000")

' ----------------------------------------------------------- refusals
on error goto next

m = money.text("19.95")
check("text is refused", error.message, "money.text expects money")
error.clear()

n = money.text(a, 2.5)
check("a fractional width is refused", error.message, "money.text expects a whole number of decimal places from 0 to 18")
error.clear()

o = money.text(a, -1)
check("a negative width is refused", error.message, "money.text expects a whole number of decimal places from 0 to 18")
error.clear()

p = money.text()
check("no arguments is refused", error.message, "money.text expects an amount and an optional number of decimal places")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
