# Design laboratory — Recipe 4: A high-cardinality dimension

Status: **Design laboratory**, and the correction it produced is built
(**R15**, `search.detectable` in `stdlib/insight.bas`, asserted in
`tests/run_insight.sh`).

This recipe was written to confirm a prediction and refuted it. The depth plan
said:

> **4. A high-cardinality dimension** — search width dominates; almost nothing
> should survive

R1 made the search width a first-class part of every Finding on exactly that
reasoning: a z remarkable across four regions is unremarkable across two hundred
product families. The reasoning is sound. Its practical weight was
overestimated by a wide margin.

Executable: `examples/automation_lab/11_a_high_cardinality_dimension.bas`.

---

## The experiment

One business of **fixed total size** — the same money, the same 12,000
transactions per period — cut three ways: 24 cells, 240, then 1,200. Revenue is
lognormal with a wide sigma, which is what transaction amounts look like and
what makes a thin cell unruly. One cell loses 75% of its revenue in the planted
runs, identically at every cut.

The only thing that changes between runs is how much of the business is left in
each cell.

## What it found

| cells | per cell | threshold | quiet run | planted z | found? |
|---|---|---|---|---|---|
| 24 | 500 | 3.49 | 0 cleared | **−14.64** | yes |
| 240 | 50 | 3.77 | 0 cleared | −5.19 | yes |
| 1,200 | 10 | 4.11 | 1 cleared | **−3.34** | **no** — S838 cleared instead |

**A fiftyfold wider search buys an 18% higher bar.** `sqrt(2 ln n)` grows about
as slowly as anything in statistics; the Bonferroni correction was never going
to dominate. What collapses is the planted cell's z, and it collapses because
the cell has almost nothing left in it.

The last row is the complete failure mode: the real cause is not reported, and a
different cell is reported in its place.

## The number the library could not say

| cells | a typical cell bills | smallest detectable change |
|---|---|---|
| 24 | 635,515 | 113,554 (18% of a cell) |
| 240 | 65,011 | 34,303 (53% of a cell) |
| 1,200 | 12,349 | 18,096 (**147% of a cell**) |

At the finest cut the smallest change that could clear the bar is **larger than
a typical cell's entire revenue**. No decline in such a cell, however complete,
could ever be reported — the cell could go to zero and the answer would still be
*within ordinary variation*, in exactly the words the library uses when a
business is healthy.

That indistinguishability is the defect, and it is a reporting defect rather
than a statistical one. The verdicts were all correct.

## The correction

**R15 — a Finding states the smallest change it could have found.**
`search.detectable` carries the change in the units of the business, a typical
cell's baseline, and the ratio. It is reported **whether or not anything
cleared**, because *nothing cleared* is precisely when a reader cannot otherwise
tell an incapable search from a healthy business.

It is a falsifiable number rather than a formula echoing itself, and the tests
treat it that way: at a cut whose stated bar is 94% of a cell, a 50% collapse
must not clear, a total collapse must, and the stated bar must lie between them.
Perturbing the computation — dropping the threshold factor, so the bar is
reported roughly four times too small — fails both the difference tier and the
bracket.

## Where this leaves the three depth recipes

Recipes 2, 3 and 4 together give a picture the design did not have. **Three
separate things degrade the cross-sectional test, and only one of them was in
the original architecture:**

| what degrades it | recipe | correction | kind |
|---|---|---|---|
| multiplicity of the search | 1 | R1, the threshold | priced |
| contamination of the reference | 3, 2 | R13, R14 | refused / declared |
| lack of support | 4 | R15 | reported |

Multiplicity — the one the architecture was built around — turns out to be the
mild one.

## What is still open

- **No refusal, deliberately.** A search whose detectable change exceeds a whole
  cell is useless, and it would be easy to refuse it. But the same number is a
  legitimate answer when the caller wants confirmation that nothing large
  happened, and there is no threshold on the ratio that is right in both cases.
  Reporting it is defensible; refusing at an invented cut is not.
- **`typical_cell` is a median baseline**, which is a summary of a distribution
  that may be very skewed. A business whose cells differ by orders of magnitude
  has no typical cell, and this figure will quietly imply that it does.
- **Nothing downstream reads it.** `decision.evaluate` does not yet ask whether
  the Finding it is sizing off could have found anything, and R9's neighbourhood
  is where that question belongs.
