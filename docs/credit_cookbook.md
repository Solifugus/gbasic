# Credit analytics cookbook

Worked recipes for `stdlib/credit.bas` — vintage curves, roll rates, migration
and charge-off over a **book**, not a loan.

**This page cannot lie.** Every code block below is a real file under
`examples/credit_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_credit_cookbook.sh`.

Design and rationale:
[credit_analytics_design.md](credit_analytics_design.md). The single-loan
library this sits on: [lending_cookbook.md](lending_cookbook.md).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/credit_cookbook/01_the_status_table.bas
```

---

## The shape of everything on this page

Every defect this library exists to prevent produces **an ordinary-looking
percentage**. A roll rate that drops attrition, a vintage curve on the wrong
index, a matrix that double-counts — each reads as a book doing slightly better
or worse than expected. None of them raises anything.

So the recipes are written to put the *wrong* answer beside the right one
rather than merely leave it out. Recipe 4 prints both denominators. Recipe 5
prints dashes where a naive table would print `0%`.

---

## 1. The input is a status table, not a list of loans

`lending` answers questions about one loan. `credit` answers questions about a
book — *is this year's lending worse than last year's, where did the 30-day
bucket go, what did we lose* — and none of those is arithmetic about a balance.
They are questions about **states over time**.

So the input is one row per loan per observation date:

```
{ id:, opened:, as_of:, status:, balance: }
```

Two reasons, and neither is taste.

**Cost.** `lending.apply` is a fold, deliberately, so a 5,000-loan book over 36
month-ends is 180,000 folds.

**Provenance.** Real portfolio data *arrives* in this shape. It is what a
servicer extract looks like and what the published Fannie Mae and Freddie Mac
single-family performance datasets look like. A library that could only read our
own `lending` loans could not be pointed at a real book.

`credit.observe` is the **bridge**, so our own machinery is a producer of the
table rather than a special case the analytics know about.

<!--CODE:01_the_status_table-->

```basic
' Recipe 1 — The input is a status table, not a list of loans.
'
' `lending` answers questions about ONE loan. `credit` answers questions about
' a BOOK — is this year's lending worse than last year's, where did the 30-day
' bucket go, what did we lose — and none of those is arithmetic about a
' balance. They are questions about STATES OVER TIME.
'
' So the input is one row per loan per observation date:
'
'     { id:, opened:, as_of:, status:, balance: }
'
' Two reasons, and neither is taste. COST: `lending.apply` is a fold, so a
' 5,000-loan book over 36 month-ends is 180,000 folds. PROVENANCE: real
' portfolio data ARRIVES in this shape — it is what a servicer extract and the
' published Fannie Mae and Freddie Mac datasets look like. A library that could
' only read our own `lending` loans could not be pointed at a real book.
'
' `credit.observe` is the bridge, so our own machinery is a producer of the
' table rather than a special case the analytics know about.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  apr {date}= "2026-04-01"
  amount {USD}= "12000.00"

  ' Six loans. Three written in January, three in April.
  book = []
  append(book, entry("L1", loan_from(jan, amount), 11))
  append(book, entry("L2", loan_from(jan, amount), 3))
  append(book, entry("L3", loan_from(jan, amount), 11))
  append(book, entry("L4", loan_from(apr, amount), 8))
  append(book, entry("L5", loan_from(apr, amount), 8))
  append(book, entry("L6", loan_from(apr, amount), 8))

  ' Twelve month-ends.
  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next

  table = credit.observe(book, observed_on, "mba")

  ' `check` validates the whole table once, so a table one function accepts
  ' cannot be one another silently mis-reads. It returns the row count.
  print "rows in the table: " + string(credit.check(table))
  print "  not 72 — a loan contributes a row only from the month it was"
  print "  written. Emitting rows before origination would put unwritten"
  print "  loans in `current` and inflate every denominator behind them."

  print ""
  print "the loans, on 2026-12-01:"
  dec {date}= "2026-12-01"
  for each r in table
    if r.as_of = dec then
      print "  " + r.id + "  " + fill(r.status, 14) + string(r.balance)
    end if
  next

  ' L2 stopped paying after three instalments. Watch it walk the ladder.
  print ""
  print "L2, which paid three times and stopped:"
  for each r in table
    if r.id = "L2" then
      print "  " + ymd(r.as_of) + "  " + r.status
    end if
  next
end program

function loan_from(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function entry(id, l, how_many)
  load lending
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
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

<!--OUT:01_the_status_table-->

```
rows in the table: 63
  not 72 — a loan contributes a row only from the month it was
  written. Emitting rows before origination would put unwritten
  loans in `current` and inflate every denominator behind them.

the loans, on 2026-12-01:
  L1  current       6673.70
  L2  dpd_120_plus  10527.80
  L3  current       6673.70
  L4  current       8226.70
  L5  current       8226.70
  L6  current       8226.70

L2, which paid three times and stopped:
  2026-01-01  current
  2026-02-01  current
  2026-03-01  current
  2026-04-01  current
  2026-05-01  dpd_30
  2026-06-01  dpd_60
  2026-07-01  dpd_90
  2026-08-01  dpd_120_plus
  2026-09-01  dpd_120_plus
  2026-10-01  dpd_120_plus
  2026-11-01  dpd_120_plus
  2026-12-01  dpd_120_plus
```

---

## 2. Two servicers, identical loans, different delinquency. Both right.

| method | what it counts | 30 days delinquent when |
|---|---|---|
| `mba` | payments missed | the day after one payment is missed |
| `ots` | days past due | the oldest unpaid instalment is 30 days old |

On a monthly loan the two run about a month apart for the whole life of a
delinquency. A book reported one way is **not comparable** with a book reported
the other, so the method is required and never inferred — the same rule
`lending` sets for accrual basis, waterfall and day count.

Both conventions read the *same* input, which is what makes them comparable at
all: one counts the dues that have come and gone, the other measures the age of
the oldest.

<!--CODE:02_delinquency-->

```basic
' Recipe 2 — Two servicers, identical loans, different delinquency. Both right.
'
'   mba — 30 days delinquent the day AFTER one payment is missed. The count is
'         PAYMENTS MISSED.
'   ots — 30 days delinquent when the oldest unpaid instalment is 30 days old.
'         The count is DAYS PAST DUE.
'
' On a monthly loan the two run about a month apart for the whole life of a
' delinquency. A book reported one way is not comparable with a book reported
' the other, so `credit` requires the method and never infers it.
program main()
  load credit

  ' A borrower who last paid in January. The February, March and April
  ' instalments are outstanding.
  feb {date}= "2026-02-01"
  mar {date}= "2026-03-01"
  apr {date}= "2026-04-01"
  unpaid = [feb, mar, apr]

  print "date         payments due   mba            ots"
  for each d in probe_dates()
    print ("  " + ymd(d) + "   " + string(due_by(unpaid, d)) + "              "
           + fill(credit.bucket(unpaid, d, "mba"), 15)
           + credit.bucket(unpaid, d, "ots"))
  next

  ' The relationship, stated rather than left to the eye: MBA is never gentler.
  ' It counts a whole missed payment as thirty days the moment it is missed,
  ' where OTS waits for the calendar to catch up.
  print ""
  print "mba is at or ahead of ots on every row above."

  ' The two agree when nothing is owed, which is the control: a `bucket` that
  ' ignored its method entirely would still pass a current-only comparison.
  jan15 {date}= "2026-01-15"
  print ""
  print "with nothing due yet:"
  print "  mba " + credit.bucket(unpaid, jan15, "mba")
  print "  ots " + credit.bucket(unpaid, jan15, "ots")

  ' There is no default. Asking for one is refused by name, because a wrong
  ' guess here shifts a whole book by a bucket and nothing on the report says so.
  on error goto next
  b = credit.bucket(unpaid, apr, "whatever")
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function probe_dates()
  a {date}= "2026-02-02"
  b {date}= "2026-03-02"
  c {date}= "2026-04-02"
  d {date}= "2026-04-15"
  e {date}= "2026-05-15"
  return [a, b, c, d, e]
end function

function due_by(unpaid, as_of)
  n = 0
  for each u in unpaid
    if u <= as_of then
      n = n + 1
    end if
  next
  return n
end function

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
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

<!--OUT:02_delinquency-->

```
date         payments due   mba            ots
  2026-02-02   1              dpd_30         current
  2026-03-02   2              dpd_60         current
  2026-04-02   3              dpd_90         dpd_30
  2026-04-15   3              dpd_90         dpd_60
  2026-05-15   3              dpd_90         dpd_90

mba is at or ahead of ots on every row above.

with nothing due yet:
  mba current
  ots current

refused: credit.bucket: method must be "mba" or "ots" -- they are a month apart and neither is the default (got whatever)
```

---

## 3. Where the book went, in counts

`migration` reports where every loan that existed at one date had got to by the
next. It returns **counts, never rates** — `roll_rates` divides — because the
invariant this library rests on is about counts, and a rate cannot state it:

> **Every loan observed at *t* is accounted for at *t+1*.**

Nothing in the library enforces that. It falls out of correct bucketing, which
is exactly what makes it worth asserting — the counterpart of the accounting
equation in [accounting_cookbook.md](accounting_cookbook.md).

**Attrition is a state, not a hole.** `paid_off` and `charged_off` are buckets
and nothing rolls out of them. A loan present at *t* and *missing* at *t+1* is
reported as `unobserved` rather than dropped: "we stopped seeing it" is a fact
about the data, not about the borrower.

<!--CODE:03_migration-->

```basic
' Recipe 3 — Where the book went, in counts.
'
' `migration` reports where every loan that existed at one date had got to by
' the next. It returns COUNTS, never rates — `roll_rates` divides, in recipe 4
' — because the invariant this library rests on is about counts and a rate
' cannot state it:
'
'     EVERY LOAN OBSERVED AT t IS ACCOUNTED FOR AT t+1.
'
' Nothing in the library enforces that. It falls out of correct bucketing,
' which is exactly what makes it worth asserting.
'
' ATTRITION IS A STATE, NOT A HOLE. `paid_off` and `charged_off` are buckets
' and nothing rolls out of them. A loan present at t and MISSING at t+1 is
' reported as `unobserved` rather than dropped — "we stopped seeing it" is a
' fact about the data, not about the borrower, and dropping those loans makes
' a book look like it is curing.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  append(book, plain("L1", jan, amount, 11))
  append(book, plain("L2", jan, amount, 11))
  append(book, plain("L3", jan, amount, 11))
  append(book, plain("L4", jan, amount, 3))       ' stops paying
  append(book, paid_off_at("L5", jan, amount, 8)) ' clears the balance
  append(book, plain("L6", jan, amount, 11))      ' leaves the extract

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next

  full = credit.observe(book, observed_on, "mba")

  ' A REAL EXTRACT HAS HOLES. L6 was sold to another servicer in September and
  ' simply stops appearing. `observe` would never do this — it emits a row for
  ' every date once a loan is written — so it is done here by hand, which is
  ' also the honest way to say that this is what your own data will look like.
  sep {date}= "2026-09-01"
  table = []
  for each r in full
    if r.id != "L6" or r.as_of <= sep then
      append(table, r)
    end if
  next

  oct {date}= "2026-10-01"
  m = credit.migration(table, sep, oct)

  print "migration 2026-09-01 to 2026-10-01"
  print "  loans at the start:  " + string(m.total)
  print "  still observed:      " + string(m.observed)
  print "  no longer observed:  " + string(m.total - m.observed)
  print "  new since the start: " + string(m.entered)

  print ""
  print "from            to              n"
  for each b in m.buckets
    if m.starting[b] > 0 then
      for each c in m.buckets
        if m.counts[b][c] > 0 then
          print "  " + fill(b, 14) + fill(c, 14) + string(m.counts[b][c])
        end if
      next
      if m.unobserved[b] > 0 then
        print "  " + fill(b, 14) + fill("(unobserved)", 14) + string(m.unobserved[b])
      end if
    end if
  next

  ' THE INVARIANT, asserted rather than trusted. For each starting bucket, the
  ' loans that went somewhere plus the loans that vanished must be the loans
  ' that were there. A matrix that quietly dropped the vanished ones would
  ' still balance to a smaller total and look perfectly reasonable.
  print ""
  bad = 0
  for each b in m.buckets
    landed = 0
    for each c in m.buckets
      landed = landed + m.counts[b][c]
    next
    if landed + m.unobserved[b] != m.starting[b] then
      bad = bad + 1
    end if
  next
  print "every starting bucket reconciles: " + string(bad = 0)

  ' Comparing against a date with no rows would report 100% attrition, which
  ' is a plausible catastrophe rather than an error. It is refused.
  on error goto next
  ancient {date}= "2020-01-01"
  x = credit.migration(table, ancient, oct)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function terms(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function stream(l, how_many)
  load lending
  due = lending.payment(l)
  out = []
  for k = 1 to how_many
    append(out, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return out
end function

function plain(id, opened, amount, how_many)
  l = terms(opened, amount)
  return { id: id, loan: l, events: stream(l, how_many) }
end function

function paid_off_at(id, opened, amount, after)
  load lending
  l = terms(opened, amount)
  evs = stream(l, after)
  clear_on = l.opened + (1 month) * (after + 1)
  quote = lending.payoff(l, evs, clear_on)
  append(evs, { on: clear_on, kind: "payment", amount: quote.total })
  return { id: id, loan: l, events: evs }
end function

function fill(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
```

<!--OUT:03_migration-->

```
migration 2026-09-01 to 2026-10-01
  loans at the start:  6
  still observed:      5
  no longer observed:  1
  new since the start: 0

from            to              n
  current       current       3
  current       paid_off      1
  current       (unobserved)  1
  dpd_120_plus  dpd_120_plus  1

every starting bucket reconciles: true

refused: credit.migration: no rows dated 2020-01-01 -- comparing against an empty date reports 100% attrition
```

---

## 4. Roll rates, and the denominator that decides them

`roll_rates` is `migration` divided. The one decision it makes is the
denominator, and it is the whole recipe:

> **The denominator is the whole starting bucket, including the loans that
> went unobserved.**

Dropping them is the commonest defect in roll-rate work, and it does not
produce an error. It produces an ordinary-looking percentage saying the book is
curing — because the loans that disappeared were disproportionately the ones
that stopped being worth servicing.

The recipe prints both numbers on the same data: **100%** stayed current
counting only the loans still visible, **75%** counting the whole starting
bucket. The first is not bad arithmetic. It answers a different question, and
nothing on a report distinguishes them.

A bucket with nothing in it reports `unknown`, never zero. A zero roll rate out
of an empty bucket reads as "nothing went bad" when the truth is "there was
nothing there".

<!--CODE:04_roll_rates-->

```basic
' Recipe 4 — Roll rates, and the denominator that decides them.
'
' `roll_rates` is `migration` divided. The one decision it makes is the
' denominator, and it is the whole recipe:
'
'     THE DENOMINATOR IS THE WHOLE STARTING BUCKET, INCLUDING THE LOANS
'     THAT WENT UNOBSERVED.
'
' Dropping them is the commonest defect in roll-rate work, and it does not
' produce an error. It produces an ordinary-looking percentage that says the
' book is curing, because the loans that disappeared were disproportionately
' the ones that stopped being worth servicing.
'
' A bucket with nothing in it reports `unknown`, never zero. A zero roll rate
' out of an empty bucket reads as "nothing went bad" when the truth is "there
' was nothing there".
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  for i = 1 to 8
    append(book, plain("C" + string(i), jan, amount, 11))
  next
  append(book, plain("D1", jan, amount, 3))
  append(book, plain("D2", jan, amount, 4))

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  full = credit.observe(book, observed_on, "mba")

  ' Two of the eight paying loans drop out of the extract after September.
  sep {date}= "2026-09-01"
  oct {date}= "2026-10-01"
  table = []
  for each r in full
    gone = (r.id = "C7" or r.id = "C8") and r.as_of > sep
    if not gone then
      append(table, r)
    end if
  next

  rr = credit.roll_rates(table, sep, oct)
  m = credit.migration(table, sep, oct)

  print "from `current`, 2026-09-01 to 2026-10-01"
  print "  loans in the bucket:  " + string(rr.starting["current"])
  print "  stayed current:       " + pct(rr.rates["current"]["current"])
  print "  went unobserved:      " + pct(rr.unobserved["current"])

  ' What the wrong denominator would have said. Same data, one decision
  ' different, and the difference is a book that looks flawless.
  stayed = m.counts["current"]["current"]
  survivors = m.starting["current"] - m.unobserved["current"]
  print ""
  print "counting only the loans still visible:"
  print ("  " + string(stayed) + " of " + string(survivors) + " = "
         + pct(stayed / survivors) + " stayed current")
  print "counting the whole starting bucket, as the library does:"
  print ("  " + string(stayed) + " of " + string(m.starting["current"]) + " = "
         + pct(rr.rates["current"]["current"]))
  print "The first number is not wrong arithmetic. It answers a different"
  print "question, and nothing on a report distinguishes them."

  ' An empty bucket, which every real book has several of.
  print ""
  print "nothing was charged off in September:"
  print "  loans in the bucket:  " + string(rr.starting["charged_off"])
  print "  roll to current:      " + pct(rr.rates["charged_off"]["current"])
  print "  is that unknown?      " + string(is_unknown(rr.rates["charged_off"]["current"]))
  print "  is that zero?         " + string(rr.rates["charged_off"]["current"] = 0)
end program

function terms(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function plain(id, opened, amount, how_many)
  load lending
  l = terms(opened, amount)
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function

function pct(r)
  if is_unknown(r) then
    return "unknown"
  end if
  return string(round(r * 100, 1)) + "%"
end function
```

<!--OUT:04_roll_rates-->

```
from `current`, 2026-09-01 to 2026-10-01
  loans in the bucket:  8
  stayed current:       75%
  went unobserved:      25%

counting only the loans still visible:
  6 of 6 = 100% stayed current
counting the whole starting bucket, as the library does:
  6 of 8 = 75%
The first number is not wrong arithmetic. It answers a different
question, and nothing on a report distinguishes them.

nothing was charged off in September:
  loans in the bucket:  0
  roll to current:      unknown
  is that unknown?      true
  is that zero?         false
```

---

## 5. Vintage curves: age, never calendar month

A vintage curve is a cohort's cumulative bad rate plotted against **months on
book**, so loans written two years apart can be compared at the same point in
their lives. Indexed by calendar month instead, every cohort's curve starts at a
different age, the averages mix cohorts, and the result is a smooth meaningless
line that does not look like a mistake.

**Cumulative means ever-reached.** Once a loan touches a bad status it counts at
that age and every later one, even if it cures. That is the convention a loss
curve is drawn under, and it is declared rather than assumed because "currently
in" is a different and equally reasonable curve.

**The table is a triangle.** A cohort's curve stops at the age it has reached.
Carrying every cohort out to the oldest one's age would report 0% at ages the
young cohort has not lived through — *no losses* where the truth is *no data*.

**`basis` is required**, and the two are different curves: `original` divides by
the cohort as it was written, `outstanding` by what is still on the book. They
part company as soon as anything runs off, which is why the January cohort below
has prepayments in it and the others do not — a cohort with defaults and no
prepayments has an identical curve either way.

<!--CODE:05_vintage-->

```basic
' Recipe 5 — Vintage curves: age, never calendar month.
'
' A vintage curve is a cohort's cumulative bad rate plotted against MONTHS ON
' BOOK, so loans written two years apart can be compared at the same point in
' their lives. Indexed by calendar month instead, every cohort's curve starts
' at a different age, the averages mix cohorts, and the result is a smooth
' meaningless line that does not look like a mistake.
'
' CUMULATIVE MEANS EVER-REACHED: once a loan touches a bad status it counts at
' that age and every later one, even if it cures. That is the convention a
' loss curve is drawn under, and it is declared rather than assumed because
' "currently in" is a different and equally reasonable curve.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  apr {date}= "2026-04-01"
  jul {date}= "2026-07-01"
  amount {USD}= "12000.00"

  ' Three quarterly cohorts of ten. Two in each stop paying after two
  ' instalments; two of the January cohort prepay and leave the book.
  book = []
  for each c in [{ opened: jan, tag: "A", prepay: 2 },
                 { opened: apr, tag: "B", prepay: 0 },
                 { opened: jul, tag: "C", prepay: 0 }]
    for i = 1 to 10
      id = c.tag + string(i)
      if i <= 2 then
        append(book, plain(id, c.opened, amount, 2))
      else if i <= 2 + c.prepay then
        append(book, prepaid(id, c.opened, amount, 4))
      else
        append(book, plain(id, c.opened, amount, 11))
      end if
    next
  next

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  table = credit.observe(book, observed_on, "mba")

  v = credit.vintage(table, { basis: "original", cohort_by: "quarter" })

  print "cumulative bad rate by months on book (basis: original)"
  print ""
  header = pad("age", 5)
  for each c in v.cohorts
    header = header + "  " + pad(c, 10)
  next
  print trim(header)
  for age = 0 to 11
    line = pad(string(age), 5)
    any = false
    for each c in v.cohorts
      r = at_age(v, c, age)
      if is_unknown(r) then
        line = line + "  " + pad("-", 10)
      else
        line = line + "  " + pad(pct(r), 10)
        any = true
      end if
    next
    if any then
      print trim(line)
    end if
  next

  ' THE TABLE IS A TRIANGLE, and that is the point of the dashes. A cohort's
  ' curve STOPS at the age it has reached. Carrying every cohort out to the
  ' oldest one's age would report 0% at ages the young cohort has not lived
  ' through — "no losses" where the truth is "no data", which is the same
  ' plausible wrong answer this library exists to refuse.
  print ""
  print "  a dash is NO DATA, not a zero. The 2026-Q3 cohort is five months"
  print "  old; it has no month-nine bad rate to report."

  ' BASIS IS REQUIRED, and the two are different curves. `original` divides by
  ' the cohort as it was written; `outstanding` by what is still on the book.
  ' They part company as soon as anything runs off — which is why the January
  ' cohort has prepayments in it and the others do not.
  o = credit.vintage(table, { basis: "outstanding", cohort_by: "quarter" })
  print ""
  print "the January cohort at month 9:"
  print ("  original     " + pct(at_age(v, "2026-Q1", 9))
         + "   (2 bad out of the 10 written)")
  print ("  outstanding  " + pct(at_age(o, "2026-Q1", 9))
         + "   (2 bad out of the " + string(at_risk(o, "2026-Q1", 9))
         + " still on the book)")
  print "  the two differ: " + string(at_age(v, "2026-Q1", 9) != at_age(o, "2026-Q1", 9))

  ' Omitting the basis is refused. Neither is the default because neither is
  ' the answer to the other's question.
  on error goto next
  x = credit.vintage(table, { cohort_by: "quarter" })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function terms(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function stream(l, how_many)
  load lending
  due = lending.payment(l)
  out = []
  for k = 1 to how_many
    append(out, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return out
end function

function plain(id, opened, amount, how_many)
  l = terms(opened, amount)
  return { id: id, loan: l, events: stream(l, how_many) }
end function

function prepaid(id, opened, amount, after)
  load lending
  l = terms(opened, amount)
  evs = stream(l, after)
  clear_on = l.opened + (1 month) * (after + 1)
  quote = lending.payoff(l, evs, clear_on)
  append(evs, { on: clear_on, kind: "payment", amount: quote.total })
  return { id: id, loan: l, events: evs }
end function

function at_age(v, cohort_label, age)
  for each p in v.series[cohort_label]
    if p.age = age then
      return p.rate
    end if
  next
  return unknown
end function

function at_risk(v, cohort_label, age)
  for each p in v.series[cohort_label]
    if p.age = age then
      return p.at_risk
    end if
  next
  return unknown
end function

function pct(r)
  if is_unknown(r) then
    return "-"
  end if
  return string(round(r * 100, 1)) + "%"
end function

function pad(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
```

<!--OUT:05_vintage-->

```
cumulative bad rate by months on book (basis: original)

age    2026-Q1     2026-Q2     2026-Q3
0      0%          0%          0%
1      0%          0%          0%
2      0%          0%          0%
3      0%          0%          0%
4      0%          0%          0%
5      20%         20%         20%
6      20%         20%         -
7      20%         20%         -
8      20%         20%         -
9      20%         -           -
10     20%         -           -
11     20%         -           -

  a dash is NO DATA, not a zero. The 2026-Q3 cohort is five months
  old; it has no month-nine bad rate to report.

the January cohort at month 9:
  original     20%   (2 bad out of the 10 written)
  outstanding  25%   (2 bad out of the 8 still on the book)
  the two differ: true

refused: credit.vintage needs a basis: "original" divides by the cohort as it was written, "outstanding" by what is still on the book, and they are different curves
```

---

## 6. Charge-offs and recoveries: gross and net are different numbers

**A charge-off is a decision, not a threshold crossing.** The servicer wrote the
balance off, on a date, and that is a fact in the record. `losses` *reads* the
`charged_off` status; it never infers one from days past due. Inferring it would
produce a loss figure the servicer's own books disagree with — and the books are
the ones that get audited.

**Gross and net are reported separately** and never netted silently. They are
different numbers used for different purposes, and quietly reporting one as the
other halves a loss rate.

Two refusals earn their place here, because both would otherwise produce a
plausible loss figure: a charge-off with no balance (the amount written off *is*
the number, and there is nothing to substitute for it), and money arriving
*before* the write-off (that is a payment, and calling it a recovery understates
the loss and overstates collections at once).

<!--CODE:06_losses-->

```basic
' Recipe 6 — Charge-offs and recoveries: gross and net are different numbers.
'
' A CHARGE-OFF IS A DECISION, not a threshold crossing. The servicer wrote the
' balance off, on a date, and that is a fact in the record. `losses` READS the
' `charged_off` status; it never infers one from days past due. Inferring it
' would produce a loss figure the servicer's own books disagree with — and the
' books are the ones that get audited.
'
' GROSS AND NET ARE REPORTED SEPARATELY and never netted silently. They are
' different numbers used for different purposes, and quietly reporting one as
' the other halves a loss rate.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  for i = 1 to 6
    append(book, plain("G" + string(i), jan, amount, 11))
  next
  append(book, plain("X1", jan, amount, 2))
  append(book, plain("X2", jan, amount, 2))

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  observed = credit.observe(book, observed_on, "mba")

  ' The servicer's decisions, applied on top of what the loans did. X1 was
  ' written off in October, X2 in November — both after months in the 120+
  ' bucket, but the DATE is the servicer's, not a rule's.
  oct {date}= "2026-10-01"
  nov {date}= "2026-11-01"
  dec {date}= "2026-12-01"
  recovered {USD}= "1500.00"

  table = []
  for each r in observed
    row = r
    if r.id = "X1" and r.as_of >= oct then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance }
    end if
    if r.id = "X2" and r.as_of >= nov then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance }
    end if
    ' A collection agency returned 1,500 on X1 in December. A recovery is
    ' attached to the row it arrived on.
    if r.id = "X1" and r.as_of = dec then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance, recovery: recovered }
    end if
    append(table, row)
  next

  l = credit.losses(table, { cohort_by: "quarter" })

  print "charge-offs by cohort"
  for each c in l.cohorts
    s = l.by_cohort[c]
    print "  " + c
    print "    accounts written off:  " + string(s.count)
    print "    gross charge-offs:     " + string(s.gross)
    print "    recoveries:            " + string(s.recoveries)
    print "    net charge-offs:       " + string(s.net)
  next

  print ""
  print "gross and net differ by the recoveries: "
  s = l.by_cohort[l.cohorts[0]]
  print "  " + string(s.gross - s.recoveries) + " = " + string(s.net)

  ' THE AMOUNT WRITTEN OFF IS THE BALANCE ON THE CHARGE-OFF ROW — the first
  ' one, so a loan carried at charged_off for months is counted once and at
  ' the figure the servicer actually wrote off.
  print ""
  print "each account is counted once: " + string(l.count) + " accounts"

  ' Two refusals worth seeing, because both would otherwise produce a
  ' plausible loss figure.
  on error goto next

  ' A charge-off with no balance. The amount written off IS the number; there
  ' is nothing to substitute for it.
  bare = []
  for each r in table
    if r.id = "X2" and r.as_of = nov then
      append(bare, { id: r.id, opened: r.opened, as_of: r.as_of,
                     status: "charged_off" })
    else
      append(bare, r)
    end if
  next
  x = credit.losses(bare, { })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if

  ' Money arriving BEFORE the write-off is a payment, not a recovery. Calling
  ' it a recovery would understate the loss and overstate collections at once.
  mar {date}= "2026-03-01"
  early = []
  for each r in table
    if r.id = "X1" and r.as_of = mar then
      append(early, { id: r.id, opened: r.opened, as_of: r.as_of,
                      status: r.status, balance: r.balance, recovery: recovered })
    else
      append(early, r)
    end if
  next
  x = credit.losses(early, { })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function plain(id, opened, amount, how_many)
  load lending
  l = lending.loan({ principal: amount, rate: 0.09, term: 24,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "30/360" })
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function
```

<!--OUT:06_losses-->

```
charge-offs by cohort
  2026-Q1
    accounts written off:  2
    gross charge-offs:     21987.13
    recoveries:            1500.00
    net charge-offs:       20487.13

gross and net differ by the recoveries: 
  20487.13 = 20487.13

each account is counted once: 2 accounts

refused: credit.losses: loan X2 is charged off with no balance -- the amount written off is the number

refused: credit.losses: loan X1 reports a recovery on 2026-03-01 that is not after a charge-off -- money before the write-off is a payment
```

---

## What is not here

`credit` measures what a book has already done. Scorecard development — turning
a population into a model that *ranks* risk — is the other half and now exists
as [`scoring`](scoring_design.md), with WOE/IV binning, AUC/KS/Gini, point
scaling and PSI. Forecasting what a book *will* do — CECL and lifetime expected
loss — is named in [credit_analytics_design.md](credit_analytics_design.md) §9
and is still not built; the measurement layer it needs now exists.

The evidence on this page is entirely self-generated: the loans are ours, the
payment streams are ours, and the figures were computed by the library being
documented. That is the honest limit of it. Real loan-level performance data
would be the equivalent of the 15,871-workbook corpus that shaped the `xlsx`
reader, and it would almost certainly find things reading cannot.
