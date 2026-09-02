# Credit scorecards

Status: **Partial** — `stdlib/scoring.bas` implements §2–§7. §9 is deferred.

Discharges the second item of [credit_analytics_design.md](credit_analytics_design.md) §9.

---

## 1. What this is, and what it is not

`credit` measures what a book has already done. This turns a population into a
**model that ranks risk**, and then into the artefact a credit committee
actually approves: a table of attributes and points.

It is deliberately the *credit-specific half only*. The modelling engine already
exists — `stats.logistic_regression` — and this does not reimplement it. What
`stats` does not have, because none of it is general statistics, is:

- **binning** a predictor and measuring it by **Weight of Evidence** and
  **Information Value**;
- **discrimination** measured the way lenders measure it — AUC, KS, Gini;
- **calibration** onto a point scale, where "20 points doubles the odds" is a
  contract with the business rather than a transformation;
- **stability** monitoring, because a scorecard's failure mode is not being
  wrong on day one but drifting on day four hundred.

The split matters for a practical reason: a lender may already have a model. If
they do, everything here except the fitting still applies to it.

---

## 2. WOE has two sign conventions, and neither is the default

Weight of Evidence for a bin is the log ratio of the two conditional
distributions. There are two spellings in common use:

```
good_bad:  woe = ln( share of goods in this bin / share of bads in this bin )
bad_good:  woe = ln( share of bads  in this bin / share of goods in this bin )
```

**They differ by sign and nothing else.** Every coefficient flips, every point
allocation flips, and the resulting scorecard ranks *perfectly backwards* while
looking entirely normal — the same amount of discrimination, pointed the wrong
way. A model with an AUC of 0.78 built on the wrong orientation is an
excellent instrument for lending to the people who will not pay.

So `orientation` is **declared and never inferred**, the rule `lending` already
sets for accrual basis and `credit` for delinquency method. `good_bad` is the
classic credit convention (higher WOE = better risk); `bad_good` is common in
machine-learning write-ups. Both are correct; only one is yours.

---

## 3. An empty cell is refused, not smoothed

A bin containing no bads gives `ln(x/0)`. A bin containing no goods gives
`ln(0/x)`. Both are infinite, and both are *ordinary* in real data — a
high-quality bin at the top of an income band very often has no defaults in a
twelve-month window.

The library **refuses by name**, saying which bin and which side is empty. It
does not silently add 0.5 to every cell.

That is not squeamishness about a standard technique. Additive smoothing is a
legitimate choice and the library supports it — but it **invents evidence**, and
how much it invents depends on the bin size in a way that is invisible in the
output. A bin with 3 goods and 0 bads, smoothed, claims a WOE it has not earned;
the same bin with 30,000 goods and 0 bads has genuinely learned something. Both
come out as a plausible number. So smoothing is `smoothing: <n>`, declared by
the caller, and its absence is a refusal rather than a default.

---

## 4. Binning must not see what it will be judged against

Bins fitted on the same rows the model is validated on will report
discrimination the model does not have. This library cannot prevent that — it
takes the rows it is given — but it states the rule, and `scoring.fit`
therefore takes bins as **data** rather than deriving them silently inside the
fit. `scoring.bin_numeric` and `scoring.bin_categorical` are separate calls
so that "which rows were these bins cut on" is a question with a visible answer.

---

## 5. Discrimination: three numbers that are not interchangeable

- **AUC** — the probability a randomly chosen bad scores worse than a randomly
  chosen good. Computed by rank (the Mann–Whitney identity), with ties taking
  half credit, so it is exact rather than trapezoidal-approximate.
- **Gini** — `2 * AUC - 1`. Reported because the industry quotes it, not because
  it is separate information.
- **KS** — the largest gap between the cumulative good and bad distributions.
  A *different* quantity: two models can rank AUC one way and KS the other,
  because AUC integrates over the whole range and KS reports a single point.
  Reported separately and never as "the" statistic.

**An AUC below 0.5 means the model is backwards, not weak.** The library reports
`reversed: true` and leaves the value below 0.5 rather than flipping it, because
flipping silently converts the single most consequential error in scorecard
work into a mediocre-looking result. A 0.32 that should be reported as 0.32 and
investigated becomes a 0.68 that gets deployed.

---

## 6. Points: a scale is a contract, and the direction is enforced

The standard scaling is linear in the log odds:

```
factor = pdo / ln(2)
offset = base_score - factor * ln(base_odds)
score  = offset + factor * ln(odds)
```

`pdo` is *points to double the odds* — the number the business actually agrees
to ("650 means 20:1, and every 20 points doubles it"). Both `base_score`,
`base_odds` and `pdo` are required; there is no house default, because a default
here silently redefines every cut-off downstream.

**Higher score always means lower risk.** The library enforces that direction
rather than inheriting it from coefficient signs, and says so in the result. A
scorecard whose points run the wrong way is the §5 failure again, one level
further along, and it is invisible on any single case.

Points are reported **per attribute** as well as in total, because that is the
artefact — a scorecard nobody can read attribute by attribute cannot be approved
by a credit committee, explained to a declined applicant, or challenged by a
regulator.

---

## 7. Stability

`psi(expected, actual)` — the Population Stability Index over score bands. The
one number that says *the model has not changed but the applicants have*. It is
included here rather than in `stats` because the banding convention and the
customary thresholds are credit practice, not statistics.

The thresholds (0.10 "look", 0.25 "act") are **rules of thumb and are labelled
as such** in the result rather than returned as a verdict.

---

## 8. Validation

Every defect this library can produce is a plausible number, so the suite is
self-checking rather than golden, and each claim is asserted as the thing that
must be *true*:

- **WOE and IV against values computed outside gBASIC**, by hand from the bin
  counts.
- **Orientation is a difference**: the same data under both conventions must
  give WOE of equal magnitude and opposite sign, and scorecards that rank in
  opposite order. A suite that cannot tell them apart is not testing the
  declaration.
- **AUC against a brute-force pair count** — every good/bad pair compared
  directly, which is the definition, checked against the rank formula the
  library actually uses. Two implementations, not one called twice.
- **A perfect separator scores 1.0, a coin flip 0.5, and a reversed model below
  0.5 without being flipped.**
- **The point scale is inverted back**: recovering `ln(odds)` from a score must
  return the odds that produced it, and `pdo` more points must exactly double
  the odds.
- **Direction**: a fitted scorecard must give the known-bad population lower
  mean points than the known-good one — the end-to-end statement of §6, and the
  one a sign error cannot survive.
- **Refusals with controls**: each beside its nearest legal neighbour.

---

## 9. Deferred, with reasons

- **Reject inference** — the applicants who were declined have no outcome, and
  every method for imputing one (parcelling, augmentation, fuzzy) is a
  *modelling assumption* that changes the answer. It belongs behind a declared
  choice, and it wants a real rejected population to test against.
- **Optimal / monotonic binning** — a search, not a calculation. The
  hand-specified cuts here are what a credit analyst produces anyway; an
  optimiser is a convenience over that, not a prerequisite.
- **Segmentation** — separate scorecards per population slice, plus the rules
  for routing between them.
- **Regulatory adverse-action reason codes** — jurisdiction policy, ruled out
  of the core library for the same reason APR is
  ([lending_design.md](lending_design.md) §8).
