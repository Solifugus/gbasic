# Core finance design

**Status:** Proposal
**Scope:** `stdlib/finance.bas` — the time value of money, cash-flow appraisal,
schedules and depreciation. This is the Phase 1 design the
[finance and business platform proposal](gbasic_finance_business_platform_proposal.md)
asks for. It settles the conventions everything downstream inherits, and
nothing else: `lending`, `deposits` and the rest are out of scope here.

**Decision taken 2026-08-29:** the public API moves to **Excel argument order**.
§2 records what that costs and why it is still right.

---

## 1. What exists today

Measured, not remembered — `stdlib/finance.bas` is 302 lines with ten public
functions:

| Function | Signature today | Returns |
|---|---|---|
| `pmt` | `pmt(principal, r, n)` | money |
| `pv` | `pv(pmt_amount, r, n)` | money |
| `fv` | `fv(amount, r, n)` | money |
| `nper` | `nper(principal, payment, r)` | number |
| `npv` | `npv(r, flows)` | money |
| `irr` | `irr(outlay, flows)` | number |
| `schedule` | `schedule(principal, r, n)` | array of records |
| `sln` | `sln(cost, salvage, life)` | money |
| `syd` | `syd(cost, salvage, life, period)` | money |
| `ddb` | `ddb(cost, salvage, life, period)` | money |

Three properties are already right and this design does not disturb them.

**The values agree with Excel.** Checked against the closed forms:

| | gBASIC | Excel |
|---|---|---|
| `pmt` 250 000 at 0.05/12 over 360 | `-1342.05` | `-1342.05` |
| `pv` of 100 for 10 at 0.05 | `-772.17` | `-772.17` |
| `fv` of 1000 for 10 at 0.05 | `1628.89` | `1628.89` |

**The sign convention is already Excel's** — money received is positive, money
paid is negative — and it is stated in the library's own header. A borrower's
`pmt` is negative because the payment leaves them.

**Amounts are money and rates are numbers**, enforced rather than documented:
`finance.pmt` *raises* when handed a plain number for the principal. Rates are
per period; annualisation is the caller's arithmetic, deliberately.

**There is no design document for any of this.** `money` has one; finance never
did. That is the actual gap this file closes, and it is why the conventions
below are written down as decisions rather than assumed.

---

## 2. Ruling: Excel argument order

**Decision: `finance.*` adopts Excel's argument order for every function that
has an Excel counterpart.**

### What is actually changing

Only the **order**. The equation, the signs, the money/number split and the
returned values are unchanged — which is what makes this affordable.

| Function | Today | Excel, and the new order |
|---|---|---|
| `pmt` | `pmt(principal, r, n)` | `pmt(rate, nper, pv)` |
| `pv` | `pv(pmt_amount, r, n)` | `pv(rate, nper, pmt)` |
| `fv` | `fv(amount, r, n)` | `fv(rate, nper, pmt, pv)` — see §7 |
| `nper` | `nper(principal, payment, r)` | `nper(rate, pmt, pv)` |
| `npv` | `npv(r, flows)` | **already Excel's** |
| `irr` | `irr(outlay, flows)` | `irr(values)` — see §7 |
| `sln` | `sln(cost, salvage, life)` | **already Excel's** |
| `syd` | `syd(cost, salvage, life, period)` | **already Excel's** |
| `ddb` | `ddb(cost, salvage, life, period)` | already Excel's, minus `factor` |
| `schedule` | `schedule(principal, r, n)` | `schedule(rate, nper, pv)` — no Excel counterpart; follows the family |

### Why

The people who will check these answers check them in a spreadsheet. Every
reference example, textbook exercise and internal worksheet they own is written
`PMT(rate, nper, pv)`. An order that reads correctly to a C programmer and
backwards to a finance professional taxes the audience the library exists for,
on every single call, forever.

There is also an inconsistency to resolve regardless. The current family is
*almost* uniform — `pmt`, `pv`, `fv` and `schedule` all take the amount first —
but `npv(r, flows)` puts the rate first, because that is Excel's order and it
was written later. So the choice is not "keep a clean convention or adopt
Excel"; it is which of two conventions already in the file wins. Excel's does.

### What it costs, stated plainly

**This is a breaking change to a shipped library**, and the argument for making
it now is that the blast radius is small and entirely inside this repository:

- `tests/finance_test.bas` — 15 call sites
- `examples/money_cookbook/06_a_loan.bas` — 3
- `examples/money_cookbook/07_amortization.bas` — 1
- `examples/money_cookbook/08_is_it_worth_it.bas` — 2
- `docs/reference.md`, `docs/money_cookbook.md`, `README.md`, `docs/ai/COOKBOOK.md` — prose and examples

No consumer outside this repository is known. The cookbook goldens move with
the recipes, and `run_money_cookbook.sh` fails until the page and the code
agree again, so the change cannot be half-made.

**The window closes with Phase 2.** Once `lending.bas` and real applications
call these, the same edit stops being seven files and becomes an ecosystem.
Doing it at the last moment when it is still cheap is the whole reason it is
being done at all.

### Compatibility

No deprecation shim. A silently-reordered call is the worst possible failure
here — `pmt(0.005, 360, principal)` under the old order would raise on the
first argument's type, but `nper` and `irr` take two amounts and could silently
compute something plausible. Every changed function therefore **type-checks its
new first argument**, so a stale call fails loudly at its own line rather than
returning a wrong number. This is a pre-1.0 language; a clean break with a
located error beats a shim nobody removes.

---

## 3. One equation

Every time-value function solves the same relation for a different unknown:

```
pv * (1+r)^n  +  pmt * ((1+r)^n - 1) / r * (1 + r*t)  +  fv  =  0
```

where `r` is the rate **for one period**, `n` the number of periods, and `t` is
`0` for end-of-period payments and `1` for beginning-of-period. The `r = 0`
case is computed exactly rather than left to the limit.

The current library implements this with `t = 0` and `fv = 0` throughout.
Phase 1 generalises it; §7 gives the surface.

**Documenting the equation is itself a deliverable.** Every disagreement about
sign or timing reduces to which term the caller thought they were setting.

---

## 4. Sign convention

**Money received is positive; money paid is negative.** Unchanged, already
Excel's, and now stated as a ruling rather than a comment.

Borrowing 250 000 and repaying monthly is a **positive** `pv` and a
**negative** `pmt`. A depreciation charge is positive because it is a magnitude,
not a cash flow — the one deliberate exception, and it matches `SLN`/`SYD`/`DDB`.

---

## 5. Money and numbers

| Quantity | Type | Why |
|---|---|---|
| amounts, payments, balances, flows | `money` | exact, carries currency |
| rates, ratios, counts of periods | `number` | dimensionless |
| `irr`, `rate` results | `number` | a rate is a ratio |
| `nper` result | `number` | usually fractional |

Rates stay per period. `0.06 / 12` is the caller's arithmetic because
compounding conventions vary by product and jurisdiction, and a library that
guessed would be wrong somewhere without saying so. Phase 1 adds explicit
conversions (§7) so the guess is never needed.

`irr` and the new `rate` compute **in plain numbers**, not money. This is
deliberate: bisection visits rates near −100% where the discount factor is
about 1e-12 and dividing money by it overflows the type. The money matters at
the ends; the search does not.

---

## 6. Optional arguments

**Superseded 2026-08-30 — and this section is kept because the decision it
records was reversed by building the missing capability rather than working
around it.**

When this document was first written, gBASIC had no optional parameters: arity
was strict and `default(value, fallback)` was a *value* helper for
`nothing`/`unknown`, not parameter defaulting. Excel's
`PMT(rate, nper, pv, [fv], [type])` therefore could not be transcribed, and the
design settled on a short form plus a record form.

The language now has **literal default parameters**, so the Excel signature is
writable directly:

```basic
finance.pmt(rate, nper, pv)                      ' the common case
finance.pmt(rate, nper, pv, fv, timing)          ' the full one
```

`finance.pmt(rate, nper, pv, fv = 0, timing = "end")` is the whole
declaration. The record form is **no longer required** and drops to an optional
convenience for the functions that genuinely have many independent inputs —
which, in core finance, may be none of them. `lending.loan({...})` in the
platform proposal's vision is still a record, because a loan really does have a
dozen fields; a `pmt` does not.

This is why the sequencing mattered: had Phase 1 shipped first, its first
deliverable would have been a workaround for a limitation that disappeared a
day later.

The two-form pattern below is retained only where a function's inputs justify
it:

One declaration covers both. The trailing two are what a spreadsheet leaves
out, and they carry the values a spreadsheet assumes:

```
function pmt(rate, nper, pv, fv = 0, timing = "end")
```

```basic
finance.pmt(0.005, 360, principal)
finance.pmt(0.005, 360, principal, balloon, "begin")
```

A record form is worth adding only where a call has enough independently
meaningful inputs that five positional arguments stop being readable — the
platform proposal's API guideline 1. In core finance that is arguably nowhere;
in `lending.loan({...})` it plainly is.

---

## 7. The Phase 1 API

### Time value

| Call | Notes |
|---|---|
| `finance.pmt(rate, nper, pv)` | payment per period |
| `finance.pv(rate, nper, pmt)` | present value of a payment stream |
| `finance.fv(rate, nper, pmt, pv)` | **shape change**, see below |
| `finance.nper(rate, pmt, pv)` | periods; number, usually fractional |
| `finance.rate(nper, pmt, pv)` | **new** — the fifth solver |
| optional tail | `fv = 0`, `timing = "end"` where the equation admits them |

**`fv` changes shape, not just order.** Today `fv(amount, r, n)` is the future
value of a *lump sum*. Excel's `FV(rate, nper, pmt, [pv], [type])` is an
annuity plus an optional lump sum. The Excel shape is adopted, so today's
`fv(c, 0.05, 10)` becomes `fv(0.05, 10, 0, c)`. This is the only function whose
*meaning* moves, and it is called out separately because a reordering
migration would otherwise miss it.

**`rate` is the gap.** Four of the five solvers exist; `rate` does not. It is
the reason Phase 1 exists at all, and it must report convergence rather than
returning a number silently — bisection like `irr`, with the same refusal when
no rate in range satisfies the equation.

### Rate conversion

| Call | Meaning |
|---|---|
| `finance.effective(nominal, periods_per_year)` | nominal → effective annual |
| `finance.nominal(effective, periods_per_year)` | the inverse |
| `finance.periodic(annual, periods_per_year)` | annual → one period |
| `finance.continuous(annual)` / `finance.from_continuous(r)` | continuous compounding |

These exist so "the rate for one period" is never a guess. **APR is
deliberately absent**: its *math* is an IRR over fee-inclusive flows and is
trivial, but *which fees count* is jurisdiction policy (Reg Z here, different
elsewhere). Putting `finance.apr` in a timeless library would be exactly the
mistake the platform proposal's principle 10 warns about. It belongs in a
versioned jurisdiction package, with the fee rules and an effective date.

### Cash flows

| Call | Notes |
|---|---|
| `finance.npv(rate, flows)` | unchanged; flows start at period **1** |
| `finance.irr(values)` | **shape change** — `values[0]` is the period-0 flow, negative, as Excel |
| `finance.xnpv(rate, values, dates)` | **new** — dated flows |
| `finance.xirr(values, dates)` | **new** |

`npv` keeping Excel's period-1 start is deliberate, including the quirk: the
idiom is `outlay + finance.npv(r, rest)`, exactly as in a spreadsheet.

`irr` absorbs the outlay into the array as a negative period-0 flow. Today's
`irr(outlay, flows)` is the readable version of the same thing, but it is not
Excel's and it makes `xirr` — where the dates array must line up with the
values array including period 0 — awkward.

**Multiple IRRs must be reported.** A sign change in the flows more than once
admits more than one root, and returning the first one found is a plausible
percentage someone acts on. `irr` must detect the condition and refuse or warn;
this is a correctness requirement, not a nicety.

### Day count

`finance.year_fraction(from, to, convention)` over `dates.bas`, with
Actual/360, Actual/365 Fixed, Actual/Actual (ISDA) and 30/360 US. Naming the
convention is required — there is no default, because the four disagree by
enough to matter and no one of them is dominant across products.

### Schedules and depreciation

`schedule(rate, nper, pv)` reorders to match the family. Its existing
behaviour — round every period to whole minor units and **adjust the final
payment so the balance lands exactly on zero** — is correct and stays. It is
what lenders do, and it is asserted arithmetically rather than as a golden.

`sln`, `syd` and `ddb` already match Excel's order and stay. `ddb` gains
Excel's optional `factor` as a defaulted parameter.

---

## 8. Migration

One commit, because a half-migration is a wrong-number machine:

1. Reorder the five, reshape `fv` and `irr`, add the type-check on the new
   first argument of each.
2. Update `tests/finance_test.bas` (15 sites) — and *keep the expected values
   unchanged*, since only the order moves. A value that shifts means the
   reorder was done wrong, which makes the existing test file the migration's
   own oracle.
3. Update money cookbook recipes 06, 07 and 08 and re-sync the page;
   `run_money_cookbook.sh` enforces agreement.
4. Update `docs/reference.md`, `README.md`, `docs/ai/COOKBOOK.md`.
5. Add this document's decisions to the reference's finance section.

Point 2 is the load-bearing one. The existing tests pin values verified against
Excel and LibreOffice; if every one of them still passes after the reorder, the
reorder preserved meaning.

---

## 9. Deferred, with reasons

- **APR/APY** — jurisdiction policy; §7.
- **Payment timing beyond `"end"`/`"begin"`** — no third convention is needed
  before a product library asks for one.
- **Growing annuities, perpetuities, deferred annuities** — real, but they are
  additions to a settled equation, not decisions about it.
- **Continuous-time and real-vs-nominal inflation adjustment** — same.
- **A `rate` value type** (a rate that knows its own compounding) — the
  platform proposal raises it, and the honest answer is that record-based APIs
  must be tried first. Its non-goal list says so too.

---

## 10. Validation

Beyond the existing suite:

- **The reorder is verified by unchanged expected values** (§8.2).
- **`rate` is checked against Excel's `RATE`** and by round-tripping: the rate
  recovered from a `pmt` must reproduce that payment.
- **Algebraic invariants**, which no reference can drift out from under:
  `pv` and `fv` are inverses; a schedule's principal parts sum to the loan;
  `npv` at the `irr` is zero.
- **`xirr`/`xnpv` against LibreOffice**, whose values are computed externally
  and committed, following the xlsx fixtures' precedent.
- **Day-count conventions against published examples**, one per convention,
  including a leap year and an end-of-month date, which is where they diverge.
- **Multiple-IRR detection** gets a fixture with a genuine double root — a
  positive-negative-positive flow pattern — and the refusal is asserted.

Reference agreement is evidence, not proof; the invariants are what make it
more than agreeing with ourselves.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| A stale call silently computes a wrong number | Type-check the new first argument; §2 |
| The `fv` reshape is missed in a reorder-only migration | Called out separately in §7 and §8 |
| The record form and short form drift | The short form is *implemented as* the record form |
| `rate` returns a plausible non-converged number | Bisection with an explicit refusal, like `irr` |
| Excel's own conventions are ambiguous | Follow the documented behaviour and name it; never clone an ambiguity silently |
| Phase 2 starts before this settles | This document is the Phase 2 precondition |
