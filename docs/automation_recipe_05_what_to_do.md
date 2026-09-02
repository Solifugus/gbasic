# Design laboratory — Recipe 5: What should we do about it?

Status: **Design laboratory**, and the increment it produced is built
(`stdlib/decision.bas`, `tests/run_decision.sh`).

The first recipe to cross the `decision` boundary. Recipes 1–4 all stop at the
finding, which left two thirds of the architecture unexamined — which is why
[automation_reasoning_design.md](automation_reasoning_design.md) §15 scheduled
this one ahead of the breadth list.

**It is executable**: `examples/automation_lab/03_what_to_do.bas`, asserted by
`tests/run_decision.sh`. It consumes a **real Finding** from
`insight.explain_change`, not a hand-written one, which is where its findings
come from.

---

## Business problem

Recipe 1 found that one store-category cell collapsed. Somebody now has to
decide whether to spend money on it.

## Business objective

Maximise revenue, subject to a spending authority: this process may commit
5,000 without asking a human.

## Available data

The Finding from Recipe 1, plus a small set of alternatives with costs and
assumed recoveries.

## Decision process

Score each alternative by expected value, pick the best, state what authority
it needs, and say how sensitive the recommendation is.

## Human role

Approving anything beyond the spending limit — and, as it turns out, being
told when the recommendation rests on a number that was never established.

---

## What the Finding actually said

```text
aggregate change   -37495     established?  false
leading cell       Northeast -> Northeast-2 -> Outdoor
its change         -16835     established?  true
```

**One cell is real and the aggregate is not.** That is not a corner case
constructed for the recipe — it is what Recipe 1's data actually produced, and
the decision layer has to survive it.

## Finding 1 — the same decision, sized two ways, picks opposite actions

An emergency restock plus local promotion costs 15,000 and is assumed to
recover 60% of the loss.

| sized off | loss taken as | expected benefit | expected value | verdict |
|---|---|---|---|---|
| the aggregate decline | −37,495 | 22,497 | **+7,497** | ACT |
| the cell that cleared | −16,835 | 10,101 | **−4,899** | DO NOT ACT |

Same intervention, same data, opposite answers. The only difference is which
number was taken as the loss — **and the Finding had already said the aggregate
was not established.**

This is R2's consequence one layer along, and it produces a new refusal:

> **R9 — a decision may not be sized off a quantity the Finding declined to
> establish.**

Enforced at the boundary in `decision.evaluate`, and again structurally in
`reasoning.decision`, so a hand-built Decision cannot smuggle one past.

The control matters as much as the refusal: the *same* Finding sized off the
cell that did clear is accepted, and a Finding whose aggregate *is* established
may use it. Without those, R9 would be indistinguishable from refusing
everything.

## Finding 2 — the recommendation is the best option, not the best affordable one

```text
do nothing                 EV 0         within authority
send a regional manager    EV 1000      within authority
restock and promote        EV 7497      needs approval

best option overall:            restock and promote (EV 7497)
best option within authority:   send a regional manager (EV 1000)
```

A decision layer that quietly returned the second would be **hiding the only
choice a human actually needs to make**, and would look entirely reasonable
doing it. So the recommendation is the first, `authority_required` is a separate
field, and enforcement happens later at the action — which is R6, and this is
the case that motivates it.

## Finding 3 — materiality has to answer `unknown`

Materiality is computed here, from the Context, exactly as §4.6 required. With
no threshold declared for a measure it returns **`unknown`, never `false`**.
*We have no threshold* and *this does not matter* are different statements and
only one of them is true.

## Finding 4 — assurance is a sensitivity, and mine was wrong

The charter's own example of a useful decision was *"the recommendation is
highly sensitive to the assumed customer churn response"*. So `assurance` here
is the share of a declared range of the recovery assumption over which the
recommendation survives, with the crossing named:

```text
recommendation   restock and promote
assurance        0.95
flips at 0.1     do nothing -> restock and promote
```

against a marginal one:

```text
recommendation   send a manager
assurance        0.43
```

**The first implementation of that sweep was wrong**, and the fixture did not
catch it. It cancelled each alternative's own recovery, so a cheap intervention
and an expensive one received identical benefit and the cheap one always won;
it reported `assurance` 0 for a recommendation it never once selected. Every
assertion passed, because `assurance < 1` and *sensitivities is non-empty* are
both satisfied by exactly that failure.

It was found by printing the values, not by the suite. Two things fix it, and
the second is the real one:

1. the library now **raises if the sweep disagrees with the point estimate at
   the nominal assumption** — at a scale of 1 it is computing what the point
   estimate computed, so they must agree;
2. the fixture asserts assurance as a **difference** between a robust
   recommendation and a marginal one (0.95 against 0.43), not as a number in
   range.

The range must also **bracket 1**, or the sweep never visits the assumption the
recommendation was made under.

---

## Design lessons

**L8 — R9, above.** New refusal, now in the design.

**L9 — the decision layer's value is not the choice.** Everything interesting
in this recipe is beside the recommendation: what it was sized off and whether
that was established, what authority it needs, how sensitive it is and where it
flips. A layer that returned only *restock and promote* would have been useless
and would have passed every test that was about the choice.

**L10 — `Context` earned its place.** It was proposed in §5 on the strength of
one argument about materiality. It is now carrying objectives, the materiality
threshold and the spending authority, read by one library and destined for
another. The shape held.

**L11 — the sweep must agree with the point estimate.** A general pattern
rather than a detail of this function: wherever a library computes something
two ways, the cheap way and the swept way must agree at the nominal point, and
that agreement is worth enforcing rather than assuming.

## What this recipe did not test

One objective, one uncertainty swept, mutually exclusive alternatives, and no
`automation` layer — nothing here executes anything, so R5 (simulation as a
precondition) and R6's *enforcement* half are still untested. Multi-objective
decisions, constraints, and the cost-asymmetry primitives in §16 are all
untouched.
