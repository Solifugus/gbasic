# Design laboratory — Recipe 3: A seasonal measure

Status: **Design laboratory**, and both corrections it produced are built
(**R13** and `versus_last_year` in `stdlib/insight.bas` /
`stdlib/reasoning.bas`, asserted in `tests/run_insight.sh`).

Every null this library offers is **cross-sectional**. `siblings` and
`siblings_permuted` both ask *is this cell unlike the others right now*, and
neither looks at time. The provenance of every Finding has recorded the
assumption in as many words —

> "ordinary movement is stationary across cells"

— and nothing anywhere checked it. Design §7 said both nulls were therefore
wrong under seasonality and left the question open. This is that question, run
rather than argued.

Executable: `examples/automation_lab/09_a_seasonal_measure.bas`.

---

## The experiment

Twenty-four cells (four regions × six categories), four periods, twenty days
each. Revenue is lognormal about a level that is the cell's own base times its
category's **seasonal index** for that month. Toys triple into Christmas and
collapse after it; groceries barely move. The index is identical in both years,
which is the generous case — seasonality perfectly stable and therefore
perfectly removable by a year-over-year comparison.

The comparison is **December to January**, which is the most ordinary seasonal
comparison in commerce. Six runs:

| run | comparison | planted | null |
|---|---|---|---|
| A | Dec → Jan | nothing | siblings |
| B | Dec → Jan | 45% collapse in West / Grocery | siblings |
| C | Jan → Jan | the same collapse | siblings |
| D | Jan → Jan | nothing | siblings |
| E | Jan → Jan | the same collapse | siblings_permuted |
| F | Jan → Jan | nothing | siblings_permuted |

A and B are the experiment. C and D exist so that *the library failed* cannot
be confused with *the data holds nothing to find*.

## What it found, which is not what was expected

The expectation was false alarms: a cross-sectional null pointed at a seasonal
measure would flag the high-amplitude cells and produce confident nonsense
about toys. It does not.

| | run A, nothing wrong | run B, a real 45% collapse |
|---|---|---|
| headline | −50.1% | −51.5% |
| net *t* | −4.49 | −4.65 |
| leader | West / Toys, z −3.35 | West / Toys, z −3.33 |
| cells clearing | 0 | 0 |
| the collapsed cell's z | +0.52 | **−0.09** |

The two runs are the same answer, which is Recipe 1's finding in a new
disguise. But the sharper half is the last row: the cell that lost 45% of its
revenue did not merely fail to stand out — **it moved towards the middle**. The
collapse made it look *more* ordinary.

**Seasonality does not cause false alarms here. It causes blindness.** The
mechanism is general and has nothing to do with seasons: a shift shared by
every cell inflates the cross-sectional spread each cell is judged against *in
proportion to itself*, so it raises the detection floor for everything.
Isolated in the simplest possible population — every cell down 40%, nothing
else — the same planted collapse that clears on its own no longer clears. That
is asserted in `tests/insight_test.bas` as a difference between the two runs
rather than as a number, because a number there would pass on a library that
had stopped detecting anything.

## Design lessons

**R13 — a share is refused when the movement is common to the population.**
Without it, run A establishes a 50.1% decline at *t* = −4.49 and reports shares:
a confident attribution of an ordinary January to whichever cell sells the most
toys. That is §11's headline claim, produced from a population with nothing
wrong in it.

The signature is in the data and costs nothing to look for. **22 of 24 cells
moved the same way** — a one-in-thirty-thousand event under a null where a cell
is as likely to rise as to fall. A sign test decides it, and it assumes *less*
than the *t* test beside it already assumes: only direction, never the shape of
how far.

It is **not a seasonality detector.** A real company-wide collapse moves 22 of
24 cells down, and so does a broken feed. What R13 establishes is what R2
establishes one level down — *the decomposition has not located this* — and
which of the three it is lies outside the data.

**`versus_last_year` was declared and not implemented.** §7 had named three
comparisons since the rewrite, `reasoning.comparisons()` returned one, and the
error message beside it listed all three in prose. The remedy for a seasonal
measure could not be spelled. With it, run C — the same data, the same library,
the same threshold — recovers West / Grocery at z −4.91, ranked first, clearing.

**The fix is the comparison, not the null**, which is the opposite of where
this recipe expected to end up. It is also why a third, temporal null is *not*
being added: the capability already existed and was unreachable.

**A third result arrived unasked.** Run D plants nothing and a cell clears
anyway at z 3.59 — the *t* threshold's known miscalibration (§4.3), seen live
rather than in a Monte Carlo. Runs E and F repeat C and D under
`siblings_permuted`: the false one goes away and the true one survives. That is
a confirmation of the permuted null on a population it was never tuned against,
and it is the strongest evidence for it so far, because both halves are
asserted — a threshold that never fires would satisfy the first half alone.

## What R13 cost, and what it changed elsewhere

R13 invalidated the **control** that R2's tier had been resting on. That
control was a population where *every cell really fell*, on the argument that a
library which never reported a share would otherwise pass. Under R13 that
population is exactly the case a share may not be reported for.

The control is now a decline that is really **somewhere**: six of twenty cells
collapse, scattered across regions so no single branch of the decomposition
owns them, and the other fourteen are left alone. Measured across five seeds it
reports shares every time, at *t* −2.76 to −3.17 and sign *p* 0.12 to 0.50.

Finding that band was itself informative. R2 and R13 together admit a narrower
range than either suggests alone: the net must be large enough for a *t* test
on cell means, yet the cells must not have moved together — and a decline
concentrated in three of twenty cells *barely* reaches *t* = 2, because the
concentration that makes a share meaningful is the same concentration that
inflates the standard deviation the aggregate test divides by.

## What is still open

- **Seasonality that is not stable.** The fixture's seasonal index is identical
  in both years, so a year-over-year comparison removes it exactly. A warm
  December leaves a residue, and nothing here measures how much.
- **A measure with a trend rather than a season.** Year-over-year removes a
  season; it does not remove growth, and a growing population moves together
  every period. R13 will fire on it, correctly and unhelpfully.
- **The sign test's resolution at small *n*.** At 20 cells the reachable
  two-sided *p* values near the cut are 0.115, 0.041 and 0.012 — there is no
  room between them. The refusal is coarse where the searches are narrowest.
