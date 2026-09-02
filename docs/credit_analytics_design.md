# Credit analytics — design

Status: **Partial** (Phase 3, second increment)

Deferred from `docs/lending_design.md` §8 with a reason that has now been
discharged: *"these are about populations, and they want the fake-data
library's planted-anomaly work first, so they can be tested against a portfolio
with known-bad accounts rather than six hand-written loans."* `fake.plant`
shipped 2026-08-31.

---

## 1. What this is for

`lending` answers questions about **one** loan: what is the balance, what does
it take to pay off, what happened and why. Credit analytics answers questions
about a **portfolio**, and they are different questions:

- Is this year's lending worse than last year's? (**vintage curves**)
- Of the accounts 30 days late in March, how many were 60 days late in April?
  (**roll rates**)
- Where did the whole book go over the quarter? (**migration**)
- What did we lose, and what came back? (**charge-off and recovery**)

None of these is derivable from a single loan, and none is an arithmetic
question about balances. They are questions about **states over time**, which
is why the state has to be defined before any of them can be computed.

---

## 2. The input is a status table, not a list of loans

The natural-looking design takes `[{ loan:, events: }]` and derives everything
from `lending`. It is the wrong one, for two reasons.

**Cost.** `lending.apply` is a fold over events, deliberately, so a balance is
explainable by replay. A vintage curve needs the state of every loan at every
month-end: for a 5,000-loan book observed over 36 months that is 180,000 folds,
and `lending.entries` is already O(n²) in events by design.

**Provenance.** Real portfolio data does not arrive as loans and events. It
arrives as a **monthly performance record per loan** — which is exactly the
shape Fannie Mae's and Freddie Mac's published single-family datasets take, and
the shape any servicer's extract takes. A library that can only read our own
`lending` loans cannot be pointed at a real book.

So the input is a table:

```basic
{ id:, cohort:, as_of:, status:, balance: }
```

`cohort` is the origination period (any value that compares and sorts — a date,
or the text "2026-Q1"). `status` is a declared bucket, below. One row per loan
per observation date.

`credit.observe(loans, dates, method)` is the **bridge**: it drives `lending`
over a list of loans and returns that table, so our own machinery is a producer
and not a special case. Analytics never call `lending` directly.

---

## 3. Delinquency is declared, and the industry disagrees about it

`docs/lending_design.md` §2 already says whether a loan is 30 days past due is
contractual, not arithmetic. Populations make that sharper, because **two
servicers report different delinquency for identical loans** and both are
correct under their own convention:

- **MBA method** — a loan is 30 days delinquent the day after it misses one
  payment. The count is *payments missed*.
- **OTS method** — a loan is 30 days delinquent when the oldest unpaid
  instalment is 30 days old. The count is *days past due*.

On a monthly loan the two differ by roughly a month for the whole life of the
delinquency, so a book reported one way is not comparable with a book reported
the other. `method` is therefore **required** and never inferred, the same rule
`lending` set for accrual basis, waterfall and day count.

Buckets are declared too, and default to the conventional ladder:

```
current, dpd_30, dpd_60, dpd_90, dpd_120_plus, paid_off, charged_off
```

## 4. Attrition is a state, not a hole — the load-bearing decision

The commonest defect in roll-rate work is a **plausible improvement**. Take the
accounts 30 days late in March and ask where they were in April. Some paid off.
Some charged off. Some are no longer in the extract at all.

If a loan that left is simply absent from the numerator, the roll rate falls
and the book looks like it is curing. If it is absent from the *denominator*
too, the rate is computed over a population that never existed. Either way the
answer is an ordinary-looking percentage that is wrong, and nothing raises.

So `paid_off` and `charged_off` are **buckets in the matrix**, and they are
**absorbing**: nothing rolls out of them. A loan present at *t* and missing at
*t+1* is not silently dropped — it is reported as `unobserved`, which is its
own row, because "we stopped seeing it" is a fact about the data and not about
the borrower.

That gives the invariant this library rests on, the counterpart of the
accounting equation in `docs/accounting_design.md`:

> **Every loan observed at *t* is accounted for at *t+1*.** The migration
> matrix's row totals plus `unobserved` must equal the starting population
> exactly, bucket by bucket.

Like the balance-sheet identity, it is never *enforced* anywhere. It falls out
of correct bucketing, which is exactly what makes it a good test.

---

## 5. Vintage is months-on-book, never calendar month

A vintage curve plots a cohort's cumulative bad rate against **age**, so that
loans made in 2024 and 2026 can be compared at the same point in their lives.
Indexed by calendar month instead, every cohort's curve starts at a different
age, the averages mix cohorts, and the result is a smooth, well-behaved,
meaningless line. It is the single most common mistake in this area and it does
not look like a mistake.

`credit.vintage(table, spec)` returns one series per cohort, indexed from 0 at
origination, with the cohort's size at each age — because a curve computed over
a shrinking denominator says something different from one over the original
cohort, and `basis: "original"` vs `"outstanding"` is declared, not assumed.

---

## 6. Charge-off and recovery

A charge-off is a **decision**, not a threshold crossing: a servicer writes the
balance off, and the date it does so is a fact in the record. The library
therefore reads `charged_off` from the status table rather than inferring it
from days past due — inferring it would produce a loss figure the servicer's
own books disagree with.

Recovery is money received *after* charge-off, and it is reported separately
rather than netted, because gross loss and net loss are different numbers used
for different purposes and quietly reporting one as the other is how a loss
rate becomes half of what it should be.

---

## 7. What is refused

Each prevents a plausible wrong number rather than a crash.

| Refused | Because |
|---|---|
| An undeclared delinquency method | §3 — two conventions, a month apart, both correct |
| A status the bucket ladder does not name | A typo'd bucket silently becomes a population of one |
| Two rows for the same loan on the same date | The migration count would be right and the population double |
| A migration between dates that are not both in the table | Comparing March against an empty April reports 100% attrition |
| Rolling *out of* an absorbing bucket | A charged-off loan that cures is a data defect, not a recovery |
| Mixed currencies in one balance column | `money` refuses already; this refuses earlier and by name |

---

## 8. Validation

Self-checking rather than golden, and here forced: **every defect above
produces an ordinary-looking percentage.** A roll rate that drops attrition, a
vintage curve on the wrong index, a matrix that double-counts — all of them
read as a portfolio doing slightly better or worse than expected, and a golden
would record the damaged figure as expected and defend it.

Four tiers carry the load:

1. **Reconciliation** — §4's invariant asserted after every migration: row
   totals plus `unobserved` equal the starting population, bucket by bucket.
   This is the arithmetic tier, and it catches the attrition bug, the
   double-count bug and the dropped-loan bug together.
2. **The planted portfolio** — §8 of the lending design, discharged. Generate a
   clean book, plant a known number of bad accounts into a known cohort with
   `fake.plant`, and require the analytics to report **exactly** that cohort as
   worse, by the planted amount. The planted rows carry no marker, so the
   analytics have to find them by computing.
3. **Difference tiers** — MBA against OTS on the same loans must *differ*, and
   the direction is stated; `original` against `outstanding` basis must differ.
   Asserting a value proves less than asserting the two conventions part
   company where the convention says they should.
4. **The bridge** — `credit.observe` over `lending` loans must produce a table
   whose balances agree with `lending.apply` at the same dates, so the producer
   and the single-loan library cannot drift.

---

## 9. Deferred, with reasons

- **Loss forecasting / CECL** — a model, not a measurement. It wants the
  measurement layer first, which is this.
- **Scorecards and PD models** — statistics, and `stats` already has the
  regression machinery; what is missing is the population, which is this.
  **Discharged 2026-09-01**: `stdlib/scoring.bas` and
  [scoring_design.md](scoring_design.md). Note what it is *not* — it does not
  reimplement the regression, it adds the credit-specific half `stats` has no
  reason to carry: binning by weight of evidence, discrimination measured the
  way lenders measure it, calibration onto a point scale, and stability. Reject
  inference stayed deferred there, for the same reason it is deferred here: every
  method for imputing an outcome onto declined applicants is a modelling
  assumption that changes the answer.
- **Cure definitions beyond bucket-to-current** — a cure that requires three
  consecutive on-time payments is a real convention and a different rule; it
  belongs behind a declared option once someone needs it.
