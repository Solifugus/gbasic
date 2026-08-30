' PLAT-MONEY phase 4: the time value of money (stdlib/finance.bas).
'
' gBASIC's statistics library covers SECURITIES ANALYTICS -- returns, Sharpe,
' drawdown, VaR, CAPM, event studies. It has never had the other half of
' finance, the half a line-of-business application actually computes: what a
' loan payment is, what a lease is worth today, whether a project earns its
' cost of capital.
'
' EXPECTED VALUES ARE EXTERNAL. Each is either a figure a spreadsheet produces
' for the same inputs (PMT 250,000 at 0.5%/month over 360 is -1498.88 in both
' Excel and LibreOffice) or computed in Python outside gBASIC. None was
' recorded from a run: a TVM function wrong in the second decimal returns a
' number a finance person would act on.

load finance

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

' ------------------------------------------------- the five TVM quantities
p {USD}= "250000.00"
check("pmt: a 250k mortgage at 6%/yr over 30 years", finance.pmt(0.06 / 12, 360, p), "-1498.88")

pay {USD}= "-1000.00"
check("pv: 1000/month for 30 years at 6%/yr", finance.pv(0.06 / 12, 360, pay), "166791.61")

' fv changed SHAPE and SIGN (design §7): it is Excel's annuity form now, and
' the old one did not negate. Paying 10000 in returns 16288.95, so the present
' value is NEGATIVE and the answer positive -- the old call's `c` was positive
' and its answer positive, which is the sign defect this migration fixes.
c {USD}= "-10000.00"
check("fv: 10000 at 5% for 10 years", finance.fv(0.05, 10, 0, c), "16288.95")

' The PAYMENT IS NEGATIVE now. The old nper took its absolute value, which
' quietly accepted a sign combination the equation cannot balance; following
' Excel strictly means a repayment leaves the borrower and says so.
pp {USD}= "-1498.88"
check("nper: rounds back to the term it came from", round(finance.nper(0.06 / 12, pp, p), 2), 360)

' A zero rate is its own case -- pow(1,n) is fine but the formula divides by r.
check("pmt at a zero rate is just the principal split", finance.pmt(0, 10, p), "-25000.00")
check("pv at a zero rate is the payments added up", finance.pv(0, 10, pay), "10000.00")

' ---------------------------------------------------------- appraisal
a {USD}= "1000.00"
check("npv: three years of 1000 at 10%", finance.npv(0.10, [a, a, a]), "2486.85")

' irr takes EXCEL'S SHAPE now (design §7): values[0] is the period-0 flow, so
' the outlay lives inside the array as a negative rather than as its own
' argument. That is also what lets xirr's dates line up one-for-one.
outlay {USD}= "-2486.85"
check("irr: recovers the rate npv used",
      round(finance.irr([outlay, a, a, a]), 4), 0.1)

' ------------------------------------------------------ dated cash flows
' Microsoft's own documented XIRR/XNPV example, recomputed independently:
' 0.373363 and 2086.65.
f0 {USD}= "-10000.00"
f1 {USD}= "2750.00"
f2 {USD}= "4250.00"
f3 {USD}= "3250.00"
f4 {USD}= "2750.00"
t0 {date}= "2008-01-01"
t1 {date}= "2008-03-01"
t2 {date}= "2008-10-30"
t3 {date}= "2009-02-15"
t4 {date}= "2009-04-01"
dated = [f0, f1, f2, f3, f4]
when = [t0, t1, t2, t3, t4]
check("xirr: Excel's documented example", round(finance.xirr(dated, when), 6), 0.373363)
check("xnpv: the same example at 9%", finance.xnpv(0.09, dated, when), "2086.65")

' xnpv at the xirr must be (near) zero -- the defining relationship, and one
' no reference can drift out from under.
at_root = finance.xnpv(finance.xirr(dated, when), dated, when)
check("xnpv at the xirr is zero", abs(number(string(at_root))) < 0.01, true)

' --------------------------------------------------------- day counts
' Verified against calendar arithmetic computed outside gBASIC.
ja {date}= "2026-01-31"
mb {date}= "2026-03-31"
check("actual/360", round(finance.year_fraction(ja, mb, "actual/360"), 8), 0.16388889)
check("actual/365", round(finance.year_fraction(ja, mb, "actual/365"), 8), 0.16164384)
check("actual/actual", round(finance.year_fraction(ja, mb, "actual/actual"), 8), 0.16164384)
' 30/360 disagrees with all three, which is the entire reason to name it.
check("30/360 differs from the actual counts",
      round(finance.year_fraction(ja, mb, "30/360"), 8), 0.16666667)

' A leap year is exactly one year under actual/actual, and the convention has
' to split at the boundary to get a span right.
la {date}= "2024-01-01"
lb {date}= "2025-01-01"
check("actual/actual: a leap year is exactly 1", finance.year_fraction(la, lb, "actual/actual"), 1)
sa {date}= "2023-12-15"
sb {date}= "2025-03-10"
check("actual/actual: a span across three years",
      round(finance.year_fraction(sa, sb, "actual/actual"), 8), 1.23287671)

' ------------------------------------------------------- depreciation
cost {USD}= "50000.00"
sal {USD}= "5000.00"
check("sln: straight line", finance.sln(cost, sal, 10), "4500.00")
check("syd: year 1 is 10/55 of the base", finance.syd(cost, sal, 10, 1), "8181.82")
check("syd: year 10 is 1/55", finance.syd(cost, sal, 10, 10), "818.18")
check("ddb: year 1 is twice straight line", finance.ddb(cost, sal, 10, 1), "10000.00")
check("ddb: year 2 is 20% of what is left", finance.ddb(cost, sal, 10, 2), "8000.00")

' --------------------------------------------------- THE SCHEDULE TIER
' The load-bearing property, and it is ARITHMETIC rather than a golden: every
' payment is rounded to whole minor units because you cannot pay a third of a
' cent, those roundings accumulate, and a schedule that used one figure
' throughout would end owing a few cents or having overpaid. The final payment
' is adjusted, which is what lenders do.
loan {USD}= "1000.00"
rows = finance.schedule(0.01, 12, loan)
check("the schedule has one row per period", count(rows), 12)

zero {USD}= "0.00"
last = rows[count(rows) - 1]
check("the final balance is EXACTLY zero", last.balance, zero)

principal_total {USD}= "0.00"
interest_total {USD}= "0.00"
paid_total {USD}= "0.00"
for each r in rows
    principal_total = principal_total + r.principal
    interest_total = interest_total + r.interest
    paid_total = paid_total + r.payment
next
check("the principal parts sum to the loan EXACTLY", principal_total, loan)
check("and payments equal principal plus interest", paid_total, loan + interest_total)

' Interest must fall and principal must rise: an amortization schedule that
' got this backwards would still sum correctly.
check("interest falls over the term", rows[0].interest > rows[11].interest, true)
check("principal rises over the term", rows[0].principal < rows[11].principal, true)
check("the first interest charge is 1% of the loan", rows[0].interest, "10.00")

' ------------------------------------------------ rate: the fifth solver
' The DEFINING property, and one no reference can drift out from under: the
' rate recovered from a payment must reproduce that payment. Checked as a
' round trip rather than against a literal, so it cannot rot.
recovered = finance.rate(360, pp, p)
check("rate: recovers the rate pmt used", round(recovered * 12, 6), 0.06)
check("rate: and round-trips back to the payment",
      finance.pmt(recovered, 360, p), "-1498.88")

' A zero rate is the case the equation divides by, so it gets its own check.
flat {USD}= "-2500.00"
flat_pv {USD}= "25000.00"
' TOLERANCE, not equality, and the reason is worth stating: near r = 0 the
' annuity factor (g-1)/r is catastrophic cancellation -- g is 1 + something
' tiny, and subtracting 1 throws away most of the significant digits -- so the
' residual goes noisy and bisection stalls around 1e-9 rather than reaching 0.
' Asserting exact zero would be asserting something arithmetic cannot deliver.
check("rate: a zero rate is found to within 1e-6",
      abs(finance.rate(10, flat, flat_pv)) < 0.000001, true)

' ------------------------------------------------- the optional tail
' `timing` and `fv` are what a spreadsheet omits. Beginning-of-period payments
' are smaller, because every payment gets one extra period of interest.
at_end = finance.pmt(0.06 / 12, 360, p)
at_begin = finance.pmt(0.06 / 12, 360, p, 0, "begin")
check("timing: a begin-of-period payment is smaller", at_begin > at_end, true)
check("timing: by exactly one period of interest",
      round(number(string(at_begin)) * (1 + 0.06 / 12), 2),
      round(number(string(at_end)), 2))

' A balloon balance reduces the payment: less principal is amortized.
balloon {USD}= "-50000.00"
with_balloon = finance.pmt(0.06 / 12, 360, p, balloon)
check("fv: a balloon balance lowers the payment", with_balloon > at_end, true)

' Omitting the tail must equal supplying its defaults.
check("omitting the tail equals supplying the defaults",
      finance.pmt(0.06 / 12, 360, p, 0, "end"), at_end)

' ------------------------------------- more than one IRR is said out loud
' Descartes: a project that changes sign more than once can have several
' rates, and bisection returns whichever its bracket contains. The classic
' [-1, 5, -6] has roots at 100% and 200%.
'
' `on warning goto next` captures it as a VALUE rather than letting it reach
' stderr, which is what lets a fixture assert on a warning at all.
on warning goto next
m0 {USD}= "-1000.00"
m1 {USD}= "5000.00"
m2 {USD}= "-6000.00"
multi = finance.irr([m0, m1, m2])
' `w = warning` snapshots AND claims it -- there is no warning.clear(); reading
' bare `warning` is what acknowledges one.
w = warning
check("multiple roots are warned about", contains(w.message, "more than one rate"), true)
check("and a root is still returned", multi > 0, true)

' THE CONTROL. A conventional project changes sign once and must be SILENT,
' or the warning is noise an author learns to ignore.
s0 {USD}= "-1000.00"
s1 {USD}= "600.00"
s2 {USD}= "600.00"
plain = finance.irr([s0, s1, s2])
check("a single sign change warns about nothing", warning, false)
check("and still returns a rate", plain > 0, true)
on warning print

' ---------------------------------------------------------- refusals
on error goto next

x = finance.pmt(0.005, 360, 250000)
check("a bare number is not a principal", error.message, "finance.pmt needs at least one amount as money")
error.clear()

x = finance.pmt("0.005", 360, p)
check("a rate must be a number", error.message, "finance.pmt expects the period rate as a number (0.05 is 5%)")
error.clear()

x = finance.pmt(0.005, 0, p)
check("zero periods is refused", error.message, "finance.pmt expects a whole number of periods, 1 or more")
error.clear()

x = finance.pmt(0.005, 12.5, p)
check("a fractional term is refused", error.message, "finance.pmt expects a whole number of periods, 1 or more")
error.clear()

' A payment too small to cover the interest never repays. Negative, because
' the payment leaves the borrower -- a POSITIVE one would mean money arriving
' from both directions, which is a different mistake and is caught by the
' balance check rather than this one.
tiny {USD}= "-1.00"
x = finance.nper(0.06 / 12, tiny, p)
check("a payment that never repays is refused", error.message, "finance.nper: the payment never repays the present value at that rate")
error.clear()

' A single flow CAN break even, at a negative rate -- 1000 against 2486.85 is
' -59.8% -- so the unbreakable case needs flows too small at ANY rate: even at
' -99% the present value of 1.00 is only 100.
huge {USD}= "1000000.00"
penny {USD}= "1.00"
x = finance.irr([huge, penny])
check("flows that cannot break even at any rate are refused", error.message, "finance.irr: no rate between -100% and 1000% makes these flows break even")
error.clear()

' And the control: the case my first version wrongly assumed was impossible.
' A single flow against a larger outlay breaks even at a NEGATIVE rate.
single = finance.irr([outlay, a])
check("a single flow breaks even at a negative rate", round(single, 3), -0.598)
error.clear()

x = finance.syd(cost, sal, 10, 11)
check("depreciating past the asset's life is refused", error.message, "finance.syd: the period is past the asset's life")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
