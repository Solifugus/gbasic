' PLAT-MONEY phase 2: currency identity, per-currency scale, guard digits.
'
' SELF-CHECKING, for the reason the whole money suite is: every defect in this
' area produces a plausible number rather than an error. A golden would have
' recorded 99.99 for `100 / 3 * 3` as the expected answer -- which it was, for
' the type's whole life before guard digits.

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

' ------------------------------------------- currencies other than USD exist
' `USD` used to be a single hardcoded strcmp in the modifier dispatch. It is a
' table lookup now, so every ISO 4217 code arrived at once.
u {USD}= "19.95"
e {EUR}= "19.95"
g {GBP}= "19.95"
check("USD", u, "19.95")
check("EUR", e, "19.95")
check("GBP", g, "19.95")

' ------------------------------------------------- per-currency minor units
' The whole reason scale cannot be hardcoded: JPY has no minor unit, KWD has
' three, CLF has four. Displaying JPY as "1995.00" would be wrong, not merely
' ugly.
j {JPY}= "1995"
check("JPY has no decimal places", j, "1995")
k {KWD}= "19.950"
check("KWD has three", k, "19.950")
c {CLF}= "1.2345"
check("CLF has four", c, "1.2345")

' ---------------------------------------------------------- GUARD DIGITS
' THE TIER THIS PHASE EXISTS FOR. Four digits below the minor unit are kept
' through intermediates and rounded once, at display. At cents scale each of
' these lost money on the way.
h {USD}= "100.00"
check("100 / 3 * 3 comes back whole", (h / 3) * 3, "100.00")
one {USD}= "1.00"
check("1 / 3 * 3 comes back whole", (one / 3) * 3, "1.00")
check("1 / 7 * 7 comes back whole", (one / 7) * 7, "1.00")
' A third of a cent is invisible at display but is still there.
cent {USD}= "0.01"
third = cent / 3
check("a third of a cent displays as zero", third, "0.00")
check("but is retained: x3 gives the cent back", third * 3, "0.01")

' JPY has no minor unit at all, so its guard digits are the only sub-unit
' precision it has -- the case a cents-shaped implementation cannot express.
y {JPY}= "100"
check("JPY 100 / 3 * 3 comes back whole", (y / 3) * 3, "100")

' -------------------------------------------------- currency is enforced
on error goto next

x = u + e
check("adding different currencies raises", error.message, "cannot add money in different currencies (USD and EUR)")
error.clear()

x = u - e
check("subtracting different currencies raises", error.message, "cannot subtract money in different currencies (USD and EUR)")
error.clear()

x = u < e
check("ordering different currencies raises", error.message, "cannot order money in different currencies (USD and EUR)")
error.clear()

' Equality ANSWERS rather than raising: "is USD 19.95 equal to EUR 19.95" is a
' real question whose answer is no. Ordering is not a question at all without
' a rate. This split follows PLAT-EQ.
check("equality across currencies answers false", u = e, false)
check("inequality across currencies answers true", u != e, true)
check("equality within a currency still works", u = g, false)
u2 {USD}= "19.95"
check("and is true for equal same-currency values", u = u2, true)

relabelled {EUR}= u
check("relabelling refuses rather than inventing a rate", error.message, "cannot relabel USD as EUR: converting needs an exchange rate")
error.clear()

same {USD}= u
check("the same currency is still idempotent", same, "19.95")
error.clear()

on error stop

' --------------------------------------------------------- same-currency ops
check("addition within a currency", u + u2, "39.90")
check("multiplication keeps the currency", string(u * 2), "39.90")

' -------------------------------------------------------------- the table
all = money.currencies()
check("the ISO table is built in", count(all) >= 178, true)
found_jpy = false
for each row in all
    if row.code = "JPY" then
        found_jpy = true
        check("JPY's exponent is 0 in the table", row.exponent, 0)
    end if
next
check("JPY is in the table", found_jpy, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
