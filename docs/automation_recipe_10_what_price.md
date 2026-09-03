# Design laboratory — Recipe 10: What price?

Status: **Design laboratory**, and the increment it produced is built
(`decision.quantity`, `tests/run_decision.sh`).

**The first recipe of a different shape.** Every executable recipe before this
was the same scenario: a measure moved, where, and which of a handful of actions
to take. That sameness was the largest untested assumption in the design — nine
recipes' worth of architectural conclusions resting on one kind of question.

This one differs in the ways that matter:

- the answer is a **continuous quantity**, not a choice from a list;
- there is **no decomposition** — *where did it happen* is not the question;
- the **cost of inaction dominates**: a wrong price bleeds every day;
- and it needs a **model**, whose uncertainty has to reach the answer.

The last of those is what breaks things, and it is the point.

Executable: `examples/automation_lab/08_what_price.bas`.

---

## The model

For constant-elasticity demand the profit-maximising price is

```text
p* = cost * b / (1 + b)
```

which exists **only while the elasticity `b` is below −1**. At −1 revenue is
flat in price and profit rises without bound; above it, the model has no maximum
at all.

## Three cases, one estimator

```text
CASE A: clearly elastic
  elasticity  -2.55   95% interval -2.82 to -2.28
  price       16.46   from the interval 15.5 to 17.83
  the elasticity interval is 1.24x wide and the price interval 1.15x
  amplification 0.93   (1 would be proportional)

CASE B: elastic, but close to the edge
  elasticity  -1.3    95% interval -1.57 to -1.03
  price       43.64   from the interval 27.64 to 373.18
  the elasticity interval is 1.52x wide and the price interval 13.5x
  amplification 8.85   (1 would be proportional)

CASE C: barely elastic
  elasticity  -1.2    95% interval -1.47 to -0.93
  REFUSED
    the parameter interval reaches -0.9977, where this model is
    undefined, so there is no quantity to recommend. A point estimate
    would have answered 60.7 with a straight face
```

**Case B is the finding.** The same quality of estimate — an interval a shade
over 1.5× wide — gives a price anywhere from 27.6 to 373, because the model
divides by `(1 + b)` and `b` is near −1. Nothing about the elasticity estimate
looks bad. The answer is simply not known.

**Case C is the refusal.** There is no answer at all, and a point estimate names
one anyway. `60.7` is a perfectly ordinary-looking price.

---

## Design lessons

**L26 — R12.** A quantity may not be recommended when the parameter's interval
reaches a value at which the model is undefined. Not a wide answer — *none*.
`decision.quantity` walks the interval rather than checking its endpoints (a
break anywhere is a break) and reports **what a point estimate would have said**,
because that number is the most confident-looking wrong answer in this design.

**L27 — amplification belongs in the output.** How much a model magnifies its
parameter's uncertainty into the answer's is not a detail; in case B it is the
whole story. Reported as a ratio, so *below 1* means the model damps and *8.85*
means the answer is eight times less known than the parameter. (Asserting it as
a raw spread passes every "is it large near the edge" check, which is how the
first version of the test failed to catch a broken one.)

**L28 — `evaluate`'s shape genuinely does not fit, and that is the architectural
answer.** `decision.evaluate` takes a list of alternatives; a price has
infinitely many. Rather than force a continuous problem into a discrete API,
`quantity` is a second entry point. The three-layer split survives — this is
still *decide*, and it still refuses to execute — but **the decision layer needs
more than one shape of question**, which nine recipes of the same kind could
never have shown.

**L29 — a `map` function value cannot capture anything.** gBASIC has no
closures, so the model function cannot take the cost from its caller and the
`10.00` is written into it. That is a real constraint on this API's shape: a
caller needs one function per model *instance*, not per model. Recorded rather
than worked around quietly; the alternative is a declarative model vocabulary
like recipe 8's `predicts`, which would also be auditable — and is probably the
better answer once there is a second model to generalise from.

---

## A note on my own testing

Three times now — Recipe 5's sensitivity sweep, Recipe 9's interval, and this
recipe's amplification — I have written an assertion that **passes on the broken
implementation**, and each time only the committed golden caught it. The pattern
is always the same: asserting that a number is *large*, or *smaller than before*,
when what distinguishes correct from broken is a **specific relationship**
(assurance differs between two cases; the width falls faster than the *t* factor
explains; amplification is below 1 where the raw spread is above it).

The rule that keeps emerging: **assert what differs between the right answer and
the plausible wrong one**, never that a value sits in a reasonable range.

---

## What is still open

One model, one parameter, one product. Nothing here handles a model with several
uncertain parameters at once — where the interval is a region rather than a
range and "walk it" stops being cheap — and nothing chooses *between* models,
which is where a real pricing problem starts.
