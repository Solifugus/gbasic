' PLAT-MONEY phase 1: arithmetic that stays in integers.
'
' THE DEFECT THIS PINS DOES NOT REPRODUCE AT ORDINARY MAGNITUDES. `money * n`
' computed `(double)cents * n` and then rounded, and below 2^53 units the
' double has precision to spare -- so `x * 3` on a few billion dollars is
' exactly right and a casual probe concludes there is no bug. Every expected
' value in the multiply tier is therefore ABOVE 2^53 units, and computed
' independently (by integer arithmetic outside gBASIC) rather than recorded
' from a run. A golden taken from the old binary would have enshrined
' 184467440737095.52 as correct.
'
' The control cases matter as much: `45000000000000.01 * 2` gives the same
' answer on both binaries, which is why a naively-chosen magnitude asserts
' nothing at all.

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

' ------------------------------------ integral scalars, ABOVE 2^53 units
' At guard scale, $50,000,000,000.01 is 50000000000010000 units -- past 2^53
' (9007199254740992), where the old double path was provably lossy, and well
' inside USD's int64 ceiling of about $9.22 trillion. Expected values are
' 50000000000010000 x n, done in integer arithmetic outside gBASIC.
big {USD}= "50000000000.01"
check("x2 above 2^53 is exact", big * 2, "100000000000.02")
check("x3 above 2^53 is exact", big * 3, "150000000000.03")
check("x10 above 2^53 is exact", big * 10, "500000000000.10")

' The CONTROL: identical on both binaries. Without it the tier above could be
' mistaken for a general claim about multiplication.
mid {USD}= "45000.01"
check("a magnitude where the defect does NOT show", mid * 2, "90000.02")

' ---------------------------------------------------------- exact addition
' +/- were always integer, but were UNCHECKED -- undefined behaviour at the
' boundary rather than a wrap. These confirm the arithmetic still works.
a {USD}= "50000000000.01"
b {USD}= "0.25"
check("addition at scale", a + b, "50000000000.26")
check("subtraction at scale", a - b, "49999999999.76")
neg {USD}= "-1.50"
check("adding a negative", a + neg, "49999999998.51")

' -------------------------------------------------- fractional scalars
' A fractional scalar rounds HALF-EVEN at the scale, and the ties are the
' cases that distinguish it from half-up.
p {USD}= "0.05"
check("0.05 * 0.5 = 0.025 ties to even", p * 0.5, "0.02")
q {USD}= "0.15"
check("0.15 * 0.5 = 0.075 ties to even", q * 0.5, "0.08")
r {USD}= "19.95"
check("a tax multiplier", r * 1.08, "21.55")
s {USD}= "100.00"
check("a percentage", s * 0.175, "17.50")

' ------------------------------------------------------------- division
d {USD}= "100.00"
check("100 / 4 is exact", d / 4, "25.00")
check("100 / 8 ties to even", d / 8, "12.50")
check("100 / 3 rounds", d / 3, "33.33")
check("100 / 7 rounds", d / 7, "14.29")
check("dividing by a fraction multiplies", d / 0.5, "200.00")

' PHASE 2 CHANGED THIS ANSWER, and the change is the guard digits earning
' their place: at cents scale (100/3)*3 was 99.99, because a third of a dollar
' had nowhere to live. With four digits below the cent it round-trips whole.
' What remains for phase 4 is ALLOCATION -- splitting 100.00 into three
' PAYABLE amounts of 33.33 / 33.33 / 33.34 that sum back exactly -- which is a
' different problem from arithmetic not losing money.
check("guard digits close the round-trip gap", (d / 3) * 3, "100.00")

' ------------------------------------------------------- scalar on the left
check("number * money works too", 3 * big, "150000000000.03")
check("and rounds the same way", 0.5 * q, "0.08")

' ----------------------------------------------------------- zero and sign
z {USD}= "0.00"
check("zero times anything", z * 12345, "0.00")
check("anything times zero", big * 0, "0.00")
nn {USD}= "-19.95"
check("a negative scaled", nn * 2, "-39.90")
check("a negative divided", nn / 2, "-9.98")
check("a negative tie goes to even", nn * 0.5, "-9.98")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
