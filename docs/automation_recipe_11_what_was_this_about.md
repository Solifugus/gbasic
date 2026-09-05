# Design laboratory — Recipe 11: What was this evidence about?

Status: **Design laboratory**, and the increment it produced is built
(**R16** in `decision.calibrate`, the §9 provenance chain in
`reasoning.decision` / `reasoning.action` / `automation.execute`, asserted in
`tests/run_decision.sh` and `tests/run_automation.sh`).

Recipe 9 turned the loop: controlled outcomes become the assumption a later
decision rests on. It measured **one** intervention, so the question a real
learning loop meets on its first day never came up. A real organisation runs
several campaigns at once and stores every measured action in one place.

Executable: `examples/automation_lab/12_what_was_this_about.bas`.

---

## The setup, and why nothing about it is suspicious

Two campaigns run over a year. Both are gated by `automation.execute`, both are
measured against a holdout, both are read back through `reasoning.as_evidence`
— so R10 is satisfied and every observation here is **controlled**, which is
the property recipe 7 established as the hard one to get.

| intervention | cost | true recovery |
|---|---|---|
| send a manager | 2,000 | 0.35 |
| cut the price | 2,000 | 0.05 |

Against a 16,835 loss an action costing 2,000 pays for itself at a recovery of
**0.119**. So the first is worth doing and the second is not, and saying so is
the entire job of a calibration.

**Asked one campaign at a time, the library gets both right:**

```text
  send a manager    estimate 0.368   interval 0.297 to 0.438
                    recommendation send a manager  ACT   assurance 1

  cut the price     estimate 0.027   interval -0.057 to 0.112
                    recommendation do nothing      DO NOT ACT   assurance 1
```

## One pool

Now ask the question a learning loop actually asks — *how well do our
interventions work?* — by calibrating from the store of every measured action.
Twenty-four controlled outcomes, all revenue, all from gated actions, all
measured the same way. Nothing in the shape of the data objects.

```text
  send a manager    estimate 0.197   interval 0.108 to 0.287
                    recommendation send a manager  ACT   assurance 0.9

  cut the price     estimate 0.197   interval 0.108 to 0.287
                    recommendation cut the price   ACT   assurance 0.9
```

**The price cut is now recommended**, and it recovers 0.05 against a break-even
of 0.119. The estimate is not a compromise between two answers. It is the
answer to a question nobody asked, and it is wrong in opposite directions for
the two campaigns that produced it.

## And more evidence makes it worse

| pool | estimate | 95% interval | width |
|---|---|---|---|
| 12 | 0.180 | 0.063 to 0.297 | 0.234 |
| 24 | 0.197 | 0.108 to 0.287 | 0.178 |
| 48 | 0.190 | 0.128 to 0.252 | 0.125 |
| 120 | 0.198 | 0.161 to 0.235 | 0.074 |

The standard error falls with the size of the pool, so **the more
incommensurable evidence is gathered, the more confident the wrong number
becomes.** By 120 observations the interval contains neither truth, and the
decision sized off it reports:

```text
  cut the price     estimate 0.198   interval 0.161 to 0.235
                    recommendation cut the price   ACT   assurance 1
```

DECISIVE, from 120 controlled observations, about an intervention that loses
money every time it is taken.

That is exactly the shape recipe 9 taught us to trust — a narrow interval well
clear of the break-even — and here it is being produced by a defect.

## The mechanism, which is not a bug in the arithmetic

The pooled mean is a **real quantity**. Twenty-four numbers were averaged and
0.197 is their average. What it is not is an estimate of the effect of anything
that was done, and no property of the arithmetic can notice that: an effect is
a bare number, and bare numbers average happily.

The failure is one level upstream. **The evidence could not say what it was
about**, because an Action did not record it. `as_evidence` returned an effect,
an expectation, an observation and a holdout, and its only gesture at identity
was the recommendation's name, which nothing read.

## R16, and its control

> **A calibration may not pool evidence about different questions.**

Every item must agree on the **measure** and the **intervention**, and the
calibration carries both:

```text
  pooling two interventions:
    decision.calibrate: item 13 is about cut the price and item 1 about send a
    manager -- calibrating one intervention from another's outcomes answers a
    question nobody asked, confidently (design R16)

  pooling two measures:
    decision.calibrate: item 13 measures days_to_pay and item 1 measures
    revenue -- these are not observations of one quantity, and their average is
    not an estimate of anything (design R16)
```

**Cells are deliberately not part of the rule.** Pooling across the places a
thing was tried is the entire purpose of a calibration, and a refusal that took
that away would be indistinguishable from having no calibration at all. So the
control is asserted beside the refusals, in the recipe and in the unit fixture:

```text
    12 outcomes from two regions, estimate 0.34, about send a manager on revenue
```

## What made the refusal possible was not a new measurement

It was the chain §9 had asked for from the start and called *not a logging
feature*:

```text
DATA → FINDING → EVIDENCE → DECISION → POLICY → ACTION → OUTCOME
```

Two of its seven links were missing. `reasoning.decision` did not require the
Finding it came from — `decision.evaluate` recorded a `finding_subject` label
and nothing else — and `reasoning.action` did not record the **policy** at all,
so an executed action could not say which objectives, thresholds and authority
had been in force when it ran.

Both are required now, and the finding reaches the Action **through** the
Decision rather than beside it: containment carries it, and a second copy is a
second thing that can disagree with the first. What `reasoning.action` checks is
that the link is there — a hand-built Decision naming no finding may not enter
the action layer, the same structural treatment R9 gets, because a rule only the
happy path obeys is a convention.

## What was measured, and what was not

Measured: the pooled estimate, its interval at four pool sizes, the
recommendations either way, and that the refusals fire and the control does
not. Three perturbations were proven red — R16's comparisons deleted,
`as_evidence` no longer carrying what it was about, and `execute` no longer
recording the policy — each caught by the tier written for it.

Not measured, and worth saying: whether the rule is **enough**. Agreement on
measure and intervention is necessary and it is not obviously sufficient. Two
runs of *send a manager* on *revenue* a year apart, under different prices and
a different organisation, are pooled here without objection. That is a real
limitation of the rule and not of the recipe — the recipe would need a
population with a genuine regime change in it to say anything about it.
