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
' The threshold is the STORAGE SCALE, not the minor unit (design §5). USD
' stores 6 places, so seven is the first that cannot be held. It was briefly
' the minor unit, which refused ordinary sub-cent prices -- and refused
' "0.030000" too, which claims no extra precision at all; the acceptance tier
' at the foot of this file is what pins that.
a {USD}= "1.2345678"
check("authored text past the STORAGE scale is refused", error.message, "USD: money text has more decimal places than the currency can store (USD stores 6)")
error.clear()

b {KWD}= "0.12345678"
check("the threshold follows the CURRENCY, not a constant", error.message, "KWD: money text has more decimal places than the currency can store (KWD stores 7)")
error.clear()

c2 {JPY}= "12.00001"
check("a minor-unit-free currency still stores its guard digits", error.message, "JPY: money text has more decimal places than the currency can store (JPY stores 4)")
error.clear()

' ------------------------------------------------------------- range
c {USD}= "9223372036854.78"
check("past USD's ceiling is refused", error.message, "money value is out of range")
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
n {USD}= number("1.2345678")
check("the SAME excess precision, COMPUTED, is accepted", n, "1.23")
error.clear()

o {USD}= "9223372036854.77"
check("just inside the ceiling is accepted", o, "9223372036854.77")
error.clear()

p {USD}= "12"
check("bare digits are accepted", p, "12.00")
error.clear()

q {USD}= "1e2"
check("a well-formed exponent is accepted", q, "100.00")
error.clear()

' --------------------------------- what the MINOR-UNIT threshold used to refuse
' These are the regression tier for the threshold fix. Every one of them was
' refused as "more decimal places than the currency allows" -- a report that
' was false, since the type holds all of them exactly. Sub-cent authored
' prices are ordinary rather than exotic: fuel is posted at $3.459 a gallon
' and electricity quoted at $0.10432 a kWh.
r {USD}= "0.035"
check("a half-cent price is accepted", r, "0.04")
error.clear()

s2 {USD}= "3.459"
check("a posted fuel price is accepted", s2, "3.46")
error.clear()

' Retention is the point, and display cannot show it: 0.10432 rounds to 0.10
' for display, so only arithmetic reveals whether the guard digits survived
' construction. A value rounded to cents at the door gives 100.00 here.
t {USD}= "0.10432"
check("an authored sub-cent value is RETAINED, not rounded at the door", t * 1000, "104.32")
error.clear()

' The requirement that motivated guard digits, in gdash's own words: three
' cents as 3.0000. Trailing zeros claim no precision at all.
u {USD}= "0.030000"
check("trailing zeros are accepted", u, "0.03")
error.clear()

v {USD}= "0.03"
check("and change nothing about the value", u = v, true)
error.clear()

w {USD}= "1.234567"
check("exactly AT the storage scale is accepted", w * 1000000, "1234567.00")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
