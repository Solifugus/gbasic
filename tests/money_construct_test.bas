' PLAT-MONEY phase 0: exact construction (docs/money_design.md §7).
'
' SELF-CHECKING, and that matters more here than usual. Every defect this
' phase fixes produced a PLAUSIBLE NUMBER rather than an error -- a cent off
' at the top of the range, a rounding rule that flipped on the binary
' representation of the literal. A golden would have recorded the damaged
' value AS the expected output and defended it from then on, which is exactly
' how these survived as long as they did.

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

' ------------------------------------------------- the range, from text
' The value gdash reported becoming ...76, which is the case the whole phase
' exists for. NOTE it is not actually the top of the range -- int64 max is
' 9223372036854775807 CENTS, so the real limit is a thousand times higher, and
' both are tested because the double path fails well below the type's limit.
gd {USD}= "92233720368547.75"
check("gdash's value survives from text", gd, "92233720368547.75")

' int64's range is ASYMMETRIC: the most negative value is one greater in
' magnitude than the most positive. Building the magnitude signed and negating
' at the end would refuse a value the type can hold -- a bug this fixture
' caught in the first version of the parser, and a second one in the renderer,
' which printed "--92233720368547758.-8" because negating LLONG_MIN overflows.
top {USD}= "92233720368547758.07"
check("int64 max, exactly", top, "92233720368547758.07")

bottom {USD}= "-92233720368547758.08"
check("int64 min, exactly -- one greater in magnitude", bottom, "-92233720368547758.08")

' ------------------------------------------- the range, from a literal
' The double for this value renders back to the same decimal under
' shortest-round-trip (PLAT-NUMFMT), so the literal route recovers it too.
' This is what makes the fix reach ordinary code and not only quoted text.
lit {USD}= 92233720368547.75
check("the same value survives from a literal", lit, "92233720368547.75")
check("both routes agree", string(lit) = string(gd), true)

' ------------------------------------------------------ ordinary values
a {USD}= 19.95
check("an ordinary literal", a, "19.95")
b {USD}= "1234.56"
check("ordinary text", b, "1234.56")
z {USD}= 0
check("zero", z, "0.00")
c {USD}= "-0.05"
check("a small negative", c, "-0.05")
d {USD}= "0.01"
check("one cent", d, "0.01")
e {USD}= ".5"
check("a leading decimal point", e, "0.50")
f {USD}= "  42.00  "
check("surrounding space is tolerated", f, "42.00")
g {USD}= "+7.25"
check("an explicit plus", g, "7.25")
h {USD}= "0000012.30"
check("leading zeros", h, "12.30")

' ----------------------------------------------------- rounding is HALF-EVEN
' THE POINT OF THIS TIER: before phase 0 the rule depended on the binary
' representation of the literal -- 0.125 rounded up while 0.145 rounded down,
' which looks like banker's rounding and was not. Now the value is rendered to
' its shortest decimal and the rule applies to the TEXT, so ties go to even
' every time and the answer is predictable from what the author wrote.
t1 {USD}= 0.125
check("0.125 ties to even (0.12)", t1, "0.12")
t2 {USD}= 0.135
check("0.135 ties to even (0.14)", t2, "0.14")
t3 {USD}= 0.145
check("0.145 ties to even (0.14)", t3, "0.14")
t4 {USD}= 0.155
check("0.155 ties to even (0.16)", t4, "0.16")
t5 {USD}= 0.126
check("above the tie still rounds up", t5, "0.13")
t6 {USD}= 0.124
check("below the tie still rounds down", t6, "0.12")
t7 {USD}= -0.125
check("a negative tie goes to even too", t7, "-0.12")
t8 {USD}= 0.006
check("0.006 rounds up", t8, "0.01")
t9 {USD}= 0.004
check("0.004 rounds down", t9, "0.00")

' ------------------------------------------- computed values ROUND, not raise
' price * 1.08 carries seventeen digits as a matter of course. Refusing that
' would make money unusable for arithmetic, which is why excess precision is
' rejected only when the AUTHOR wrote it (see the refusal tier).
c1 {USD}= 0.1 + 0.2
check("a computed value rounds rather than raising", c1, "0.30")
c2 {USD}= 19.95 * 1.08
check("a tax calculation lands on cents", c2, "21.55")
c3 {USD}= 0.00001
check("a value below a cent becomes zero", c3, "0.00")
c4 {USD}= 0.000001
check("an exponent-rendered small value parses", c4, "0.00")

' ------------------------------------------------------------- idempotence
' Re-applying the modifier to a value that already has it must not change or
' lose anything -- otherwise a helper that defensively re-tags its argument
' would quietly degrade it.
once {USD}= "12.34"
twice {USD}= once
check("the modifier is idempotent", twice, "12.34")

' ------------------------------------------------------------- integer input
i {USD}= 5
check("a bare integer", i, "5.00")
j {USD}= 1000000
check("a large integer", j, "1000000.00")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
