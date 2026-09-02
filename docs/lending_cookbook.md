# Lending cookbook

Worked recipes for `stdlib/lending.bas` — loans, servicing and payoff over
exact `money`.

**This page cannot lie.** Every code block below is a real file under
`examples/lending_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_lending_cookbook.sh`.

Design and rationale: [lending_design.md](lending_design.md). The time-value
arithmetic underneath: [finance_cookbook.md](finance_cookbook.md). Where the
entries go: [accounting_cookbook.md](accounting_cookbook.md). Portfolio-level
questions: [credit_cookbook.md](credit_cookbook.md).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/lending_cookbook/01_a_loan.bas
```

---

## 1. A loan is a set of conventions, not a number

`finance` answers what the payment is. `lending` answers **what happens next** —
and what happens next is not a formula. A borrower pays late, partly, or extra;
a rate changes; the loan is paid off on the 14th. Each of those moves the
balance, and how it moves it depends on three choices no formula settles:

| choice | what it decides |
|---|---|
| `basis` | how interest accrues between events |
| `waterfall` | what a short payment pays off first |
| `day_count` | how days become a fraction of a year |

The loan **declares** all three. A missing one is refused, not defaulted —
amortized and daily-simple accrual are different loans, and a guess is wrong by
a few dollars a month, compounding, with nothing on screen to say so.

<!--CODE:01_a_loan-->

```basic
' Recipe 1 — A loan is a set of conventions, not a number.
'
' `finance` answers what the payment is. `lending` answers what happens next —
' and what happens next depends on three choices that no formula settles:
' how interest accrues, what order a payment is applied in, and how days are
' counted. Each changes the balance. None has a defensible default, so the
' loan DECLARES all three and the library never assumes.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12, opened: opened,
                     basis:     "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  ' The rate on the loan is ANNUAL. The rate it accrues at is per period, and
  ' the loan's own `periods_per_year` (12 unless you say otherwise) converts it.
  print "annual rate:       " + string(l.rate)
  print "periods per year:  " + string(l.periods_per_year)
  print "period rate:       " + string(lending.period_rate(l))
  print "scheduled payment: " + string(lending.payment(l))

  ' The schedule is `finance.schedule` in the loan's own terms. Its last
  ' payment is adjusted so the balance lands exactly on zero.
  rows = lending.schedule(l)
  print ""
  print "first payment:  interest " + string(rows[0].interest)
  print "                principal " + string(rows[0].principal)
  print "last payment:   interest " + string(rows[11].interest)
  print "                principal " + string(rows[11].principal)
  print "final balance:  " + string(rows[11].balance)

  ' A MISSING CONVENTION IS REFUSED, not filled in. Amortized and daily-simple
  ' accrual are different loans; guessing is wrong by a few dollars a month,
  ' compounding, with nothing on screen to say so.
  on error goto next
  guess = lending.loan({ principal: amount, rate: 0.12, term: 12, opened: opened,
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:01_a_loan-->

```
annual rate:       0.12
periods per year:  12
period rate:       0.01
scheduled payment: 888.49

first payment:  interest 100.00
                principal 788.49
last payment:   interest 8.80
                principal 879.69
final balance:  0.00

refused: lending.loan needs a basis
```

---

## 2. Servicing: what actually happened

`lending.apply` is a **fold** over the event list, not an incremental step. The
state is a pure function of the loan and everything that has happened to it, so
*why is this balance what it is* is answerable by replaying the record. Stored
incremental state makes the number itself the answer, and if it is ever wrong
there is nothing left to reconstruct it from.

An event is `{ on: date, kind: "payment"|"fee"|"rate_change", amount: ... }`,
and events must be in date order.

`history` is **opt-in**. Returning it always would make a portfolio scan O(n) in
memory per loan for a field it never reads.

<!--CODE:02_servicing-->

```basic
' Recipe 2 — Servicing: what actually happened.
'
' `lending.apply` is a FOLD over the event list, not an incremental step. The
' state is a pure function of the loan and everything that has happened to it,
' so "why is this balance what it is" is answerable by replaying the record.
' Stored incremental state makes the number itself the answer, and if it is
' ever wrong there is nothing left to reconstruct it from.
'
' An event is { on: date, kind: "payment"|"fee"|"rate_change", amount: ... },
' and events must be in date order.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  feb {date}= "2026-02-01"
  mar15 {date}= "2026-03-15"
  apr {date}= "2026-04-01"
  may {date}= "2026-05-01"

  scheduled {USD}= "888.49"
  late_fee {USD}= "25.00"
  partial {USD}= "400.00"
  catch_up {USD}= "1400.00"

  events = [
    { on: feb,   kind: "payment", amount: scheduled },
    { on: mar15, kind: "fee",     amount: late_fee },
    { on: mar15, kind: "payment", amount: scheduled },
    { on: apr,   kind: "payment", amount: partial },
    { on: may,   kind: "payment", amount: catch_up }
  ]

  ' History is OPT-IN. Returning it always would make a portfolio scan O(n) in
  ' memory per loan for a field it never reads.
  st = lending.apply(l, events, true)

  print "date         event         balance     owed interest   fees"
  for each h in st.history
    print (string(h.on.year) + "-" + pad(h.on.month) + "-" + pad(h.on.day)
           + "   " + fill(h.kind, 12) + "  " + fill(string(h.balance), 10)
           + "  " + fill(string(h.accrued), 8) + "     " + string(h.fees_due))
  next

  print ""
  print "principal repaid:  " + string(st.paid_principal)
  print "interest paid:     " + string(st.paid_interest)
  print "fees paid:         " + string(st.paid_fees)
  print "still owed:        " + string(st.balance)

  ' The fold's own invariant, and worth asserting rather than trusting: every
  ' dollar received went somewhere.
  received {USD}= "0.00"
  for each e in events
    if e.kind = "payment" then
      received = received + e.amount
    end if
  next
  print ""
  print "received:          " + string(received)
  print "accounted for:     " + string(st.paid_principal + st.paid_interest + st.paid_fees)
  print "nothing lost:      " + string(received = st.paid_principal + st.paid_interest + st.paid_fees)
end program

function pad(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function

function fill(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
```

<!--OUT:02_servicing-->

```
date         event         balance     owed interest   fees
2026-02-01   payment       9211.51     0.00         0.00
2026-03-15   fee           9211.51     92.12        25.00
2026-03-15   payment       8440.14     0.00         0.00
2026-04-01   payment       8040.14     0.00         0.00
2026-05-01   payment       6720.54     0.00         0.00

principal repaid:  3279.46
interest paid:     272.52
fees paid:         25.00
still owed:        6720.54

received:          3576.98
accounted for:     3576.98
nothing lost:      true
```

---

## 3. Accrual basis: two different loans wearing the same numbers

`amortized` accrues one period's interest per whole period elapsed since
origination, and nothing for a part period. `daily_simple` runs a meter: the
actual balance for the actual days.

Same principal, same rate, same payment — different loans. This is why the
basis is declared and never inferred, and it is also where the library was
first **wrong**: `_accrue` prorated the amortized basis by days, which makes
the two algebraically identical. The bases agreed to the cent and the
declaration was decorative. The test that caught it is the one this recipe
prints: the two must *differ*, and the difference must be the days.

<!--CODE:03_the_two_bases-->

```basic
' Recipe 3 — Accrual basis: two different loans wearing the same numbers.
'
' `amortized` accrues one period's interest per whole period elapsed since
' origination, and nothing for a part period. `daily_simple` runs a meter: the
' actual balance for the actual days.
'
' Same principal, same rate, same payment. Different loans. This is why the
' basis is declared and never inferred — and why the first version of the
' library was WRONG here: it prorated the amortized basis by days, which makes
' the two algebraically identical. The bases agreed to the cent and the
' declaration was decorative. The test that caught it is the one below: the
' two must DIFFER, and the difference must be the days.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  amort = lending.loan({ principal: amount, rate: 0.12, term: 12,
                         opened: opened, basis: "amortized",
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })
  daily = lending.loan({ principal: amount, rate: 0.12, term: 12,
                         opened: opened, basis: "daily_simple",
                         waterfall: "fees_interest_principal",
                         day_count: "actual/365" })

  feb {date}= "2026-02-01"
  jan27 {date}= "2026-01-27"

  on_time = [{ on: feb, kind: "payment", amount: scheduled }]
  early   = [{ on: jan27, kind: "payment", amount: scheduled }]

  a1 = lending.apply(amort, on_time, false)
  d1 = lending.apply(daily, on_time, false)

  print "one payment, on the due date (31 days after origination)"
  print "  amortized      interest " + string(a1.paid_interest)
  print "  daily_simple   interest " + string(d1.paid_interest)
  print ("  daily-simple charges for the 31st day too, so it is dearer by "
         + string(d1.paid_interest - a1.paid_interest))

  a2 = lending.apply(amort, early, false)
  d2 = lending.apply(daily, early, false)

  print ""
  print "the same payment five days early (26 days after origination)"
  print "  amortized      interest " + string(a2.paid_interest)
  print "  daily_simple   interest " + string(d2.paid_interest)

  ' That is the difference, stated plainly. On a daily-simple loan paying early
  ' saves the borrower five days, because the meter stopped five days sooner.
  ' On an amortized loan the interest belongs to the PERIOD; on the 27th no
  ' period has closed, so nothing has accrued and paying early buys nothing.
  ' Neither is a bug. They are different products, and a borrower who thinks
  ' they have one when they have the other is surprised at payoff.
  print ""
  print ("paying early saves interest on a daily-simple loan: "
         + string(d2.paid_interest < d1.paid_interest))
  print "on an amortized loan the interest arrives with the period, not the day."

  ' So the balances part company, which is the whole point.
  print ""
  print "balance after the on-time payment"
  print "  amortized     " + string(a1.balance)
  print "  daily_simple  " + string(d1.balance)
  print "  the two bases differ: " + string(a1.balance != d1.balance)
end program
```

<!--OUT:03_the_two_bases-->

```
one payment, on the due date (31 days after origination)
  amortized      interest 100.00
  daily_simple   interest 101.92
  daily-simple charges for the 31st day too, so it is dearer by 1.92

the same payment five days early (26 days after origination)
  amortized      interest 0.00
  daily_simple   interest 85.48

paying early saves interest on a daily-simple loan: true
on an amortized loan the interest arrives with the period, not the day.

balance after the on-time payment
  amortized     9211.51
  daily_simple  9213.43
  the two bases differ: true
```

---

## 4. The waterfall: where a short payment lands

A borrower sends less than is owed and fees, interest and principal are all
outstanding. Which is paid first is written in the note. It changes the
balance, the interest that accrues next period, and whether the loan reports
delinquent.

The **totals do not differ** — the same money arrived either way. Only the
components move, which is exactly why a waterfall bug is hard to see: every
figure on the statement is a perfectly ordinary amount of money. So the check
worth making asserts the totals are *equal* and the components *differ*. A test
comparing totals alone would pass on a library with no waterfall at all.

<!--CODE:04_the_waterfall-->

```basic
' Recipe 4 — The waterfall: where a short payment lands.
'
' A borrower sends less than is owed. Fees, interest and principal are all
' outstanding. Which gets paid first is a POLICY, it is written in the note,
' and it changes the balance, the interest that accrues next period, and
' whether the loan reports delinquent.
'
' The totals do NOT differ — the same money arrived either way. Only the
' components move, which is exactly why a waterfall bug is hard to see: every
' figure on the statement is a perfectly ordinary amount of money.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  fee {USD}= "50.00"
  short {USD}= "120.00"

  fip = lending.loan({ principal: amount, rate: 0.12, term: 12,
                       opened: opened, basis: "amortized",
                       waterfall: "fees_interest_principal",
                       day_count: "actual/365" })
  ipf = lending.loan({ principal: amount, rate: 0.12, term: 12,
                       opened: opened, basis: "amortized",
                       waterfall: "interest_principal_fees",
                       day_count: "actual/365" })

  mid {date}= "2026-01-15"
  feb {date}= "2026-02-01"

  ' A late fee on the 15th, then $120 on the 1st against $100 of interest and
  ' a $50 fee. There is not enough to clear both.
  events = [{ on: mid, kind: "fee",     amount: fee },
            { on: feb, kind: "payment", amount: short }]

  a = lending.apply(fip, events, false)
  b = lending.apply(ipf, events, false)

  print "fees_interest_principal — the fee is taken first"
  print "  to fees:       " + string(a.paid_fees)
  print "  to interest:   " + string(a.paid_interest)
  print "  to principal:  " + string(a.paid_principal)
  print "  still owed:    interest " + string(a.accrued) + ", fees " + string(a.fees_due)

  print ""
  print "interest_principal_fees — interest is taken first, the fee waits"
  print "  to fees:       " + string(b.paid_fees)
  print "  to interest:   " + string(b.paid_interest)
  print "  to principal:  " + string(b.paid_principal)
  print "  still owed:    interest " + string(b.accrued) + ", fees " + string(b.fees_due)

  ' The check worth making. Assert the TOTALS are equal — because they are, by
  ' construction — and the COMPONENTS differ. A test that compared totals would
  ' pass on a library that had no waterfall at all.
  print ""
  print ("same money received either way:   "
         + string(a.paid_fees + a.paid_interest + a.paid_principal
                  = b.paid_fees + b.paid_interest + b.paid_principal))
  print "different place on the balance:   " + string(a.balance != b.balance)

  ' An overpayment is REFUSED rather than becoming a negative balance. Money
  ' beyond what is owed is a refund, and a loan does not hold it.
  on error goto next
  huge {USD}= "99999.00"
  x = lending.apply(fip, [{ on: feb, kind: "payment", amount: huge }], false)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:04_the_waterfall-->

```
fees_interest_principal — the fee is taken first
  to fees:       50.00
  to interest:   70.00
  to principal:  0.00
  still owed:    interest 30.00, fees 0.00

interest_principal_fees — interest is taken first, the fee waits
  to fees:       0.00
  to interest:   100.00
  to principal:  20.00
  still owed:    interest 0.00, fees 50.00

same money received either way:   true
different place on the balance:   true

refused: lending.apply: payment exceeds what is owed by 89899.00 -- an overpayment is a refund, not a negative balance
```

---

## 5. "What do I owe if I pay it off on Thursday?"

A payoff quote is not the balance. It is the balance plus interest accrued
since the last event, plus anything still owed in fees — and it goes stale, so
it carries a **per-diem** saying what each further day costs.

`payoff` takes `as_of` rather than `on`, because `on` is a reserved word and
cannot be a parameter name.

<!--CODE:05_a_payoff_quote-->

```basic
' Recipe 5 — "What do I owe if I pay it off on Thursday?"
'
' A payoff quote is not the balance. It is the balance PLUS interest accrued
' since the last event, plus anything still owed in fees — and it goes stale,
' so it carries a per-diem saying what each further day costs.
'
' `payoff` takes `as_of` rather than `on` because `on` is a reserved word and
' cannot be a parameter name.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "daily_simple",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  feb {date}= "2026-02-01"
  mar {date}= "2026-03-01"
  events = [{ on: feb, kind: "payment", amount: scheduled }]

  quote = lending.payoff(l, events, mar)

  print "payoff quote for 2026-03-01"
  print "  principal   " + string(quote.principal)
  print "  interest    " + string(quote.interest)
  print "  fees        " + string(quote.fees)
  print "  total       " + string(quote.total)
  print "  per diem    " + string(quote.per_diem)

  ' The parts must be the total. Worth asserting rather than trusting: a quote
  ' that is off by the accrued interest is a perfectly ordinary-looking number
  ' and the borrower pays it.
  print ""
  print "the parts are the total: "
  print "  " + string(quote.total = quote.principal + quote.interest + quote.fees)

  ' A quote goes stale. Two days later it is the per-diem larger — which is
  ' what the per-diem is FOR, and it is why a quote is dated.
  mar3 {date}= "2026-03-03"
  later = lending.payoff(l, events, mar3)
  print ""
  print "two days later the total is " + string(later.total)
  print "  which is larger by         " + string(later.total - quote.total)
  print "  and two per-diems are      " + string(quote.per_diem * 2)

  ' A quote for a date BEFORE the last event is refused. It is not a quote,
  ' it is a question about the past that the event list already answers.
  on error goto next
  jan {date}= "2026-01-10"
  bad = lending.payoff(l, events, jan)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
```

<!--OUT:05_a_payoff_quote-->

```
payoff quote for 2026-03-01
  principal   9213.43
  interest    84.81
  fees        0.00
  total       9298.24
  per diem    3.03

the parts are the total: 
  true

two days later the total is 9304.30
  which is larger by         6.06
  and two per-diems are      6.06

refused: lending.payoff: the date is before the last event
```

---

## 6. Underwriting ratios, and the `unknown` that keeps them honest

Four ratios and two traps.

**A missing input returns `unknown`, never zero.** These feed credit decisions.
An absent income figure that silently became zero would make every ratio look
either perfect or catastrophic, and nothing on the page would say which.

**`dscr` is oriented the other way up.** Low is good for loan-to-value,
debt-to-income and payment-to-income; *above one* is good for debt service
coverage. That is a real trap, which is why it is its own named function rather
than folded into `dti`.

<!--CODE:06_underwriting-->

```basic
' Recipe 6 — Underwriting ratios, and the `unknown` that keeps them honest.
'
' Four ratios, and two traps.
'
' TRAP ONE: a missing input returns `unknown`, never zero. These feed credit
' decisions. An absent income figure that silently became zero would make
' every ratio look either perfect or catastrophic, and nothing on the page
' would say which.
'
' TRAP TWO: `dscr` is oriented the OTHER WAY UP. Low is good for the first
' three; above one is good for dscr. That is a real trap, which is why it is
' its own named function rather than folded into `dti`.
program main()
  load lending

  loan_amount {USD}= "240000.00"
  value {USD}= "300000.00"
  monthly_debt {USD}= "2000.00"
  monthly_income {USD}= "6000.00"
  monthly_payment {USD}= "1498.88"
  noi {USD}= "130000.00"
  debt_service {USD}= "100000.00"

  print "loan to value:       " + pct(lending.ltv(loan_amount, value))
  print "debt to income:      " + pct(lending.dti(monthly_debt, monthly_income))
  print "payment to income:   " + pct(lending.payment_to_income(monthly_payment, monthly_income))
  print ""
  print "debt service coverage: " + string(round(lending.dscr(noi, debt_service), 4))
  print "  above 1 is healthy — the inverse of the three above."
  print "  healthy here?        " + string(lending.dscr(noi, debt_service) > 1)

  ' A missing input is `unknown`, and a caller can SEE that it is missing.
  ' Compare with the alternative: a zero income gives a debt-to-income of
  ' infinity or of nothing, and either reads as a number somebody can act on.
  no_income = lending.dti(monthly_debt, nothing)
  print ""
  print "with no income on file:"
  print "  is it unknown?  " + string(is_unknown(no_income))
  print "  is it zero?     " + string(no_income = 0)

  ' A zero denominator is the same case for the same reason.
  nothing_earned {USD}= "0.00"
  print "with an income of exactly zero:"
  print "  is it unknown?  " + string(is_unknown(lending.dti(monthly_debt, nothing_earned)))
end program

function pct(r)
  if is_unknown(r) then
    return "unknown"
  end if
  return string(round(r * 100, 2)) + "%"
end function
```

<!--OUT:06_underwriting-->

```
loan to value:       80%
debt to income:      33.33%
payment to income:   24.98%

debt service coverage: 1.3
  above 1 is healthy — the inverse of the three above.
  healthy here?        true

with no income on file:
  is it unknown?  true
  is it zero?     false
with an income of exactly zero:
  is it unknown?  true
```

---

## 7. Posting a loan to real books

`lending.entries` **emits** entries; it never posts them. The caller owns the
ledger, so an application posts to its own chart, batches, or throws them away.
`accounts` maps the four roles — `receivable`, `cash`, `interest_income`,
`fee_income` — onto your own codes.

This is also how the arithmetic is *proved* rather than asserted. An entry that
does not balance, or that names an account not in the chart, is refused where
it is posted. So a loan whose whole life posts cleanly, leaving receivables
equal to the servicing balance and interest income equal to interest collected,
has demonstrated its own figures.

**Known cost, recorded rather than optimised:** `entries` folds each prefix of
the event list, so it is O(n²) in events. At a loan's scale that is nothing —
360 payments is 65,000 cheap operations. At a portfolio's it would matter, and
the remedy is the checkpoint the design already names.

<!--CODE:07_to_the_ledger-->

```basic
' Recipe 7 — Posting a loan to real books.
'
' `lending.entries` EMITS entries; it never posts them. The caller owns the
' ledger, so an application posts to its own chart, batches, or throws them
' away. `accounts` maps the four roles onto your own codes.
'
' This is also how the arithmetic is PROVED rather than asserted. An entry
' that does not balance, or that names an account not in the chart, is refused
' where it is posted — so a loan whose whole life posts cleanly, leaving
' receivables equal to the servicing balance, has demonstrated its own figures.
program main()
  load lending
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash",             kind: "asset" },
    { code: "1200", name: "Loans receivable", kind: "asset" },
    { code: "4100", name: "Interest income",  kind: "revenue" },
    { code: "4200", name: "Fee income",       kind: "revenue" }
  ])

  accounts = { }
  accounts["receivable"]      = "1200"
  accounts["cash"]            = "1000"
  accounts["interest_income"] = "4100"
  accounts["fee_income"]      = "4200"

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  scheduled {USD}= "888.49"

  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  ' Eleven monthly payments, February to December.
  events = []
  for m = 2 to 12
    d {date}= "2026-" + two(m) + "-01"
    append(events, { on: d, kind: "payment", amount: scheduled })
  next

  entries = lending.entries(books, l, events, accounts)
  lg = accounting.ledger()
  for each e in entries
    lg = accounting.post(lg, e)
  next

  print "entries emitted: " + string(count(entries))
  print "  one advance at origination, plus one per payment received."

  bal = accounting.balances(lg, nothing)
  sheet = accounting.balance_sheet(books, lg, nothing)
  serviced = lending.apply(l, events, false)

  print ""
  print "loans receivable on the books:  " + string(bal["1200"])
  print "servicing balance on the loan:  " + string(serviced.balance)
  print "they agree:                     " + string(bal["1200"] = serviced.balance)

  ' Revenue carries a credit balance, which `balances` reports as negative
  ' against the debit convention. Flip the sign to compare with cash collected.
  print ""
  print "interest income recognised:     " + string(bal["4100"] * -1)
  print "interest actually collected:    " + string(serviced.paid_interest)
  print "they agree:                     " + string(bal["4100"] * -1 = serviced.paid_interest)

  print ""
  print "and the ledger balances:        " + string(sheet.balanced)
end program

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
```

<!--OUT:07_to_the_ledger-->

```
entries emitted: 12
  one advance at origination, plus one per payment received.

loans receivable on the books:  778.92
servicing balance on the loan:  778.92
they agree:                     true

interest income recognised:     552.31
interest actually collected:    552.31
they agree:                     true

and the ledger balances:        true
```

---

## What is not here

`lending` covers fixed-rate instalment loans, servicing, payoff and the
accounting boundary. Adjustable-rate machinery (caps, floors, index lookups),
leases and escrow are named in [lending_design.md](lending_design.md) §8 and
are not built. APR and its jurisdiction-specific rules are deliberately out of
the core library: they are policy, and policy belongs where the lender's
compliance team can read it.
