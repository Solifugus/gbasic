# Design laboratory — Recipe 1: Sales Decline Investigation

Status: **Design laboratory.** Not user documentation. The libraries this
argues for do not exist.

This is the first recipe called for by
[automation_reasoning_design.md](automation_reasoning_design.md) §26, written
in the structure §16 sets out. Its purpose is only half to solve the scenario;
the other half is to discover what the API wants to become.

**It is executable.** `examples/automation_lab/01_sales_decline.bas` runs on
today's gBASIC with no new libraries, its output is a committed golden, and
`tests/run_automation_lab.sh` asserts the things that make the experiment mean
what this page says it means. The design lessons below are therefore
*measurements*, not predictions — which matters, because the most important one
contradicts the design document.

---

## Business problem

Revenue is down this month. Somebody needs to find out where, and why, and
whether it is worth doing anything about.

Today a human opens a dashboard and starts slicing: by region, then by store,
then by category, until a number looks bad enough to talk about. §11 of the
design document proposes that the system do this automatically, and shows the
output it imagines:

```text
Revenue declined 8.1%.
67% of the decline is attributable to:
    Northeast -> Syracuse stores -> Outdoor category -> Product family X
```

## Business objective

Direct attention. Nothing here decides anything or changes anything — the point
is to hand a manager a shorter list than "everything".

## Available data

Daily revenue by region, store and category, over two periods. Internal only;
no external context in this recipe.

## Insight process

Compute the total change, then for each dimension compute how much of that
change happened at each level, take the largest, and drill into it. Repeat.

## Decision process

None. Recipe 1 stops at the finding, deliberately — §7 says the insight layer
observes and reasons and does not alter the business.

## Automation policy

Report only.

## Human role

All of it, at this stage. The output is an argument presented to a person.

---

## What was actually built

The experiment runs the same decomposition over **two** populations:

- **Run A** — a real 45% collapse planted in one known cell
  (Northeast → Northeast-2 → Outdoor).
- **Run B** — nothing planted anywhere. Lognormal noise only.

The second run is the entire experiment. A decomposition that is only ever
pointed at data with a known answer will always look like it works.

---

## Expected findings, and what actually happened

Run A recovers the planted cell exactly:

```text
revenue  baseline 2040764   current 2003269
change   -37495  (-1.8%)

  by region:
    Northeast        change -30984        = 82.6% of the total change
    ...
  CONCLUSION: Northeast -> Northeast-2 -> Outdoor
```

Run B, over data in which **nothing happened at all**:

```text
revenue  baseline 2047374   current 2010812
change   -36562  (-1.8%)

  by region:
    Southeast        change -29360        = 80.3% of the total change
    ...
  CONCLUSION: Southeast -> Southeast-2 -> Electronics
```

Same headline decline, to the tenth of a percent. Same confident three-level
chain. Same shape of evidence — 80.3% against 82.6%, 51.2% against 57.3%.

**One of those is a real cause and the other is nothing, and the output gives
the reader no way to tell.** §11's proposal, implemented directly, produces the
second one just as readily as the first, and phrases it just as confidently.

---

## Failure and edge cases

### A decomposition is a search, and a search always returns a winner

This is not a bug in the implementation. It is what drill-down *is*. With four
regions, three stores each and five categories there are 60 leaf cells; take the
most extreme of 60 draws and it will look extreme, because that is what "most
extreme of 60" means.

The design document's §11 example — *"67% of the decline is attributable to…"* —
is the output of this search with no statement of how wide the search was.

### The fix, and where it comes from

The null model is **available from the data itself**: the other 59 cells are a
sample of ordinary movement. Against that null:

| | run A (real cause) | run B (noise) |
|---|---|---|
| ordinary cell change | mean −625, sd 4703 | mean −609, sd 4545 |
| the winning cell | **−16835 (z = −3.45)** | **−11412 (z = −2.38)** |

The two separate. No new data source, no external baseline, no second library —
the siblings are the reference distribution.

### The threshold is not a constant

The cut that separates those two is **not** a number a library may choose. A
`z = 3` that is remarkable across four regions is unremarkable across two
hundred product families.

> **Superseded while implementing §13.** This recipe used `sqrt(2 ln n)` — the
> *expected* maximum of *n* draws. That turned out to be far too weak: 6 of 13
> pure-noise seeds cleared it. `insight.explain_change` uses a family-wise
> quantile instead — over a **leave-one-out** spread, with a *t* threshold,
> after Recipe 6 found that standardising a cell against a spread including
> itself caps `max|z|` at `(n-1)/sqrt(n)`. `examples/automation_lab/02_explain_change.bas`
> reruns this same investigation through the current library. The recipe's conclusions are unchanged —
> the planted cell clears either cut and the noise cell clears neither — but
> the threshold shown in this page's output is the weaker one.

So a decomposition **cannot set this cut unless it knows how wide its own
search was** — which makes search width part of the result, not an
implementation detail.

### Contribution shares do not partition

In run A the region shares read −22.6%, −3.7%, 43.7%, 82.6%. They sum to 100%
only because two of them are negative. **Among the regions that actually
declined, the shares sum to 126%.**

That is arithmetically correct and rhetorically disastrous. "Northeast is 82.6%
of the decline" invites *most of it was Northeast*, when Midwest was
independently 43.7% of it and the difference was made up by Southeast rising.
The larger the offsetting movements, the more the shares inflate — and in the
limit, where a big riser cancels a big faller, shares diverge without bound
while the net change approaches zero.

---

## Design lessons

These are the reason the recipe exists. Each is proposed as an amendment to
[automation_reasoning_design.md](automation_reasoning_design.md).

**L1 — §11 needs a null model, and it is the load-bearing part.**
Automatic decomposition is the document's most attractive proposal and, as
specified, its most dangerous. It should not be built without one. Measured
above: real cause and pure noise produce the same headline and the same chain.

**L2 — search width belongs in the result.**
`explain_change` must report how many cells it examined, because the
significance cut is a function of that number. A Finding that omits it cannot
be judged, and cannot be compared with a Finding from a narrower search.

**L3 — the reference distribution is the sibling cells.**
No new capability is required for L1. This is cheap, and it is the pattern to
generalise: *what does ordinary look like here?* is answerable from the same
data that raised the question.

**L4 — report the gross alongside the net, and refuse the share when they
diverge.** A contribution share is only meaningful when the net change is large
relative to the gross movement behind it. When it is not, the honest output is
the signed contributions and a statement that no share is reportable — not a
percentage that happens to be computable.

**L5 — "confidence" is currently three different quantities.**
The document uses one word for: statistical confidence in an estimate (§10),
plausibility of a causal explanation (§11's `confidence .91`), and a
decision-level confidence that §25 compares against `.95`. These do not share a
scale and must not share a threshold. They want three names. This one was found
by reading, not by running, but the experiment is what made it matter: the z
above is the first kind, and nothing in the proposed Finding says so.

**L6 — materiality cannot live where §10 puts it.**
The experiment can say a cell is *statistically unusual* with no business
context at all. It cannot say the −1.8% *matters* without an objective, and
objectives are `decision.bas`'s input. §10 makes materiality a property of a
Finding, which forces `insight.bas` to know the business's goals and breaks the
§7 separation. Either materiality moves to the decision layer, or the insight
layer takes an explicit policy argument and its dependence becomes visible.

**L7 — what the library must absorb.**
Of the experiment's ~200 lines, the decomposition is about 60 and the rest is
grouping, distinct-value extraction and filtered totals — all of which
`frame.summarize` already does. `insight.explain_change` should take a frame, a
measure column, a period column and an ordered list of dimensions. The
boilerplate is the API's job.

---

## What this recipe did not test

It uses one measure, one period comparison, three dimensions and a single
planted cause. It says nothing about several causes at once, a cause that moves
between periods, a dimension with hundreds of levels, or seasonality — under
which the "ordinary movement" null is wrong, because ordinary movement is not
stationary. Recipe 1 deliberately assumed it was.
