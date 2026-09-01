' lending.bas -- loans, servicing and payoff (docs/lending_design.md).
'
' SELF-CHECKING. Every defect this library exists to prevent produces an
' ORDINARY-LOOKING BALANCE: an accrual basis that does not distinguish itself,
' a waterfall applied in the wrong order, interest that never accrues. All
' three read as perfectly good money, so a golden would record them as expected
' and defend them. Two of the three happened while writing it.

load lending
load accounting

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

function two(n)
    if n < 10 then
        return "0" + string(n)
    end if
    return string(n)
end function

function usd(t)
    m {USD}= t
    return m
end function

opened {date}= "2026-01-01"

function mk(basis, waterfall)
    return lending.loan({ principal: usd("10000.00"), rate: 0.12, term: 12,
                          opened: opened, basis: basis, waterfall: waterfall,
                          day_count: "actual/365" })
end function

' ----------------------------------------------------- schedule
' Verified against finance, whose own figures were checked outside gBASIC.
l = mk("amortized", "fees_interest_principal")
check("the scheduled payment", lending.payment(l), "888.49")
rows = lending.schedule(l)
check("one row per period", count(rows), 12)
check("the final balance is exactly zero", rows[11].balance, usd("0.00"))
' The invariant the whole library rests on, asserted arithmetically.
total_principal = usd("0.00")
for each r in rows
    total_principal = total_principal + r.principal
next
check("principal parts sum to the loan exactly", total_principal, usd("10000.00"))

' ------------------------------------------- ACCRUAL BASIS IS A DIFFERENCE
' The two bases must give DIFFERENT answers, and the difference must be the
' days. A test that cannot tell them apart is not testing the basis -- and the
' first implementation prorated by days, which made them algebraically
' identical and the declaration decorative.
feb {date}= "2026-02-01"
one_payment = [{ on: feb, kind: "payment", amount: usd("888.49") }]
amort = lending.apply(mk("amortized", "fees_interest_principal"), one_payment, false)
daily = lending.apply(mk("daily_simple", "fees_interest_principal"), one_payment, false)
check("amortized accrues one period, whatever the days", amort.paid_interest, "100.00")
check("daily simple accrues the actual 31 days", daily.paid_interest, "101.92")
check("so the two bases differ", amort.balance != daily.balance, true)
check("and the difference is the extra days beyond a period",
      daily.paid_interest - amort.paid_interest, "1.92")

' ------------------------------------------- WATERFALL IS A DIFFERENCE TOO
' The same partial payment under two orders must land differently, asserted
' PER COMPONENT rather than by total -- the totals are equal by construction.
mid {date}= "2026-01-15"
partial = [{ on: mid, kind: "fee", amount: usd("50.00") },
           { on: feb, kind: "payment", amount: usd("120.00") }]
fip = lending.apply(mk("amortized", "fees_interest_principal"), partial, false)
ipf = lending.apply(mk("amortized", "interest_principal_fees"), partial, false)
check("fees first: the fee is cleared", fip.paid_fees, "50.00")
check("  and what is left goes to interest", fip.paid_interest, "70.00")
check("  leaving interest still owed", fip.accrued, "30.00")
check("interest first: interest is cleared", ipf.paid_interest, "100.00")
check("  the remainder reaches principal", ipf.paid_principal, "20.00")
check("  and the fee is still owed", ipf.fees_due, "50.00")

' Interest must accrue even when no single gap spans a period -- the events
' above are 14 and 17 days apart, and counting periods WITHIN a gap made both
' round to zero, so a weekly-payment loan accrued nothing at all.
check("interest accrues across short gaps", fip.paid_interest + fip.accrued, "100.00")

' ------------------------------------------------------------ payoff
mar {date}= "2026-03-01"
quote = lending.payoff(mk("daily_simple", "fees_interest_principal"), one_payment, mar)
check("a payoff quote names the principal", quote.principal, "9213.43")
check("and carries a per-diem", quote.per_diem > usd("0.00"), true)
check("with a total that is the parts", quote.total,
      quote.principal + quote.interest + quote.fees)

' ------------------------------------------------------- underwriting
check("ltv", round(lending.ltv(usd("240000.00"), usd("300000.00")), 4), 0.8)
check("dti", round(lending.dti(usd("2000.00"), usd("6000.00")), 4), 0.3333)
check("dscr is oriented the other way: above 1 is healthy",
      lending.dscr(usd("130000.00"), usd("100000.00")) > 1, true)
' A MISSING INPUT IS `unknown`, NOT ZERO. These feed credit decisions, and an
' absent income that became zero would make every ratio look perfect.
check("a missing income gives unknown, not a ratio",
      is_unknown(lending.dti(usd("2000.00"), nothing)), true)

' --------------------------------------------- THE LEDGER PROVES THE REST
' Design §5: a loan's whole life posted to a real ledger must balance, with
' receivables equal to the outstanding balance. An unbalanced entry or a
' phantom account is refused where it is posted, so a loan that posts cleanly
' has demonstrated its arithmetic rather than asserted it.
books = accounting.chart([
  { code: "1200", name: "Loans receivable", kind: "asset" },
  { code: "1000", name: "Cash",             kind: "asset" },
  { code: "4100", name: "Interest income",  kind: "revenue" },
  { code: "4200", name: "Fee income",       kind: "revenue" }
])
acc = { }
acc["receivable"] = "1200"
acc["cash"] = "1000"
acc["interest_income"] = "4100"
acc["fee_income"] = "4200"

year = []
for m = 2 to 12
    d {date}= "2026-" + two(m) + "-01"
    append(year, { on: d, kind: "payment", amount: usd("888.49") })
next
serviced = lending.apply(l, year, false)
es = lending.entries(books, l, year, acc)
lg = accounting.ledger()
for each e in es
    lg = accounting.post(lg, e)
next
bs = accounting.balance_sheet(books, lg, nothing)
bal = accounting.balances(lg, nothing)
check("every entry posted", count(es), 12)
check("the ledger balances", bs.balanced, true)
check("receivables equal the servicing balance", bal["1200"], serviced.balance)
check("and interest income equals interest collected", bal["4100"] * -1, serviced.paid_interest)

' ------------------------------------------------------------ refusals
on error goto next
x = lending.loan({ principal: usd("100.00"), rate: 0.1, term: 12, opened: opened,
                   waterfall: "fees_interest_principal", day_count: "actual/365" })
check("a loan without a basis is refused", error.message, "lending.loan needs a basis")
error.clear()
x = lending.loan({ principal: usd("100.00"), rate: 0.1, term: 12, opened: opened,
                   basis: "amortized", waterfall: "fees_interest_principal",
                   day_count: "actual/999" })
check("an unknown day count is refused by finance",
      contains(error.message, "unknown convention"), true)
error.clear()
big = [{ on: feb, kind: "payment", amount: usd("99999.00") }]
x = lending.apply(mk("amortized", "fees_interest_principal"), big, false)
check("an overpayment is refused, not a negative balance",
      contains(error.message, "an overpayment is a refund"), true)
error.clear()
back = [{ on: feb, kind: "payment", amount: usd("100.00") },
        { on: mid, kind: "payment", amount: usd("100.00") }]
x = lending.apply(mk("amortized", "fees_interest_principal"), back, false)
check("out-of-order events are refused",
      contains(error.message, "events must be in order"), true)
error.clear()
odd = [{ on: feb, kind: "sneeze", amount: usd("1.00") }]
x = lending.apply(mk("amortized", "fees_interest_principal"), odd, false)
check("an unknown event kind is refused by name",
      contains(error.message, "unknown event kind"), true)
error.clear()
on error stop

' THE CONTROL: the nearest legal neighbour of each refusal still works.
ok_loan = mk("daily_simple", "interest_principal_fees")
check("a fully declared loan is accepted", ok_loan.basis, "daily_simple")
exact = lending.apply(l, one_payment, false)
check("a payment that fits is accepted", exact.paid_principal, "788.49")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
