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
check("pmt: a 250k mortgage at 6%/yr over 30 years", finance.pmt(p, 0.06 / 12, 360), "-1498.88")

pay {USD}= "-1000.00"
check("pv: 1000/month for 30 years at 6%/yr", finance.pv(pay, 0.06 / 12, 360), "166791.61")

c {USD}= "10000.00"
check("fv: 10000 at 5% for 10 years", finance.fv(c, 0.05, 10), "16288.95")

pp {USD}= "1498.88"
check("nper: rounds back to the term it came from", round(finance.nper(p, pp, 0.06 / 12), 2), 360)

' A zero rate is its own case -- pow(1,n) is fine but the formula divides by r.
check("pmt at a zero rate is just the principal split", finance.pmt(p, 0, 10), "-25000.00")
check("pv at a zero rate is the payments added up", finance.pv(pay, 0, 10), "10000.00")

' ---------------------------------------------------------- appraisal
a {USD}= "1000.00"
check("npv: three years of 1000 at 10%", finance.npv(0.10, [a, a, a]), "2486.85")
out {USD}= "2486.85"
check("irr: recovers the rate npv used", round(finance.irr(out, [a, a, a]), 4), 0.1)

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
rows = finance.schedule(loan, 0.01, 12)
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

' ---------------------------------------------------------- refusals
on error goto next

x = finance.pmt(250000, 0.005, 360)
check("a bare number is not a principal", error.message, "finance.pmt expects the principal as money")
error.clear()

x = finance.pmt(p, "0.005", 360)
check("a rate must be a number", error.message, "finance.pmt expects the period rate as a number (0.05 is 5%)")
error.clear()

x = finance.pmt(p, 0.005, 0)
check("zero periods is refused", error.message, "finance.pmt expects a whole number of periods, 1 or more")
error.clear()

x = finance.pmt(p, 0.005, 12.5)
check("a fractional term is refused", error.message, "finance.pmt expects a whole number of periods, 1 or more")
error.clear()

tiny {USD}= "1.00"
x = finance.nper(p, tiny, 0.06 / 12)
check("a payment that never repays is refused", error.message, "finance.nper: the payment never repays the principal at that rate")
error.clear()

' A single flow CAN break even, at a negative rate -- 1000 against 2486.85 is
' -59.8% -- so the unbreakable case needs flows too small at ANY rate: even at
' -99% the present value of 1.00 is only 100.
huge {USD}= "1000000.00"
penny {USD}= "1.00"
x = finance.irr(huge, [penny])
check("flows that cannot break even at any rate are refused", error.message, "finance.irr: no rate between -99% and 1000% makes these flows break even")
error.clear()

' And the control: the case my first version wrongly assumed was impossible.
' A single flow against a larger outlay breaks even at a NEGATIVE rate.
single = finance.irr(out, [a])
check("a single flow breaks even at a negative rate", round(single, 3), -0.598)
error.clear()

x = finance.syd(cost, sal, 10, 11)
check("depreciating past the asset's life is refused", error.message, "finance.syd: the period is past the asset's life")
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
