# Design laboratory — Recipe 9: How much evidence is enough?

Status: **Design laboratory**, and the increment it produced is built
(`decision.calibrate`, evidence-grounded sensitivity, `tests/run_decision.sh`).

This turns the loop. Recipe 5 **assumed** a recovery of 0.6 and flagged its
recommendation as sensitive to exactly that figure. Recipe 7 showed where such a
figure comes from, and that measuring it without a control gives the wrong one.
This is the third step: take controlled evidence, produce the assumption, and
decide again.

Executable: `examples/automation_lab/07_how_much_evidence.bas`.

---

## The question has no answer in the abstract

*How much evidence is enough* is unanswerable on its own. It has an answer
relative to **a decision**.

Sending a manager costs 2,000 against a loss of 16,835, so the action pays for
itself at a recovery of **0.119**. That is the break-even, and the whole
question is whether the evidence can put the truth on one side of it.

**Enough is when the interval the evidence supports stops straddling that
point.**

## Two cases, same machinery

**Case A — true incremental effect 0.35, well clear of break-even:**

```text
n = 2    estimate 0.48    95% interval 0.226 to 0.734    DECISIVE
n = 5    estimate 0.44    95% interval 0.324 to 0.556    DECISIVE
n = 12   estimate 0.362   95% interval 0.292 to 0.432    DECISIVE
```

**Case B — true incremental effect 0.15, a hair above it:**

```text
n = 2    estimate 0.28    95% interval 0.026 to 0.534    NOT DECISIVE
n = 5    estimate 0.24    95% interval 0.124 to 0.356    DECISIVE
n = 12   estimate 0.162   95% interval 0.092 to 0.232    NOT DECISIVE
n = 30   estimate 0.142   95% interval 0.101 to 0.183    NOT DECISIVE
```

**Two observations settle case A. Thirty do not settle case B.** Sufficiency is
a fact about the *distance from the decision boundary*, not about the sample
size — and any rule of thumb ("at least thirty") is wrong in both directions at
once.

## Case B at n=5 is the uncomfortable one

It reports DECISIVE, and n=12 and n=30 do not. That is not a bug and not
progress reversing. It is what happens when a marginal question is **re-asked as
evidence arrives**: an interval that straddles a boundary will sometimes clear
it by chance, and a system that acts the first time it does is choosing the
moment the noise flattered it.

The recipe prints it rather than picking sample sizes that hide it.

## The way out is not more data

In case B the decision is marginal because the action costs nearly what it
recovers. **A cheaper action moves the break-even** and settles a question that
no amount of measurement was going to settle. That is a genuinely different
remedy from "gather more evidence", and the system can tell you which you need.

---

## Design lessons

**L22 — the invented range is retired.** Recipe 5 swept `sensitivity_range:
[0, 2]` because there was nothing better, and I flagged it at the time as
mine rather than anything a reader would recognise. A calibration supplies the
range the **evidence** supports, so `assurance` becomes the share of the
plausible interval over which the recommendation survives, rather than the share
of a span somebody chose.

**L23 — calibration takes evidence, not numbers.** `decision.calibrate` accepts
`reasoning.as_evidence` results and nothing else, so R10 has already refused
anything uncontrolled. A calibration cannot be assembled from observations of
what merely happened next — which is exactly how the 0.6 would have been
produced.

**L24 — one observation is refused.** An interval needs two, and a single
measurement offers no way to tell a real effect from the one time it happened
to work.

**L25 — a weak assertion nearly let a real defect through.** The fixture
asserted the interval *narrows* with more evidence. A standard error that forgot
to divide by √n still narrows a little, because the *t* factor falls as degrees
of freedom rise — so the check passed on a broken implementation and only the
committed golden caught it. It now asserts the width falls by **more than the
t factor alone accounts for** (about fivefold from n=3 to n=20, against about
twofold without the √n). Same lesson as Recipe 5's sensitivity sweep: assert a
*difference between two cases*, not a direction.

---

## What is still open

The loop turns, but only for one assumption on one decision. Nothing yet
maintains a *body* of calibrations across many decisions, decides when an old
one has gone stale, or notices that the effect itself has changed — which is a
different question from estimating it, and the one a business would actually
hit second.
