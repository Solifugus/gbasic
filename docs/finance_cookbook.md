# Finance cookbook

Worked recipes for `stdlib/finance.bas` — the time value of money, rate
conventions, cash-flow appraisal, schedules and day counts.

**This page cannot lie.** Every code block below is a real file under
`examples/finance_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_finance_cookbook.sh`. If the
library changes and a number moves, this page fails until it is regenerated.

Design and conventions: [finance_design.md](finance_design.md). The `money`
type these recipes are built on: [money_cookbook.md](money_cookbook.md).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/finance_cookbook/01_a_loan_payment.bas
```

---


## 1. What does this loan cost a month?

The five time-value functions all solve one equation for a different unknown,
and they take **Excel's argument order**: rate first, then the number of
periods, then the amount. `finance.pmt(0.005, 360, principal)` is
`PMT(0.5%, 360, 250000)` in a spreadsheet — which is where the answer will be
checked, so that is the order it is written in.

Two things the library will not guess for you. **Rates are per period**: a
6%/year loan paid monthly is `0.06 / 12`, and `finance.periodic` says so out
loud. **Signs follow the spreadsheet convention**: money received is positive,
money paid is negative, so a payment comes back negative because it leaves
you.

<!--CODE:01_a_loan_payment-->

```basic
' Recipe 1 — What does this loan cost a month?
'
' The five time-value functions all solve one equation for a different unknown,
' and they take EXCEL'S ARGUMENT ORDER: rate first, then the number of periods,
' then the amount. `finance.pmt(0.005, 360, principal)` is `PMT(0.5%, 360,
' 250000)` in a spreadsheet, which is where the answer will be checked.
program main()
  load finance

  principal {USD}= "250000.00"

  ' RATES ARE PER PERIOD. A 6%/year loan paid monthly is 0.06/12 — the library
  ' will not guess, because compounding conventions vary by product.
  monthly = finance.periodic(0.06, 12)

  payment = finance.pmt(monthly, 360, principal)
  print "250,000 at 6% nominal over 30 years"
  print "  monthly payment: " + string(payment)

  ' Negative because the payment LEAVES you. Signs follow the spreadsheet
  ' convention throughout: received is positive, paid is negative.
  print "  paid in total:   " + string(payment * 360)
  print "  interest:        " + string(payment * 360 + principal)
end program
```

<!--OUT:01_a_loan_payment-->

```
250,000 at 6% nominal over 30 years
  monthly payment: -1498.88
  paid in total:   -539595.47
  interest:        -289595.47
```

---


## 2. The same equation, five ways round

```
pv * (1+r)^n  +  pmt * ((1+r)^n - 1)/r  +  fv  =  0
```

Any four of those quantities give you the fifth, and there is a function for
each. That is the whole of time-value arithmetic; everything later in this page
is an application of it.

The round trip in the middle is worth copying as a habit: solve for the payment,
then solve for the *rate* given that payment, and check it comes back where you
started. It is how you test a solver you did not write.

<!--CODE:02_the_five_solvers-->

```basic
' Recipe 2 — The same equation, five ways round.
'
' pv * (1+r)^n  +  pmt * ((1+r)^n - 1)/r  +  fv  =  0
'
' Each function solves it for one unknown, so any four of the quantities give
' you the fifth. That is the whole of time-value arithmetic.
program main()
  load finance

  principal {USD}= "250000.00"
  monthly = finance.periodic(0.06, 12)

  ' Solve for the PAYMENT.
  payment = finance.pmt(monthly, 360, principal)
  print "payment for 250,000 over 360:   " + string(payment)

  ' Solve for the RATE, given that payment. It must come back to where we
  ' started — which is how you check a solver you did not write.
  recovered = finance.rate(360, payment, principal)
  print "rate recovered from it (x12):   " + string(round(recovered * 12, 6))

  ' Solve for the NUMBER OF PERIODS.
  print "periods at that payment:        " + string(round(finance.nper(monthly, payment, principal), 2))

  ' Solve for the PRESENT VALUE of that stream.
  print "present value of the payments:  " + string(finance.pv(monthly, 360, payment))

  ' Solve for the FUTURE VALUE. Saving 500 a month for ten years:
  saving {USD}= "-500.00"
  print ""
  print "500/month for 10 years at 6%:   " + string(finance.fv(monthly, 120, saving))
end program
```

<!--OUT:02_the_five_solvers-->

```
payment for 250,000 over 360:   -1498.88
rate recovered from it (x12):   0.06
periods at that payment:        360
present value of the payments:  250000.00

500/month for 10 years at 6%:   81939.67
```

---


## 3. Nominal, effective, periodic: three numbers, one loan

"12% compounded monthly" and "12% a year" are **different loans**. The first
earns 12.6825% over a year — 68 basis points more — and which one a quoted rate
means is a question the number itself does not answer.

- `finance.periodic(nominal, m)` — one period of a nominal annual rate.
- `finance.effective(nominal, m)` — what it actually comes to in a year. This
  is the **APY** of a quoted rate.
- `finance.nominal(effective, m)` — the exact inverse, for when a bank
  advertises the APY and you need the rate behind it.
- `finance.continuous(effective)` / `finance.from_continuous(r)` — for curve
  and option work, which is written in continuously compounded rates.

`effective` and `nominal` round-trip exactly, which is the property that makes
the pair trustworthy.

<!--CODE:03_which_rate-->

```basic
' Recipe 3 — Nominal, effective, periodic: three numbers, one loan.
'
' "12% compounded monthly" and "12% a year" are different loans. Getting this
' wrong is not a rounding difference — it is 68 basis points.
program main()
  load finance

  quoted = 0.12

  print "quoted (nominal, monthly):  " + string(quoted)
  print "  one month of it:          " + string(finance.periodic(quoted, 12))
  print "  what it actually earns:   " + string(round(finance.effective(quoted, 12), 6))
  print "  the difference:           " + string(round(finance.effective(quoted, 12) - quoted, 6))

  ' Compounding more often earns more, converging on the continuous rate.
  print ""
  print "the same 12% nominal, compounded"
  print "  yearly:     " + string(round(finance.effective(quoted, 1), 6))
  print "  quarterly:  " + string(round(finance.effective(quoted, 4), 6))
  print "  monthly:    " + string(round(finance.effective(quoted, 12), 6))
  print "  daily:      " + string(round(finance.effective(quoted, 365), 6))

  ' Going the other way: a bank advertises an APY. What nominal rate is that?
  print ""
  apy = 0.05
  print "an advertised APY of 5% is a nominal rate of"
  print "  " + string(round(finance.nominal(apy, 12), 6)) + " compounded monthly"

  ' And the exact inverse, which is how you know the pair is trustworthy.
  print "  back to APY: " + string(round(finance.effective(finance.nominal(apy, 12), 12), 6))
end program
```

<!--OUT:03_which_rate-->

```
quoted (nominal, monthly):  0.12
  one month of it:          0.01
  what it actually earns:   0.126825
  the difference:           0.006825

the same 12% nominal, compounded
  yearly:     0.12
  quarterly:  0.125509
  monthly:    0.126825
  daily:      0.127475

an advertised APY of 5% is a nominal rate of
  0.048889 compounded monthly
  back to APY: 0.05
```

---


## 4. Where the money actually goes

`finance.schedule(rate, nper, pv)` returns one record per period —
`period`, `payment`, `interest`, `principal`, `balance`.

**The final payment is adjusted so the balance lands exactly on zero.** Every
period is rounded to whole cents and those roundings accumulate; a schedule
that used one figure throughout would end owing a few cents or having overpaid.
Lenders do the same thing.

The check worth making is arithmetic, not visual: the principal parts must
reconstruct the loan **exactly**. A schedule that is off by a cent looks
perfectly reasonable on screen.

<!--CODE:04_amortization-->

```basic
' Recipe 4 — Where the money actually goes.
'
' `finance.schedule` returns one record per period: the payment, how much of it
' is interest, how much repays principal, and what is left owing.
program main()
  load finance

  loan {USD}= "10000.00"
  rows = finance.schedule(0.005, 12, loan)

  print "period  payment   interest  principal   balance"
  for each r in rows
    print (string(r.period) + "       " + string(r.payment) + "   " + string(r.interest)
           + "      " + string(r.principal) + "    " + string(r.balance))
  next

  ' THE LAST PAYMENT IS ADJUSTED so the balance lands exactly on zero. Every
  ' period is rounded to whole cents and those roundings accumulate; a schedule
  ' using one figure throughout would end owing a few cents. Lenders do this too.
  print ""
  print "final balance is exactly zero:   " + string(rows[11].balance = loan * 0)

  ' The parts must reconstruct the loan EXACTLY -- the check worth making,
  ' because a schedule that is off by a cent looks perfectly reasonable.
  total_principal = loan * 0
  total_interest = loan * 0
  for each r in rows
    total_principal = total_principal + r.principal
    total_interest = total_interest + r.interest
  next
  print "principal parts sum to the loan: " + string(total_principal = loan)
  print "total interest paid:             " + string(total_interest)
end program
```

<!--OUT:04_amortization-->

```
period  payment   interest  principal   balance
1       860.66   50.00      810.66    9189.34
2       860.66   45.95      814.72    8374.62
3       860.66   41.87      818.79    7555.83
4       860.66   37.78      822.89    6732.94
5       860.66   33.66      827.00    5905.94
6       860.66   29.53      831.13    5074.81
7       860.66   25.37      835.29    4239.52
8       860.66   21.20      839.47    3400.05
9       860.66   17.00      843.66    2556.39
10       860.66   12.78      847.88    1708.50
11       860.66   8.54      852.12    856.38
12       860.66   4.28      856.38    0.00

final balance is exactly zero:   true
principal parts sum to the loan: true
total interest paid:             327.97
```

---


## 5. Is this project worth doing?

NPV asks *what are these future flows worth today*; IRR asks *what rate would
make them break even*.

`finance.irr` takes the flows as **one array with the outlay first and
negative**, the way a spreadsheet lays them out in a column. `finance.npv`
discounts flows one period apart **starting at period 1**, so a period-0 outlay
is added separately — which is exactly how Excel's `NPV` behaves, quirk
included.

<!--CODE:05_is_it_worth_it-->

```basic
' Recipe 5 — Is this project worth doing?
'
' NPV asks "what are these future flows worth today"; IRR asks "what rate would
' make them break even". Both take the flows as ONE array with the outlay first
' and negative, the way a spreadsheet lays them out in a column.
program main()
  load finance

  ' A machine costing 50,000 that saves 15,000 a year for five years.
  spent {USD}= "-50000.00"
  saved {USD}= "15000.00"
  flows = [spent, saved, saved, saved, saved, saved]

  ' `npv` discounts flows one period apart starting at period 1, so the
  ' period-0 outlay is added separately -- Excel's NPV works the same way.
  later = [saved, saved, saved, saved, saved]
  print "savings are worth today: " + string(finance.npv(0.10, later))
  print "the machine costs:       " + string(spent * -1)
  print "net at 10%:              " + string(finance.npv(0.10, later) + spent)

  print ""
  print "internal rate of return: " + string(round(finance.irr(flows) * 100, 2)) + "%"

  ' Discounting harder makes future money worth less, which is the whole idea.
  print ""
  print "net at 5%:  " + string(finance.npv(0.05, later) + spent)
  print "net at 20%: " + string(finance.npv(0.20, later) + spent)

  ' Above your cost of capital the project earns its keep; below it, it does not.
  print ""
  print "worth doing at a 10% cost of capital: " + string(finance.irr(flows) > 0.10)
  print "worth doing at 20%:                   " + string(finance.irr(flows) > 0.20)
end program
```

<!--OUT:05_is_it_worth_it-->

```
savings are worth today: 56861.80
the machine costs:       50000.00
net at 10%:              6861.80

internal rate of return: 15.24%

net at 5%:  14942.15
net at 20%: -5140.82

worth doing at a 10% cost of capital: true
worth doing at 20%:                   false
```

---


## 6. Flows that fall on real dates

Projects do not pay on tidy period boundaries. `finance.xnpv` and
`finance.xirr` take a date per flow and discount **Actual/365 from the first
date**, which is Excel's definition rather than a choice this library gets to
make.

The last line of the recipe is the point: treating the same flows as evenly
spaced gives 11.54% instead of 37.34%. They are not evenly spaced, and that gap
is why the dated form exists.

`xnpv` at the `xirr` is zero by definition — a relationship no reference can
drift out from under, and a good self-check.

<!--CODE:06_real_dates-->

```basic
' Recipe 6 — Flows that fall on real dates.
'
' Projects do not pay on tidy period boundaries. `xnpv` and `xirr` take a date
' per flow and discount Actual/365 from the first one, which is Excel's
' definition -- so the answers match the spreadsheet they will be checked in.
program main()
  load finance

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

  flows = [f0, f1, f2, f3, f4]
  when = [t0, t1, t2, t3, t4]

  print "xirr:          " + string(round(finance.xirr(flows, when) * 100, 4)) + "%"
  print "xnpv at 9%:    " + string(finance.xnpv(0.09, flows, when))

  ' The defining relationship: discounted at its own IRR, a project is worth
  ' nothing. Checking this is how you know the two agree.
  at_root = finance.xnpv(finance.xirr(flows, when), flows, when)
  print "xnpv at xirr:  " + string(at_root) + "   <- zero, by definition"

  ' The same flows treated as EVEN periods give a different answer, because
  ' they are not evenly spaced. That gap is why the dated form exists.
  print ""
  print "as if evenly spaced: " + string(round(finance.irr(flows) * 100, 4)) + "%"
end program
```

<!--OUT:06_real_dates-->

```
xirr:          37.3363%
xnpv at 9%:    2086.65
xnpv at xirr:  0.00   <- zero, by definition

as if evenly spaced: 11.5413%
```

---


## 7. How much of a year is that?

Every accrual rests on this, and the conventions **disagree**. There is no
default convention: you name the one your contract uses.

`finance.year_fraction(from, to, convention)` takes `"actual/360"`,
`"actual/365"`, `"actual/actual"` (ISDA — each day weighted by the length of
its own year) or `"30/360"` (US bond basis, including the end-of-February
rules).

The spread is not academic. On a million dollars at 5% over the same two dates,
Actual/360 and 30/360 differ by $138.89.

<!--CODE:07_day_counts-->

```basic
' Recipe 7 — How much of a year is that?
'
' Every accrual rests on this, and the conventions DISAGREE. There is no
' default: you name the one your contract uses, because none is dominant and a
' guess would be wrong somewhere without saying so.
program main()
  load finance

  start {date}= "2026-01-31"
  finish {date}= "2026-03-31"

  print "31 Jan 2026 to 31 Mar 2026 is 59 actual days"
  print "  actual/360:     " + string(round(finance.year_fraction(start, finish, "actual/360"), 8))
  print "  actual/365:     " + string(round(finance.year_fraction(start, finish, "actual/365"), 8))
  print "  actual/actual:  " + string(round(finance.year_fraction(start, finish, "actual/actual"), 8))
  print "  30/360:         " + string(round(finance.year_fraction(start, finish, "30/360"), 8))

  ' On a million dollars at 5%, that spread is real money.
  balance {USD}= "1000000.00"
  print ""
  print "interest at 5% on 1,000,000 for that period"
  print "  actual/360:     " + string(balance * (0.05 * finance.year_fraction(start, finish, "actual/360")))
  print "  30/360:         " + string(balance * (0.05 * finance.year_fraction(start, finish, "30/360")))

  ' actual/actual weights each day by the length of ITS OWN year, so a leap
  ' year is exactly one year and a span across several is split at the
  ' boundaries.
  ly {date}= "2024-01-01"
  ly2 {date}= "2025-01-01"
  print ""
  print "2024 (a leap year) under actual/actual: " + string(finance.year_fraction(ly, ly2, "actual/actual"))
  print "  the same span under actual/365:      " + string(round(finance.year_fraction(ly, ly2, "actual/365"), 8))
end program
```

<!--OUT:07_day_counts-->

```
31 Jan 2026 to 31 Mar 2026 is 59 actual days
  actual/360:     0.16388889
  actual/365:     0.16164384
  actual/actual:  0.16164384
  30/360:         0.16666667

interest at 5% on 1,000,000 for that period
  actual/360:     8194.44
  30/360:         8333.33

2024 (a leap year) under actual/actual: 1
  the same span under actual/365:      1.00273973
```

---


## 8. When there is more than one answer, and when there is none

A rate solver returns a plausible percentage whatever you feed it. These are
the two cases where *a number came back* is not the same as *that is the
answer*.

**More than one sign change admits more than one rate** (Descartes' rule). The
recipe's flows are satisfied exactly by **both** 100% and 200% — the page proves
it by discounting at each and getting zero. `irr` and `xirr` **warn** rather
than refuse, because the root returned is real and usually the one wanted;
`on warning ignore` is the opt-out once you have thought about it.

**Flows with no rate at all are an error, not a number.** All-positive flows
never break even, and saying so is better than returning something.

<!--CODE:08_when_the_answer_is_not_alone-->

```basic
' Recipe 8 — When there is more than one answer, and when there is none.
'
' A rate solver returns a plausible percentage whatever you feed it. These are
' the two cases where "a number came back" is not the same as "that is the
' answer", and gBASIC says so rather than letting you find out later.
program main()
  load finance

  ' A conventional project -- pay once, receive after -- changes sign ONCE and
  ' has exactly one rate. Silent, as it should be.
  a {USD}= "-1000.00"
  b {USD}= "600.00"
  print "conventional project: " + string(round(finance.irr([a, b, b]) * 100, 2)) + "%"

  ' This one pays out again at the end -- a decommissioning cost, a
  ' reinvestment. It changes sign TWICE, and Descartes says that admits two
  ' rates. Both 100% and 200% satisfy it exactly.
  print ""
  on warning goto next
  m0 {USD}= "-1000.00"
  m1 {USD}= "5000.00"
  m2 {USD}= "-6000.00"
  r = finance.irr([m0, m1, m2])
  w = warning
  print "two sign changes: a rate comes back (" + string(round(r * 100, 2)) + "%)"
  print "and gBASIC says so:"
  print "  " + w.message
  on warning print

  ' Check it: the OTHER root satisfies the equation just as exactly.
  print ""
  print "npv at 100%: " + string(finance.npv(1.0, [m1, m2]) + m0)
  print "npv at 200%: " + string(finance.npv(2.0, [m1, m2]) + m0)

  ' And the case with no answer at all is an error, not a number.
  print ""
  on error goto next
  never = finance.irr([b, b])
  if error then
    print "all-positive flows: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:08_when_the_answer_is_not_alone-->

```
conventional project: 13.07%

two sign changes: a rate comes back (100%)
and gBASIC says so:
  finance.irr: these flows change sign 2 times, so more than one rate can satisfy them -- the one returned is a root, not necessarily the only one

npv at 100%: 0.00
npv at 200%: 0.00

all-positive flows: finance.irr: no rate between -100% and 1000% makes these flows break even
```

---


## 9. Payments at the start, and money still owed at the end

Every solver takes two more arguments you can ignore until you need them:
`fv`, a balance still outstanding when the term ends, and `timing`, which is
`"end"` (the default) or `"begin"`.

Leases are paid in advance, so every payment earns one extra period of interest
and the payment is smaller. Car loans often end in a balloon, which leaves less
principal to amortize. And saving *towards* a target is the same equation read
the other way — a present value of zero and a future value of what you want.

The last line pins the property that makes the short form safe: omitting the
tail is exactly the same as supplying its defaults.

<!--CODE:09_leases_and_balloons-->

```basic
' Recipe 9 — Payments at the start, and money still owed at the end.
'
' Every solver takes two more arguments you can ignore until you need them:
' `fv`, a balance still outstanding when the term ends, and `timing`, which is
' "end" (the default) or "begin". Leases are paid in advance; car loans often
' end in a balloon.
program main()
  load finance

  monthly = finance.periodic(0.06, 12)
  principal {USD}= "250000.00"

  ' A lease is paid at the START of each period, so every payment earns one
  ' extra period of interest and the payment is smaller.
  at_end = finance.pmt(monthly, 360, principal)
  at_begin = finance.pmt(monthly, 360, principal, 0, "begin")
  print "paid in arrears: " + string(at_end)
  print "paid in advance: " + string(at_begin)
  print "  smaller by:    " + string(at_end - at_begin)

  ' A balloon: 50,000 still owed at the end of the term. Less principal is
  ' amortized, so the monthly payment falls.
  balloon {USD}= "-50000.00"
  print ""
  print "with 50,000 owing at the end: " + string(finance.pmt(monthly, 360, principal, balloon))
  print "  versus fully amortized:     " + string(at_end)

  ' Saving TOWARDS a target is the same equation read the other way: how much
  ' must go in each month to reach 100,000 in fifteen years?
  target {USD}= "100000.00"
  nothing_now {USD}= "0.00"
  print ""
  print "to reach 100,000 in 15 years at 6%:"
  print "  save each month: " + string(finance.pmt(monthly, 180, nothing_now, target))

  ' And omitting the tail must equal supplying its defaults -- the property
  ' that makes the short form safe to use everywhere.
  print ""
  print "short form equals the full one: " + string(finance.pmt(monthly, 360, principal) = finance.pmt(monthly, 360, principal, 0, "end"))
end program
```

<!--OUT:09_leases_and_balloons-->

```
paid in arrears: -1498.88
paid in advance: -1491.42
  smaller by:    -7.46

with 50,000 owing at the end: -1449.10
  versus fully amortized:     -1498.88

to reach 100,000 in 15 years at 6%:
  save each month: -343.86

short form equals the full one: true
```

---
