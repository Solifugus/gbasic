# Design laboratory — Recipe 7: Did it work?

Status: **Design laboratory**, and the increment it produced is built
(`automation.assign`, `reasoning.as_evidence`/`as_observation`,
`tests/run_automation.sh`).

`automation.observe` records an outcome and R7 refuses to cite an unmeasured
one — but nothing fed an outcome *back*, so §11's learning cycle was
bookkeeping rather than a loop. This recipe closes it, and finds that **the
obvious way to close it teaches the system something false**.

Executable: `examples/automation_lab/05_did_it_work.bas`.

---

## The experiment

Eighteen months. Each month the process finds the worst-declining cell and
"intervenes". Cells are split into treatment and holdout by
`automation.assign`, deterministic in the cell's own key.

**The intervention has a true effect of exactly zero.** No line in the fixture
alters a single number after the decision is taken.

| group | n | mean fall | mean bounce | apparent recovery |
|---|---|---|---|---|
| treated | 12 | −7,106 | 3,458 | **48.7%** |
| holdout | 5 | −8,431 | 7,208 | **85.5%** |

## What a learning loop would have stored

> *The intervention recovers 48.7% of the loss.*

That number is false. The intervention does nothing. What it measures is
**regression to the mean**: a cell is selected *because* it was extreme, and
extreme observations contain noise, which does not repeat.

And note where that number was headed. Recipe 5 **assumed** a recovery of 60%
and flagged its recommendation as sensitive to exactly that figure. This is
where the assumption was going to come from — and uncontrolled, it would have
come back 48.7%, confirming a guess about an intervention that does nothing.

## What the comparison says

```text
treated recovered  48.7%
holdout recovered  85.5%
```

The cells nobody touched recovered just as well. **That** is the measurement.

The honest conclusion is *not* that acting hurts — the difference is noise on a
handful of observations, and a holdout this small cannot resolve a small effect
either. The defensible statement is that there is **no evidence the intervention
does anything**, which happens to be exactly right.

---

## Design lessons

**L16 — R10.** An outcome measured without a comparison is evidence that
something happened next, not that the action worked. And the bias is not a
tail risk: an action is taken *precisely when* a measure is extreme, so the
system's entire learning corpus is drawn from the one condition where the naive
measurement is systematically wrong.

`reasoning.as_evidence` now refuses an uncontrolled outcome by name.
`reasoning.as_observation` will read it, labelled `uncontrolled: true` with the
caveat carried **in the value** rather than in a comment somebody may not read.

**L17 — the architecture had no way to not act.** A system that always acts
when it should can never learn whether acting helps. `automation.assign(key,
holdout_rate)` is that missing concept, and it is **deterministic in the key**
rather than random, for two reasons that are both about R5: a replay must make
the same assignments as the live run or the rehearsal describes a different
program, and an operator asking *why was this one held back* must get an answer.

A zero holdout rate is allowed — plenty of processes have nothing worth
learning about — but it **says what it costs** in the returned value. Holding
back everything is refused.

**L18 — a hash bug that hid behind comfortable test data.** The first
assignment hash left consecutive short keys consecutive, so a whole run landed
on one side of the threshold: **0 holdouts out of 400** for keys `c1`…`c400`.
The first distribution check used `cell-N/x` — longer and more varied — and
reported a clean 0.200. The fixture now uses short sequential keys *on purpose*,
because a distribution tier built from comfortable keys does not test the hash.

---

## What is still open

The loop is closed but not *turned*: nothing yet takes a controlled effect and
feeds it into a later `decision.evaluate` as the recovery assumption. That is a
small step now — the value shape carries `effect`, and `decision` already takes
`recovers` — but it wants its own recipe, because the interesting question is
what happens when the evidence is thin: one controlled observation is not a
calibration, and the design has no rule yet for how much evidence is enough.
