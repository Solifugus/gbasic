# Design laboratory — Recipe 12: The whole loop, once

Status: **Design laboratory.** Adds nothing and refuses nothing; it *runs* what
eleven recipes had only ever assembled in pieces.

Executable: `examples/automation_lab/13_the_whole_loop.bas`, asserted in
`tests/run_automation.sh`.

---

## Why it exists

The design's status header has claimed the architecture is closed end to end
since Recipe 6. Eleven recipes later that claim had never been **run**. Every
seam was exercised — but always with a hand-built value on one side, and one
seam was not exercised at all:

> No Action anywhere in this tree was ever built from a Decision that
> `decision.evaluate` produced.

Recipes 9 and 11 *print* `evaluate`'s answer and then execute a hand-written
`reasoning.decision`. So R9's sizing check and §9's provenance chain had only
ever been enforced against records written by hand.

This recipe runs a shop chain — 4 regions × 5 categories, fourteen months — in
one program, through the real libraries, and reports what happens.

## What the loop did

Because the process looks every month, it declares `repetitions: 12` (R18),
which raises the bar from 3.51 to 4.63 and is why several real collapses below
that bar are correctly not acted on.

**Month 2 — R5 bites before anything else can.** The finding is real (z −5.01)
and the decision is sound (expected value 9,308), and nothing runs:

```text
  would it run? false -- needs rehearsal
    this process has never been replayed against history, so nothing is known
    about how often it fires or how often it is wrong (design R5)
```

**The live months.** Of eleven months, five findings cleared. `automation.assign`
held two of them back deliberately — the counterfactual exists only because
something chose not to act — and three were executed.

**The loop turns.** Three controlled outcomes calibrate to **0.374**, against a
true incremental effect of 0.38 (a treated cell recovers 0.62 of its gap, an
untreated one 0.24 on its own). Then the same finding decided twice:

```text
  sensitivity_range [0, 2]    send a manager   assurance 0.9
                              over [0, 2]
                              from a range the caller declared

  the calibrated interval     send a manager   assurance 1
                              over [0.885, 1.115]
                              from the calibrated interval, from 3 controlled outcomes
```

Same recommendation; different thing known about it. That is R17's `from`
earning its place in a running system rather than in a fixture.

**And R9 still bites inside the loop.** Month 13's leader did not clear, so
there is no quantity to size a decision off, and the boundary says so rather
than sizing off the leader anyway.

## What one Action turned out to carry

```text
  the finding it came from     revenue, z -5.87
    which established           leading_cell = -17128
  the decision that chose it   send a manager
    assured over               a range the caller declared
  the policy that permitted it spend limit 5000, rehearsal >= 6
  the authority it spent       within delegated authority
  the rehearsal it rested on   12 periods, 9 firings
  what the executor reported   dispatched: send a manager
```

Every line came out of the run. `assurance_is` is the proof the Decision came
from `evaluate` — nothing else puts one on a Decision — and it is what the
suite asserts.

## What the recipe found

Two defects, both mine and both instructive.

**The business had no problems in it.** The first draft keyed the collapse
schedule off the *treatment* record, so a cell only ever "collapsed" as a
consequence of being acted on. It ran, printed a plausible table, and found
nothing — because there was nothing. The collapse schedule is now independent
of anything the process decides, which is the only arrangement in which the
run means anything.

**`assign` returns a record.** `automation.assign(cell, 0.3) = "holdout"`
compares a record to a string and is **false**, silently and correctly (PLAT-EQ
answers across kinds and only refuses ordering). The consequence is specific to
this library: nothing is held back, so there is no counterfactual, so R10
refuses every outcome, so no calibration is ever produced — every step behaving
exactly as designed. The measured symptom was *0 of 20 held out at a declared
rate of 0.3*, which reads like a broken hash. It was a missing `.arm`.

## The load-bearing assertion

Not the golden. The suite requires the loop to **recover 0.38** from its own
controlled outcomes, and that is the only check here that would fail if the
chain were wired correctly and measuring the wrong thing. Proven red: with
`effect` taking the observed value instead of the difference from the holdout,
the calibration reports **0.623** — the treated recovery, which is a perfectly
reasonable number and the wrong one.
