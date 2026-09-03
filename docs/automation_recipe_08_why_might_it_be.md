# Design laboratory — Recipe 8: Why might it be happening?

Status: **Design laboratory**, and the increment it produced is built
(`reasoning.hypothesis`, `insight.weigh`, `tests/run_hypothesis.sh`).

§4's ladder is OBSERVATION → ASSOCIATION → **CAUSAL HYPOTHESIS** → TEST →
supported or rejected explanation. Everything before this stopped at the first
rung: `explain_change` says *where* a change is concentrated and refuses to say
why, and R3 had been pointing at machinery that did not exist — `hypotheses`
was an empty array in every Finding the library had ever produced.

Executable: `examples/automation_lab/06_why_might_it_be.bas`.

---

## What it does not do

**It does not determine a cause, and it is built so that it cannot.** Every
hypothesis carries `explains: false` for its whole life; only a recorded test
can promote one, and that is R3, and this is not it.

What it produces is a **question**: what each candidate predicts, how well that
matches what actually happened, and the observation that would tell the
survivors apart.

## The value is in the pattern, not the magnitude

A hypothesis predicts **which cells** should have moved. Comparing that against
the cells that actually cleared is a *contingency* — hits, over-predictions,
cells left unexplained — and it is auditable in a way a scalar is not.

```text
hypothesis                                       predicts hit over agree
a stock-out of Outdoor goods at Northeast-2      1        1   0    1
a competitor opened near Northeast-2 selling o   1        1   0    1
supply constraint across store Northeast-2       5        1   4    0.2
an Outdoor category problem everywhere           12       1   11   0.08
a Northeast regional demand shock                15       1   14   0.07
```

**Parsimony is not imposed here — it falls out.** A hypothesis predicting
fifteen cells and explaining one has over-predicted fourteen, and set agreement
says so on its own. (Proven by perturbation: scoring on hits alone makes
*"everything"* the winning explanation.)

## No probability is claimed

The charter's §11 showed `inventory availability   confidence .91`. **That
number is unearned.** Nothing in the data supports a probability that a
hypothesis is *true*. What can honestly be computed is set agreement between
predicted and affected cells, and it is reported under its own name with its
definition travelling beside it:

> *set agreement between predicted and affected cells (hits / union), NOT a
> probability that the hypothesis is true*

## R11 — what this data cannot decide

The top two predict **exactly the same cell**:

```text
'a stock-out of Outdoor goods at Northeast-2'
'a competitor opened near Northeast-2 selling outdoor goods'
predict exactly the same cells. Ranking them would invent a
preference the evidence does not support.

leader                a stock-out of Outdoor goods at Northeast-2
separable from rivals false

NEXT TEST
  the leading hypotheses predict the same cells and this data cannot
  separate them. Observe: on-hand inventory for that line at that store
  OR  footfall and competitor pricing near that store
```

That is the output. Not a cause — a question, and the observation that would
answer it.

---

## Design lessons

**L19 — R11.** Two hypotheses that predict the same pattern are not separated
by that pattern, and ordering them is invention. Report them tied and name what
would separate them. Its control matters as much: hypotheses that *do* predict
differently must still be ranked, or "refuses to rank" is satisfied by refusing
to rank anything.

**L20 — a discriminator is required, and that is the opinionated part.** A
hypothesis you cannot imagine an observation for is not a hypothesis, it is a
story — and a story scores against a pattern exactly as well as an explanation
does. Requiring the discriminator up front is what makes the ranking mean
something.

**L21 — `predicts` is declarative, not a function value.** gBASIC has
first-class functions and a predicate would be more general, but a prediction
that cannot be written into provenance cannot be audited later, and *why did we
believe that* is the question this whole layer exists to answer.

---

## The real-data check

The second half of this work: point the pipeline at real business spreadsheets
rather than generated ones. The Enron corpus from the xlsx campaign is still on
disk — 15,871 workbooks.

**Measured on a 250-workbook sample: 4.4% carry a month-headed table.** That is
roughly 700 across the corpus — enough for a recipe, not enough for broad
validation, and my detector is crude (month names in the first twelve rows, so
it misses quarters and period numbers while over-counting sheets `grid` would
reject).

Pointing `grid` at a real one is the more useful result.
`daren_farmer__6199__Consolidated_2000.xlsx` has notes and file paths in rows
1–3 and its real header on **row 4**. `grid.tables` found 13 blocks and reported
the main one as:

```text
rows 1-566  header 2  confidence low  frame cols 3
  rows above the header were skipped (title or stub);
  the row below also looks like a header: two-row header?
```

It guessed wrong — and **said so, with reasons, instead of producing a
plausible frame from three of thirty-five columns and calling it a table.**
That is `grid`'s stated contract working on data it has never seen.

**The conclusion that matters for the roadmap:** `explain_change` was never the
bottleneck for real data. Getting a frame out of a real sheet is, and that is a
spec-writing problem per sheet, not an analytics problem. A real-data campaign
here would mostly be `grid.extract` specs — which is worth knowing before
committing to one.
