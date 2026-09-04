# Design laboratory — Recipe 2: Two causes at once

Status: **Design laboratory**, and the correction it produced is built
(**R14** and `max_causes` in `stdlib/insight.bas`, asserted in
`tests/run_insight.sh`).

Recipe 1 established that a drill-down is a search and needs a null. Recipe 6
fixed the null's worst defect — a cell was being standardised against a spread
it was itself part of, which put a hard ceiling on how extreme anything could
look — by leaving the cell out.

Leave-one-out removes a cell from *its own* reference and **nothing else**.
This recipe asks what that costs when more than one thing has gone wrong, which
is the ordinary condition of a business rather than an exotic one.

Executable: `examples/automation_lab/10_two_causes_at_once.bas`.

---

## The experiment

Twenty-four cells, two periods, thirty days each, lognormal revenue. One
**watched** cell — Northeast / Outdoor — collapses 75% in every run, by the
same amount, from the same numbers. The only thing that varies between runs is
how many *other* cells collapsed beside it: cells in other regions and other
categories, with which it has nothing to do.

If the verdict on the watched cell moves, nothing about the watched cell caused
it to.

## What it found

| others broken | watched change | reference mean | reference sd | z | verdict |
|---|---|---|---|---|---|
| 0 | −25,728 | −666 | 6,974 | **−5.70** | found |
| 1 | −25,728 | −1,643 | 8,472 | −3.65 | found |
| 2 | −25,728 | −2,663 | 9,668 | −2.83 | nothing |
| 4 | −25,728 | −4,942 | 12,213 | **−1.86** | nothing |

Both terms of the standardisation move against detection at once: the reference
mean slides *towards* the anomaly, and the reference spread inflates. Four
unrelated cells breaking beside the watched one take two thirds of its z away
and put it below the bar, without a single number in that cell changing.

**The test finds a problem only while it is nearly the only problem.**

Making the problem worse does not make it more findable, either. At a 90%
collapse instead of 75%, the watched cell's z falls from −6.99 to −1.98 across
the same four runs — because the cells contaminating the reference deepen along
with it.

## Two repairs that were measured and failed

Both are recorded because a negative result about a remedy is worth as much as
the finding, and because either would have looked reasonable in a design
document.

**Sequential peeling** — test the most extreme cell, remove it, test the next,
in the manner of a generalized ESD test. It does not help. The *first* test is
the most contaminated one and it is the test that decides whether anything is
reported at all; by the time peeling has removed a competitor, the verdict has
already been taken. Measured at 24 cells with two identical causes, the first
two statistics were −3.10 and −3.05 against a threshold of 3.49 — neither
clears, so nothing is ever peeled.

**A robust median/MAD scale.** Also no. At 24 cells the MAD *itself* rose 28%
between one cause and two, because two extreme values in twenty-four is not a
small fraction of anything. And once the robust statistic's own null threshold
is measured rather than borrowed — 4.18, against the t formula's 3.49 — the
robust z at two causes (−2.99) is *further* from clearing than the ordinary one.
This one is worth dwelling on: it would have passed a review, and it fails for a
reason that is only visible when the threshold is calibrated for the statistic
actually used rather than assumed from a formula.

## The correction

**R14 — how many things may be wrong at once is declared, not assumed.**

What works is excluding the other candidates from the **reference**. Not
blessing them as findings; merely declining to let them define what ordinary
looks like. It restores the statistic to a property of the cell:

| others broken | max_causes | watched z | threshold | verdict |
|---|---|---|---|---|
| 1 | 2 | −5.59 | 4.35 | found |
| 2 | 3 | −5.46 | 4.84 | found |
| 4 | 5 | −5.48 | **6.46** | nothing |

The trimmed z is flat at about −5.5 across all three runs — which is correct,
because the cell did not change. **And the correction is not free.** The bar
rises with what is allowed for, and by five causes in twenty-four cells it has
outrun the evidence.

That last row is deliberately in the golden and is asserted as a requirement. A
recipe showing only successful recoveries would teach that `max_causes` repairs
the problem, when what it actually does is **move** it — from a silent failure
to a priced choice.

`max_causes` defaults to 1, because that is what this library has always
assumed without saying so. Naming it changes no existing answer, and a test
asserts exactly that.

**Above 1 it requires `siblings_permuted`.** Trimming the reference changes the
statistic, and the t quantile is a formula for the untrimmed one — not
approximately right, but a threshold for a different quantity, and it errs
towards reporting causes that are not there. The permuted null takes its
threshold from the statistic actually used, so it follows the trim.

## What this shares with Recipe 3

They are one result seen twice. §4.8 found the cross-sectional null blinded by a
movement shared across *every* cell; §4.9 finds it blinded by a handful of cells
sharing the *same failure*. In both, **the reference population is contaminated
by exactly the thing being looked for**, and leave-one-out addresses only the
special case where the contaminating cell is the cell under test.

## What is still open

- **`max_causes` cannot be inferred, and this recipe does not try.** A caller
  who declares 1 when five things are broken gets silence; one who declares 5
  when one is broken gets a bar it may not clear. Nothing here helps them
  choose, and the honest statement is that the data does not contain the answer.
- **The permuted threshold is estimated, and the estimate is noisy.** At 100
  draws the same population reported thresholds varying by half a unit between
  runs — enough to flip a verdict. Measured at 400 draws it is stable to about
  0.08. The recipe uses 200 and the cost is quadratic in cells times the trim.
- **`strength` still names one cell.** `strength.clearing` now lists all of
  them, but a Finding's headline remains singular, and nothing downstream reads
  the list yet.
