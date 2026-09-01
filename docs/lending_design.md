# Lending and deposits design

**Status:** Proposal
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

**`apply` is a pure function of the loan and its event list**, so a servicing
state is reproducible from the record of what happened — which is what makes it
auditable, and what lets a test assert the whole life of a loan rather than one
step.

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

## 7. Deposits

The mirror image, sharing the accrual machinery: interest is **credited** to
the balance rather than amortized away.

- **Balance methods**: daily balance, average daily balance, minimum balance.
  These give different interest on the same account and the same activity,
  which is the deposit-side version of §2's accrual basis.
- **Compounding and crediting are separate**: interest may compound daily and
  be credited monthly. Conflating them is the common error.
- **Certificates**: term, maturity, and an early-withdrawal penalty that can
  exceed interest earned — so a penalty may reduce principal, and the library
  must not clamp that silently.
- **Tiered rates**: the rate depends on the balance, and whether it applies to
  the whole balance or only the portion in the tier is a product decision.

Deferred: deposit beta, decay and attrition modelling, cost of funds — those
are portfolio analytics rather than account mechanics.

---

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

Deliberately smaller than the workstream:

1. The loan record and its declared conventions (§2).
2. `lending.schedule` over them.
3. `lending.apply` with payment, fee and rate-change events, both accrual bases
   and at least two waterfalls.
4. `lending.payoff` with per-diem.
5. Underwriting ratios (§6).
6. Deposits: daily and average-daily balance, compounding separate from
   crediting, and a certificate with an early-withdrawal penalty.
7. `lending.entries` and the ledger test (§5).

ARM machinery, credit analytics and everything in §8 follow later, each with
its own review.
