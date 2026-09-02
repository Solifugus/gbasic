# Deposits cookbook

Worked recipes for `stdlib/deposits.bas` — deposit interest, crediting and
certificates over exact `money`.

**This page cannot lie.** Every code block below is a real file under
`examples/deposits_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_deposits_cookbook.sh`.

Design and rationale: [lending_design.md](lending_design.md) §7. The simple
interest underneath: [finance_cookbook.md](finance_cookbook.md). The mirror
library for the other side of the balance sheet:
[lending_cookbook.md](lending_cookbook.md).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/deposits_cookbook/01_a_savings_account.bas
```

---

## Why this is not part of `lending`

`waterfall`, `delinquency` and per-diem mean nothing to a savings account.
`tier`, crediting schedule and early-withdrawal penalty mean nothing to a
mortgage. What the two genuinely share is *simple interest over a day count*,
and that lives in `finance` where both can reach it — a deposit does not
borrow.

What they do share is a rule: **the product declares its conventions and the
library never assumes.** For a loan that is the accrual basis; for a deposit it
is the balance method.

---

## 1. A savings account, and the difference between compounding and crediting

They are not the same thing, and conflating them is the common error.

Interest is **computed** over a crediting period and **added** to the balance
at the end of it — so the next period earns on the larger balance. That
addition is the compounding. `crediting` is simply how many days a period is.

Interest earned since the last crediting date is reported **separately**, in
`accrued`. The holder has earned it and has not been paid it, and rolling it
into the balance would say otherwise.

<!--CODE:01_a_savings_account-->

```basic
' Recipe 1 — A savings account, and the difference between compounding
' and crediting.
'
' They are not the same thing, and conflating them is the common error.
' Interest is COMPUTED over a crediting period and ADDED to the balance at the
' end of it — so the next period earns on the larger balance. That addition is
' the compounding. `crediting` is simply how many days a period is.
'
' Interest earned since the last crediting date is reported SEPARATELY, in
' `accrued`. The holder has earned it and has not been paid it, and rolling it
' into the balance would say otherwise.
program main()
  load deposits

  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"

  acct = deposits.account({ opened: opened, balance: balance, rate: 0.05,
                            day_count: "actual/365",
                            balance_method: "daily",
                            crediting: 30 })

  ' No deposits or withdrawals — just the account, left alone for a year.
  year_end {date}= "2026-12-31"
  st = deposits.apply(acct, [], year_end)

  print "opening balance:     " + string(acct.balance)
  print "credited over 2026:  " + string(st.credited)
  print "closing balance:     " + string(st.balance)
  print ""
  print "last crediting date: " + ymd(st.last_credited)
  print "  earned since then, not yet paid: " + string(st.accrued)
  print "available today:     " + string(st.available)
  print "  which is balance + accrued: " + string(st.available = st.balance + st.accrued)

  ' THAT is the compounding, and here is the proof. Simple interest on 10,000
  ' at 5% for a year is 500. Twelve 30-day periods, each credited, come to
  ' more — and the excess is what the earlier periods' interest itself earned.
  simple {USD}= "500.00"
  print ""
  print "simple interest would be:  " + string(simple)
  print "credited interest was:     " + string(st.credited)
  print "compounding is worth:      " + string(st.credited - simple)

  ' Deposits and withdrawals are events, in date order.
  mar {date}= "2026-03-01"
  sep {date}= "2026-09-01"
  paycheck {USD}= "2500.00"
  rent {USD}= "1800.00"
  active = deposits.apply(acct, [{ on: mar, kind: "deposit",    amount: paycheck },
                                 { on: sep, kind: "withdrawal", amount: rent }], year_end)
  print ""
  print "with a 2,500 deposit in March and an 1,800 withdrawal in September:"
  print "  closing balance:  " + string(active.balance)
  print "  interest credited " + string(active.credited)
  print "  more than idle:   " + string(active.credited > st.credited)
end program

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
```

<!--OUT:01_a_savings_account-->

```
opening balance:     10000.00
credited over 2026:  504.45
closing balance:     10504.45

last crediting date: 2026-12-27
  earned since then, not yet paid: 5.76
available today:     10510.21
  which is balance + accrued: true

simple interest would be:  500.00
credited interest was:     504.45
compounding is worth:      4.45

with a 2,500 deposit in March and an 1,800 withdrawal in September:
  closing balance:  11280.45
  interest credited 580.45
  more than idle:   true
```

---

## 2. Which balance earns? Three answers, and banks name theirs

| method | what earns |
|---|---|
| `daily` | every day earns on its own balance |
| `average_daily` | the mean balance earns for the whole period |
| `minimum` | the **lowest** balance the account ever held earns for the whole period |

Same money, same activity, different interest — 28.77 against 4.11 on the
activity below. That is why the account declares its method.

And then a pair that does **not** differ, asserted as equal and explained.
Simple interest is *linear* in the balance, so the mean balance earns exactly
what each day's own balance earns: `daily` and `average_daily` agree whenever
the rate is constant, and part company only under tiers. Asserting a difference
there would be asserting something false — which matters, because the
neighbouring recipe exists to catch exactly the opposite mistake.

<!--CODE:02_the_balance_method-->

```basic
' Recipe 2 — Which balance earns? Three answers, and banks name theirs.
'
'   daily          every day earns on its own balance
'   average_daily  the mean balance earns for the whole period
'   minimum        the LOWEST balance the account ever held earns for the
'                  whole period
'
' Same money, same activity, different interest. That is why the account
' declares its method and this library never picks one for you.
program main()
  load deposits

  opened {date}= "2026-01-01"
  eom {date}= "2026-01-31"
  mid {date}= "2026-01-21"
  balance {USD}= "10000.00"
  big {USD}= "9000.00"

  daily   = acct("daily")
  average = acct("average_daily")
  lowest  = acct("minimum")

  ' A large withdrawal LATE in the period: 10,000 for twenty days, then 1,000
  ' for ten. The three methods see three different accounts.
  events = [{ on: mid, kind: "withdrawal", amount: big }]

  d = deposits.apply(daily, events, eom)
  a = deposits.apply(average, events, eom)
  n = deposits.apply(lowest, events, eom)

  print "10,000 held for 20 days, then 9,000 withdrawn:"
  print "  daily          " + string(d.credited)
  print "  average_daily  " + string(a.credited)
  print "  minimum        " + string(n.credited)
  print ""
  print ("minimum pays "
         + string(round(number(string(d.credited)) / number(string(n.credited)), 1))
         + " times less on identical activity.")

  ' AND A PAIR THAT DOES NOT DIFFER — asserted as EQUAL, and explained,
  ' because asserting a difference here would assert something false. Simple
  ' interest is LINEAR in the balance, so the mean balance earns exactly what
  ' each day's own balance earns. `daily` and `average_daily` agree whenever
  ' the rate is constant, and part company only under tiers (recipe 3).
  print ""
  print "daily and average_daily are equal here: " + string(d.credited = a.credited)
  print "  not a coincidence — simple interest is linear in the balance."

  ' And the control: with no activity, all three agree, because there is only
  ' ever one balance to disagree about.
  q = deposits.apply(daily, [], eom)
  r = deposits.apply(lowest, [], eom)
  print ""
  print "with no activity at all, daily and minimum agree: " + string(q.credited = r.credited)
end program

function acct(method)
  load deposits
  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"
  return deposits.account({ opened: opened, balance: balance, rate: 0.05,
                            day_count: "actual/365", balance_method: method,
                            crediting: 30 })
end function
```

<!--OUT:02_the_balance_method-->

```
10,000 held for 20 days, then 9,000 withdrawn:
  daily          28.77
  average_daily  28.77
  minimum        4.11

minimum pays 7 times less on identical activity.

daily and average_daily are equal here: true
  not a coincidence — simple interest is linear in the balance.

with no activity at all, daily and minimum agree: true
```

---

## 3. Tiered rates: whole-balance or bracket?

A tier table is `{ from, rate }`, lowest first. What a balance earns depends on
which of two products you are selling:

- **`whole`** — the whole balance earns the tier it lands in.
- **`portion`** — each slice earns its own tier, like a tax bracket.

They are far apart at a boundary: 3,000.00 against 1,800.00 on the same 60,000
below. A deposit product using the wrong one pays a perfectly plausible amount
of the wrong interest, which is why the mode is declared rather than assumed.

`tiered_rate` **refuses** the portion mode outright. There is no single rate a
bracketed balance earns, and returning the top tier's would be a number that
reads like an answer. `tiered_interest` computes the amount instead.

<!--CODE:03_tiered_rates-->

```basic
' Recipe 3 — Tiered rates: whole-balance or bracket?
'
' A tier table is { from, rate }, lowest first. What a balance earns depends on
' which of two products you are selling, and they are far apart at a boundary:
'
'   "whole"    the whole balance earns the tier it LANDS IN
'   "portion"  each slice earns its own tier, like a tax bracket
'
' A deposit product using the wrong one pays a perfectly plausible amount of
' the wrong interest, which is why the mode is declared rather than assumed.
program main()
  load deposits
  load finance

  base {USD}= "0.00"
  mid {USD}= "10000.00"
  top {USD}= "50000.00"

  tiers = [{ from: base, rate: 0.01 },
           { from: mid,  rate: 0.03 },
           { from: top,  rate: 0.05 }]

  small {USD}= "500.00"
  large {USD}= "60000.00"

  print "whole-balance mode — the balance finds its tier:"
  print "     500 earns " + string(deposits.tiered_rate(tiers, small, "whole"))
  print "  60,000 earns " + string(deposits.tiered_rate(tiers, large, "whole"))

  opened {date}= "2026-01-01"
  one_year {date}= "2027-01-01"

  whole_rate = deposits.tiered_rate(tiers, large, "whole")
  whole_amt = finance.accrue(large, whole_rate, opened, one_year, "actual/365")
  portioned = deposits.tiered_interest(tiers, large, opened, one_year, "actual/365")

  print ""
  print "a year on 60,000:"
  print "  whole    " + string(whole_amt) + "   (all of it at 5%)"
  print "  portion  " + string(portioned) + "   (10,000 at 1%, 40,000 at 3%, 10,000 at 5%)"
  print "  the gap  " + string(whole_amt - portioned)

  ' Check the bracket arithmetic against its own definition rather than
  ' trusting the total: each slice, priced at its own tier, summed by hand.
  slice_a {USD}= "10000.00"
  slice_b {USD}= "40000.00"
  slice_c {USD}= "10000.00"
  by_hand = (finance.accrue(slice_a, 0.01, opened, one_year, "actual/365")
             + finance.accrue(slice_b, 0.03, opened, one_year, "actual/365")
             + finance.accrue(slice_c, 0.05, opened, one_year, "actual/365"))
  print ""
  print "the slices, priced by hand: " + string(by_hand)
  print "matches tiered_interest:    " + string(by_hand = portioned)

  ' `tiered_rate` refuses the portion mode outright: there IS no single rate a
  ' bracketed balance earns, and returning the top tier's would be a number
  ' that reads like an answer.
  on error goto next
  r = deposits.tiered_rate(tiers, large, "portion")
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:03_tiered_rates-->

```
whole-balance mode — the balance finds its tier:
     500 earns 0.01
  60,000 earns 0.05

a year on 60,000:
  whole    3000.00   (all of it at 5%)
  portion  1800.00   (10,000 at 1%, 40,000 at 3%, 10,000 at 5%)
  the gap  1200.00

the slices, priced by hand: 1800.00
matches tiered_interest:    true

refused: deposits.tiered_rate: only the whole-balance mode returns a single rate; use deposits.tiered_interest for portions
```

---

## 4. Certificates, and the penalty that eats principal

A certificate is an account with a term and an early-withdrawal penalty
measured in days of interest. The penalty is computed on the **principal**, for
its own number of days, regardless of how long the money has actually been
there.

So redeeming a one-year certificate after a month returns *less than was put
in*. The library does not clamp that at zero. Clamping would report proceeds
the holder is not going to receive: a plausible number, and the wrong one.
`principal_reduced` says when it happened.

The maturity case is the **control**. Without it, a library that always
penalised would satisfy every check above.

<!--CODE:04_certificates-->

```basic
' Recipe 4 — Certificates, and the penalty that eats principal.
'
' A certificate is an account with a term and an early-withdrawal penalty
' measured in days of interest. The penalty is computed on the PRINCIPAL, for
' its own number of days, regardless of how long the money has actually been
' there — which is why redeeming a one-year certificate after a month can
' return LESS than was put in.
'
' The library does not clamp that at zero. Clamping would report proceeds the
' holder is not going to receive: a plausible number, and the wrong one.
program main()
  load deposits

  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"

  cd = deposits.certificate({ opened: opened, balance: balance, rate: 0.05,
                              day_count: "actual/365",
                              balance_method: "daily", crediting: 30,
                              term_days: 365, penalty_days: 90 })

  print "a 365-day certificate with a 90-day penalty"
  print "  matures on " + ymd(deposits.matures(cd))

  ' Redeemed after one month. Thirty-one days of interest earned; ninety days
  ' of interest charged.
  early {date}= "2026-02-01"
  r = deposits.redeem(cd, early)

  print ""
  print "redeemed early, on 2026-02-01:"
  print "  principal          " + string(r.principal)
  print "  interest earned    " + string(r.interest) + "   (31 days)"
  print "  penalty            " + string(r.penalty) + "  (90 days)"
  print "  proceeds           " + string(r.proceeds)
  print ""
  print "  was it early?              " + string(r.early)
  print "  did the penalty exceed the interest? " + string(r.principal_reduced)
  print "  so the holder gets back less than they put in: "
  print "    " + string(r.proceeds < r.principal)

  ' THE CONTROL. Without a maturity case, a library that always penalised
  ' would satisfy everything above.
  m = deposits.redeem(cd, deposits.matures(cd))
  print ""
  print "redeemed at maturity:"
  print "  interest earned    " + string(m.interest)
  print "  penalty            " + string(m.penalty)
  print "  proceeds           " + string(m.proceeds)
  print "  was it early?      " + string(m.early)
  print "  proceeds are principal plus interest: "
  print "    " + string(m.proceeds = m.principal + m.interest)

  ' Somewhere between the two the penalty stops biting. Past 90 days of
  ' earnings, an early redemption still returns more than the principal — it
  ' just returns less than waiting would have.
  late {date}= "2026-10-01"
  l = deposits.redeem(cd, late)
  print ""
  print "redeemed early but late in the term, on 2026-10-01:"
  print "  interest earned    " + string(l.interest)
  print "  penalty            " + string(l.penalty)
  print "  proceeds           " + string(l.proceeds)
  print "  principal intact?  " + string(l.proceeds > l.principal)
  print "  but worse than waiting: " + string(l.proceeds < m.proceeds)
end program

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
```

<!--OUT:04_certificates-->

```
a 365-day certificate with a 90-day penalty
  matures on 2027-01-01

redeemed early, on 2026-02-01:
  principal          10000.00
  interest earned    42.47   (31 days)
  penalty            123.29  (90 days)
  proceeds           9919.18

  was it early?              true
  did the penalty exceed the interest? true
  so the holder gets back less than they put in: 
    true

redeemed at maturity:
  interest earned    500.00
  penalty            0.00
  proceeds           10500.00
  was it early?      false
  proceeds are principal plus interest: 
    true

redeemed early but late in the term, on 2026-10-01:
  interest earned    373.97
  penalty            123.29
  proceeds           10250.68
  principal intact?  true
  but worse than waiting: true
```

---

## What is not here

Escrow accounts and the reserve analysis that goes with them are named in
[lending_design.md](lending_design.md) §8 and are not built. Regulation-DD
disclosure figures (APY and its rounding rules) are policy rather than
arithmetic and are deliberately outside the library.
