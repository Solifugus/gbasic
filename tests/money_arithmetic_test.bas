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
' 9223372036854775 cents is past 2^53 (9007199254740992), so the double path
' is provably lossy here. Expected values are 9223372036854775 x n, done in
' integer arithmetic:  x2 = 18446744073709550,  x3 = 27670116110564325.
big {USD}= "92233720368547.75"
check("x2 above 2^53 is exact", big * 2, "184467440737095.50")
check("x3 above 2^53 is exact", big * 3, "276701161105643.25")
check("x10 above 2^53 is exact", big * 10, "922337203685477.50")

' The CONTROL: identical on both binaries. Without it the tier above could be
' mistaken for a general claim about multiplication.
mid {USD}= "45000000000000.01"
check("a magnitude where the defect does NOT show", mid * 2, "90000000000000.02")

' ---------------------------------------------------------- exact addition
' +/- were always integer, but were UNCHECKED -- undefined behaviour at the
' boundary rather than a wrap. These confirm the arithmetic still works.
a {USD}= "92233720368547.75"
b {USD}= "0.25"
check("addition at scale", a + b, "92233720368548.00")
check("subtraction at scale", a - b, "92233720368547.50")
neg {USD}= "-1.50"
check("adding a negative", a + neg, "92233720368546.25")

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

' Division still cannot split a hundred into three whole cents -- that is
' ALLOCATION, and it is phase 4, not a defect in the operator.
check("the penny problem is still open (phase 4)", (d / 3) * 3, "99.99")

' ------------------------------------------------------- scalar on the left
check("number * money works too", 3 * big, "276701161105643.25")
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
