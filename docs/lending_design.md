# Lending and deposits design

**Status:** Partial — `stdlib/lending.bas` (`tests/run_lending.sh`) and
`stdlib/deposits.bas` (`tests/run_deposits.sh`) both ship the first increment.
Shared simple interest was pushed down into `finance.accrue` as §7 said it
should be. ARM machinery, credit analytics and everything in §8 are not built.

**Scope:** `stdlib/lending.bas` and `stdlib/deposits.bas` — loan definition,
amortization, servicing, payoff, underwriting ratios, and deposit interest.
Phase 3 of the
[finance and business platform proposal](gbasic_finance_business_platform_proposal.md),
reordered after accounting on 2026-08-30.

Builds on `finance` (Phase 1: the five solvers, day counts, rate conversions,
`schedule`) and `accounting` (Phase 2: the ledger these events post into).

---

## 1. What Phase 3 is for

Phase 1 answers *what is the payment*. That is a formula, and `finance.pmt`
already gives it. **Phase 3 answers what happens next**, which is not a
formula: a borrower pays late, pays extra, pays part, stops paying; a rate
changes; a fee is added; the loan is paid off on the 14th of a month. Every one
of those is a decision about *policy*, and every one moves the balance.

That is why this is a library and not a function. The arithmetic is small; the
conventions are the whole subject.

---

## 2. The conventions that must be declared

Each of these changes the answer, none has a dominant default, and a guess is
silently wrong somewhere. Following the rule `finance` already set for period
rates, **the loan declares them and the library never assumes**.

### Accrual basis

**Amortized** (scheduled): interest for a period is the scheduled interest,
whatever day the payment arrives. **Daily simple interest**: interest accrues
on the actual outstanding balance for the actual number of days.

They are different loans. Most US mortgages are amortized; most US auto loans
are daily simple interest. Paying five days early on a daily-simple loan saves
five days of interest and on an amortized loan saves nothing — and a servicing
engine that picks the wrong one produces a balance that looks entirely ordinary
and is wrong by a few dollars a month, compounding.

### Payment waterfall

A payment that does not cover everything due is applied in an order: fees,
interest, principal — or interest, principal, fees, or others. The order
changes the balance, the interest accrued next period, and whether the loan is
reported delinquent.

### Day count

`finance.year_fraction` already offers Actual/360, Actual/365, Actual/Actual
and 30/360, and already refuses to default. A loan names one.

### Rounding, and where the residual goes

`finance.schedule` already adjusts the **final** payment so the balance lands
exactly on zero. Servicing needs the mid-life rule too: each period rounds to
whole minor units, and the accumulated residual is carried, not dropped.
`money` is exact, so this is a policy choice rather than a numerical necessity —
which is exactly why it has to be written down.

### Delinquency

Whether a loan is 30 days past due is a **contractual** question (the oldest
unpaid due date) rather than an arithmetic one. Two loans with identical
balances can differ. The rule is declared, not derived.

---

## 3. What is refused

Each prevents a plausible wrong number rather than a crash:

| Refused | Because |
|---|---|
| A payment larger than the payoff | Overpaying is a refund, not a negative balance |
| Payment below the period's interest, without explicit `negative_amortization: true` | The balance grows silently, which is a product, not an accident |
| A rate change dated before the loan opens | It would rewrite history nobody agreed to |
| A schedule whose parts do not reconcile to the principal | The invariant this library rests on |
| Mixed currencies | `money` refuses already; the loan refuses earlier and by name |
| An unnamed accrual basis, waterfall or day count | §2 — the whole point |

---

## 4. Shape

A loan is a record, and servicing is a fold over events:

```
loan  = { principal, rate, term, opened, basis, waterfall, day_count, rounding }
event = { on, kind: "payment"|"fee"|"rate_change"|"advance", amount }
state = lending.apply(loan, events)
      -> { balance, accrued, paid_principal, paid_interest, paid_fees,
           last_accrued_to, delinquent_days, history }
```

**`apply` is a pure function of the loan and its event list** — a fold, not an
incremental step (decided 2026-08-31).

The alternative is how servicing actually runs: one event at a time, carrying
state forward. **The fold is chosen for auditability, which in lending is not a
nice-to-have.** State is derived from the record of what happened, so *why is
this balance what it is* is always answerable by replaying it. With stored
incremental state the number IS the answer, and if it is ever wrong there is
nothing to reconstruct it from. In a dispute that difference is the whole
question.

The cost is recomputation, and it is free at the scale that matters first: a
360-payment loan folds in microseconds. It bites only on a portfolio, and the
remedy is an **addition rather than a redesign** — `apply(loan, events,
from_state)` is a fold with a starting value, and the audit trail survives
because the checkpoint is itself derivable.

`history` is therefore **opt-in**: returning it always would make every
portfolio scan O(n) in memory per loan, for a field a scan does not read.

(gBASIC makes this cheap either way: a record is a value, so even the
incremental design would have to return new state — `state = lending.post(state,
e)`, exactly as `accounting.post` does. There is no mutable-object ergonomics
to give up.)

`lending.payoff(loan, events, on)` answers what closes it on a date, including
per-diem. `lending.schedule(loan)` is `finance.schedule` with the loan's own
conventions applied.

---

## 5. The accounting boundary — the payoff for doing Phase 2 first

Lending **emits** journal entries; it never posts them. The caller owns the
ledger, so an application can post to its own chart of accounts, batch, or
discard.

```
entries = lending.entries(chart, state, accounts)
```

where `accounts` maps the roles — receivable, interest income, fee income,
cash, allowance — onto the caller's own codes.

**This is also the test that proves the whole thing at once**, the same device
accounting used for `fake`: a loan's entire life, posted to a real ledger,
must leave it balanced, with receivables equal to the outstanding balance. An
unbalanced entry, a phantom account or a cross-currency line is refused where
it is posted, so a loan whose history posts cleanly has *demonstrated* its
arithmetic rather than asserted it.

---

## 6. Underwriting

Small, self-contained, and useful immediately: `lending.ltv`, `.dti`, `.dscr`,
`.payment_to_income`. Each takes money and returns a ratio.

The only decision worth making is what a missing input does. These feed credit
decisions, so an absent income figure must **not** silently become zero — it
returns `unknown`, and a caller who wants a refusal asks for one.

---

## 7. Deposits are a separate library

`stdlib/deposits.bas`, not part of `lending` (decided 2026-08-31, and what the
platform proposal's architecture table already said).

**The vocabularies barely overlap.** `waterfall`, `delinquency`, `payoff` and
per-diem mean nothing to a savings account; `tier`, `crediting schedule`,
`early-withdrawal penalty` and average daily balance mean nothing to a
mortgage. Two sets of concepts in one namespace makes both harder to read.

**And what they genuinely share is not lending's to own.** The overlap is
*interest accrued on a balance over a day count*, which is `finance`'s job —
`year_fraction` already lives there. Putting shared accrual in `lending` would
force `deposits` to depend on it, which is backwards: deposits do not borrow.

```
finance    — accrual, day counts, rate conversions   (shared)
  |- lending   — schedules, servicing, payoff, underwriting
  |- deposits  — balance methods, crediting, certificates, tiers
```

**Lending ships first**, and any shared accrual helper is pushed *down* into
`finance` once lending has proved what it needs — so the shared piece is
designed from one real caller rather than two hypothetical ones.

The deposit subject, for when it comes: balance methods (daily, average daily,
minimum) which give different interest on the same activity; compounding and
crediting as **separate** schedules, since interest may compound daily and be
credited monthly and conflating them is the common error; certificates whose
early-withdrawal penalty may exceed the interest earned and so reduce
principal, which must not be silently clamped; and tiered rates, where whether
the rate applies to the whole balance or only the portion in the tier is a
product decision. Deposit beta, decay and cost of funds are portfolio
analytics rather than account mechanics, and are deferred with §8.

## 8. Deferred, with reasons

- **Adjustable-rate products** (index, margin, caps, floors, reset schedules) —
  a rate change event is in scope; the *index* machinery is a product family of
  its own and wants real index data.
- **Credit analytics** — vintage curves, roll rates, migration, charge-off and
  recovery. These are about *populations*, and they want the fake-data
  library's planted-anomaly work first so they can be tested against a
  portfolio with known-bad accounts rather than six hand-written loans.
- **APR** — jurisdiction policy, ruled out of core finance for the same reason
  (`docs/finance_design.md` §7). The math is an IRR over fee-inclusive flows;
  *which fees count* belongs in a versioned jurisdiction package.
- **Leases** — a different accounting treatment, not just a different schedule.
- **Escrow, insurance, and servicing transfer** — real, and each is its own
  ruling.

---

## 9. Validation

- **The schedule reconciles arithmetically**: principal parts sum to the loan,
  the final balance is exactly zero. Already `finance`'s standard; extended to
  a serviced loan with irregular payments.
- **Basis is proved by difference**: the same loan and the same payments under
  amortized and daily-simple accrual must give *different* balances, and the
  difference must be the days. A test that cannot tell them apart is not
  testing the basis.
- **The waterfall likewise**: the same partial payment under two orders must
  land differently, asserted per component.
- **The whole life posts to a ledger** (§5), balanced, with receivables equal
  to the outstanding balance.
- **Against external references**: a full amortization schedule checked against
  values computed outside gBASIC, as Phase 1 did.
- **Refusals with controls**: each refusal beside its nearest legal neighbour.

---

## 10. First increment

`lending` only — deposits follow as their own library (§7). Deliberately
smaller than the workstream:

1. The loan record and its declared conventions (§2).
2. `lending.schedule` over them.
3. `lending.apply` with payment, fee and rate-change events, both accrual bases
   and at least two waterfalls, with `history` opt-in.
4. `lending.payoff` with per-diem.
5. Underwriting ratios (§6).
6. `lending.entries` and the ledger test (§5).

ARM machinery, credit analytics, deposits and everything in §8 follow later,
each with its own review.
