' PLAT-MONEY phase 0: what construction REFUSES, and in what words.
'
' The rule splits by ORIGIN (docs/money_design.md §11), and that split is the
' whole reason the type stays usable: text the AUTHOR wrote carrying more
' precision than the currency holds is a bug in their input and is refused,
' while a COMPUTED value carrying seventeen digits is ordinary arithmetic and
' rounds. The pair of tiers below is what keeps each honest -- a refusal tier
' alone could be satisfied by refusing everything.

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

on error goto next

' ------------------------------------------- authored excess precision
a {USD}= "1.234"
check("sub-cent authored text is refused", error.message, "money text has more decimal places than the currency allows")
error.clear()

b {USD}= "0.125"
check("even a single excess digit is refused", error.message, "money text has more decimal places than the currency allows")
error.clear()

' ------------------------------------------------------------- range
c {USD}= "92233720368547758.08"
check("one cent past int64 max is refused", error.message, "money value is out of range")
error.clear()

d {USD}= "999999999999999999999"
check("a wildly out-of-range value is refused", error.message, "money value is out of range")
error.clear()

' ------------------------------------------------------- malformed text
e {USD}= "12 dollars"
check("trailing text is refused", error.message, "money text has trailing characters")
error.clear()

f {USD}= "abc"
check("text that is not a number is refused", error.message, "money text is not a number")
error.clear()

g {USD}= "1.2.3"
check("two decimal points are refused", error.message, "money text has more than one decimal point")
error.clear()

h {USD}= ""
check("empty text is refused", error.message, "money text is not a number")
error.clear()

i {USD}= "1e"
check("a malformed exponent is refused", error.message, "money text has a malformed exponent")
error.clear()

' --------------------------------------------------------- wrong types
j {USD}= true
check("a boolean is refused", error.message, "USD modifier expects a number or decimal text")
error.clear()

k {USD}= [1, 2]
check("an array is refused", error.message, "USD modifier expects a number or decimal text")
error.clear()

l {USD}= nothing
check("nothing is refused", error.message, "USD modifier expects a number or decimal text")
error.clear()

' gBASIC refuses 0/0 outright, so the non-finite path is reached via number("inf").
m {USD}= number("inf")
check("a non-finite number is refused", error.message, "USD modifier expects a finite number")
error.clear()

' ------------------------------------------ THE CONTROL: these must NOT raise
' Without this tier the refusals above could be satisfied by a modifier that
' rejected everything. Each of these is the nearest legal neighbour of a
' refusal directly above it.
n {USD}= 1.234
check("the SAME excess precision, COMPUTED, is accepted", n, "1.23")
error.clear()

o {USD}= "92233720368547758.07"
check("int64 max itself is accepted", o, "92233720368547758.07")
error.clear()

p {USD}= "12"
check("bare digits are accepted", p, "12.00")
error.clear()

q {USD}= "1e2"
check("a well-formed exponent is accepted", q, "100.00")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
