' PLAT-MONEY phase 4: allocation -- splitting money into PAYABLE parts.
'
' Division and allocation are different problems, and guard digits only solve
' the first. `(100.00 / 3) * 3` comes back whole because a third of a dollar
' has somewhere to live -- but three PAYMENTS cannot each be 33.3333: an
' invoice, a payroll line or a dividend has to be a whole number of minor
' units. So allocation works at the minor unit and distributes the remainder
' one unit at a time, which is the only way the parts sum back exactly.

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

function total_of(parts, ccy)
    t = parts[0] * 0
    for each x in parts
        t = t + x
    next
    return t
end function

h {USD}= "100.00"
zero {USD}= "0.00"

' ------------------------------------------------- the indivisible case
three = money.allocate(h, 3)
check("100 into 3 gives three parts", count(three), 3)
check("and they are not all equal", string(three), "[33.34,33.33,33.33]")
check("THE POINT: they sum back exactly", total_of(three, "USD"), h)

' Never three of 33.33, which loses a cent; never three of 33.34, which
' invents one. Both would be plausible and both would be wrong.
check("the parts are not all 33.33", three[0] = three[1], false)
check("but only one part carries the extra", three[1] = three[2], true)

' ----------------------------------------------------------- weighted
w = money.allocate(h, [1, 1, 2])
check("weights split proportionally", string(w), "[25.00,25.00,50.00]")
check("and still sum exactly", total_of(w, "USD"), h)

odd = money.allocate(h, [1, 1, 1, 1, 1, 1, 7])
check("an awkward weighting still sums exactly", total_of(odd, "USD"), h)

' A zero weight gets nothing, and must not receive a remainder unit either --
' allocating to a party with no share would be a real error.
z = money.allocate(h, [1, 0, 1])
check("a zero weight gets nothing", z[1], zero)
check("and the rest still sums exactly", total_of(z, "USD"), h)

' -------------------------------------------------- currency awareness
' JPY has no minor unit, so its parts are whole yen.
y {JPY}= "100"
jp = money.allocate(y, 3)
check("JPY splits into whole yen", string(jp), "[34,33,33]")
jz {JPY}= "0"
check("and sums exactly", total_of(jp, "JPY"), y)

k {KWD}= "1.000"
kp = money.allocate(k, 3)
check("KWD splits at three places", string(kp), "[0.334,0.333,0.333]")
check("and sums exactly", total_of(kp, "KWD"), k)

' ------------------------------------------------------------- edges
n {USD}= "-100.00"
np = money.allocate(n, 3)
check("a negative amount allocates", string(np), "[-33.34,-33.33,-33.33]")
check("and sums exactly", total_of(np, "USD"), n)

one = money.allocate(h, 1)
check("one part is the whole amount", one[0], h)

check("zero allocates to zeros", total_of(money.allocate(zero, 3), "USD"), zero)

exact = money.allocate(h, 4)
check("an amount that divides evenly needs no remainder", string(exact), "[25.00,25.00,25.00,25.00]")

' ---------------------------------------------------------- refusals
on error goto next
x = money.allocate(h, 0)
check("zero parts is refused", error.message, "money.allocate expects a whole part count of 1 or more")
error.clear()
x = money.allocate(h, 2.5)
check("a fractional part count is refused", error.message, "money.allocate expects a whole part count of 1 or more")
error.clear()
x = money.allocate(h, [])
check("no weights is refused", error.message, "money.allocate needs at least one weight")
error.clear()
x = money.allocate(h, [0, 0])
check("all-zero weights are refused", error.message, "money.allocate weights must not all be zero")
error.clear()
x = money.allocate(h, [1, -1])
check("a negative weight is refused", error.message, "money.allocate weights must be whole numbers of 0 or more")
error.clear()
x = money.allocate(100, 3)
check("a bare number is not money", error.message, "money.allocate expects money")
error.clear()
on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
