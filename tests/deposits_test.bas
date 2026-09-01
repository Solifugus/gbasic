' deposits.bas -- deposit interest, crediting and certificates
' (docs/lending_design.md §7).
'
' SELF-CHECKING. Every defect here produces an ordinary-looking amount of
' interest: a balance method that fails to distinguish itself, compounding
' conflated with crediting, a penalty silently clamped at zero. A golden would
' record all three as expected.

load deposits
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

function usd(t)
    m {USD}= t
    return m
end function

opened {date}= "2026-01-01"
eom {date}= "2026-01-31"
mid {date}= "2026-01-21"

function mk(method)
    return deposits.account({ opened: opened, balance: usd("10000.00"), rate: 0.05,
                              day_count: "actual/365", balance_method: method,
                              crediting: 30 })
end function

' -------------------------------------------- BALANCE METHOD IS A DIFFERENCE
' A large withdrawal late in the period. `minimum` charges the whole period at
' the lowest balance the account ever held, which is why banks name the method
' in the terms.
big = [{ on: mid, kind: "withdrawal", amount: usd("9000.00") }]
d = deposits.apply(mk("daily"), big, eom)
n = deposits.apply(mk("minimum"), big, eom)
' NOTE these read `credited`, not `accrued`: the window is exactly one
' crediting period, so the interest has been PAID rather than left pending.
' Asserting `accrued` here gets 0.00 -- correctly, and confusingly, which is
' the point of the compounding-versus-crediting tier below.
check("daily earns on each day's own balance", d.credited, "28.77")
check("minimum earns on the lowest balance for the whole period", n.credited, "4.11")
check("so the methods differ, and by a lot", d.credited != n.credited, true)

' AND ONE THAT DOES NOT DIFFER, WHICH IS A FACT ABOUT ARITHMETIC RATHER THAN A
' BUG. Simple interest is linear in the balance, so the average balance earns
' exactly what each day's balance earns. `daily` and `average_daily` agree at a
' constant rate and part company only when the rate is not constant -- under
' tiers, below. Asserting they differ would be asserting something false.
a = deposits.apply(mk("average_daily"), big, eom)
check("average_daily equals daily at a constant rate", a.credited, d.credited)

' ------------------------------------- COMPOUNDING IS NOT CREDITING
' Interest is computed per crediting period and added at the end of it, so the
' next period earns on more. Twelve 30-day periods on 10,000 at 5% comes to
' more than a flat 500, and that difference IS the compounding.
year_end {date}= "2026-12-31"
plain = deposits.apply(mk("daily"), [], year_end)
check("a year of 30-day crediting compounds past simple interest",
      plain.credited > usd("500.00"), true)
check("and the credited interest is on the balance", plain.balance,
      usd("10000.00") + plain.credited)
' Interest since the last crediting date is reported separately, not folded in
' -- an account holder has earned it and has not been paid it.
check("interest since the last crediting date is separate",
      plain.accrued > usd("0.00"), true)
check("available is the balance plus that", plain.available,
      plain.balance + plain.accrued)

' ---------------------------------------------------------- tiers
tiers = [{ from: usd("0.00"), rate: 0.01 },
         { from: usd("10000.00"), rate: 0.03 },
         { from: usd("50000.00"), rate: 0.05 }]
check("the whole balance earns the tier it lands in",
      deposits.tiered_rate(tiers, usd("60000.00"), "whole"), 0.05)
check("and a balance below the first boundary earns the base",
      deposits.tiered_rate(tiers, usd("500.00"), "whole"), 0.01)

' `portion` is the other product, and the difference at a boundary is large --
' which is why the mode is declared rather than assumed.
one_year {date}= "2027-01-01"
portioned = deposits.tiered_interest(tiers, usd("60000.00"), opened, one_year, "actual/365")
whole_rate = deposits.tiered_rate(tiers, usd("60000.00"), "whole")
whole_amt = finance.accrue(usd("60000.00"), whole_rate, opened, one_year, "actual/365")
check("portion and whole disagree at a boundary", portioned != whole_amt, true)
check("and portion is the smaller here", portioned < whole_amt, true)

' ------------------------------------------------------ certificates
cd = deposits.certificate({ opened: opened, balance: usd("10000.00"), rate: 0.05,
                            day_count: "actual/365", balance_method: "daily",
                            crediting: 30, term_days: 365, penalty_days: 90 })
early {date}= "2026-02-01"
r = deposits.redeem(cd, early)
check("interest earned to the redemption date", r.interest, "42.47")
check("the penalty is its own days of interest", r.penalty, "123.29")
' THE PENALTY MAY EXCEED THE INTEREST, and then it reduces PRINCIPAL. Clamping
' it at zero would report proceeds the holder will not receive.
check("a penalty larger than the interest reduces principal",
      r.principal_reduced, true)
check("and the proceeds are below the principal", r.proceeds, "9919.18")

' At maturity there is no penalty -- the control, without which a library that
' always penalised would pass every case above.
matured = deposits.matures(cd)
m = deposits.redeem(cd, matured)
check("no penalty at maturity", m.penalty, usd("0.00"))
check("and the proceeds are principal plus interest", m.proceeds,
      usd("10000.00") + m.interest)

' ------------------------------------------------------------ refusals
on error goto next
x = deposits.account({ opened: opened, balance: usd("1.00"), rate: 0.01,
                       day_count: "actual/365", crediting: 30 })
check("an account without a balance method is refused", error.message,
      "deposits.account needs a balance_method")
error.clear()
x = deposits.account({ opened: opened, balance: usd("1.00"), rate: 0.01,
                       day_count: "actual/365", balance_method: "vibes",
                       crediting: 30 })
check("an unknown balance method is refused by name",
      contains(error.message, "none is the default"), true)
error.clear()
over = [{ on: mid, kind: "withdrawal", amount: usd("99999.00") }]
x = deposits.apply(mk("daily"), over, eom)
check("a withdrawal beyond the balance is refused",
      contains(error.message, "exceeds the balance"), true)
error.clear()
odd = [{ on: mid, kind: "transmute", amount: usd("1.00") }]
x = deposits.apply(mk("daily"), odd, eom)
check("an unknown event kind is refused by name",
      contains(error.message, "unknown event kind"), true)
error.clear()
on error stop

' THE CONTROL: the nearest legal neighbour of each refusal still works.
ok_acct = mk("average_daily")
check("a fully declared account is accepted", ok_acct.balance_method, "average_daily")
fine = [{ on: mid, kind: "withdrawal", amount: usd("100.00") }]
ok_state = deposits.apply(mk("daily"), fine, eom)
' The balance is the deposit less the withdrawal PLUS the interest credited --
' asserting a bare 9900 would be asserting that no interest was paid.
check("a withdrawal that fits is accepted", ok_state.balance,
      usd("10000.00") - usd("100.00") + ok_state.credited)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
