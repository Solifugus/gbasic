# Business Automation Reasoning

Status: **Partial.** **All three layers now have a first increment** —
`stdlib/reasoning.bas`, `stdlib/insight.bas` (`tests/run_insight.sh`),
`stdlib/decision.bas` (`tests/run_decision.sh`) and `stdlib/automation.bas`
(`tests/run_automation.sh`). The architecture is closed end to end: a Finding
becomes a Decision becomes a gated Action becomes a measured Outcome. What is
not built is breadth — one function per layer, and §15's recipes 2–4. Recipe 1 is built and measured
([automation_recipe_01_sales_decline.md](automation_recipe_01_sales_decline.md),
`examples/automation_lab/01_sales_decline.bas`,
`tests/run_automation_lab.sh`), and `examples/automation_lab/02_explain_change.bas`
runs the same investigation through the library as a cross-check.

*This document began as a charter written 2026-09-01 and was rewritten into a
design on the same day, after Recipe 1. The vision in §2–§3 is substantially
the original; everything the original marked "provisional", "intentionally
unresolved" or "should emerge from experimentation" has either been decided
here or moved to §16 with a reason.*

---

## 1. What this is

Many business activities consist of **repeatable reasoning over data**:
observing conditions, noticing meaningful change, investigating why, weighing
alternatives, deciding under uncertainty, acting within authority, escalating
what needs judgement, and measuring whether the last action worked.

Business intelligence stops at the first step:

```text
DATA → REPORT → human observation → human investigation → human decision → ACTION
```

This proposes to make more of that programmable:

```text
DATA → INSIGHT → DECISION → AUTOMATION → ACTION → NEW STATE ─┐
         ↑                                                   │
         └───────────────────────────────────────────────────┘
```

The programmer is not encoding the answer. They are encoding **a process for
discovering, evaluating and acting on answers**. That distinction is the whole
project.

## 2. Selective autonomy

A sufficiently complete implementation could automate substantial parts of a
business. That is not the same as eliminating people, and the architecture must
not pretend otherwise.

Humans remain where decisions involve subjective judgement, ethics,
relationships, negotiation, creativity, ambiguous objectives, organisational
politics, novel circumstances, legal accountability, high-consequence
authorisation, and preferences that cannot honestly be reduced to a number.

So the architectural objective is **selective autonomy**: routine, measurable,
well-understood decisions may be automated; uncertain, unusual, subjective or
consequential ones are escalated. Which is which is *declared*, not inferred
(§7).

## 3. Composition, not duplication

gBASIC already has `stats`, `finance`, `accounting`, `lending`, `credit`,
`scoring`, `deposits`, `market`, `fundamentals`, `edgar`, `frame`, `dbframe`,
`persist`, `schedule`, `mail`, `web`, `chart`, `grid`, `crypto`, `fake` and
`llm`. This composes them.

Recipe 1 is the evidence that this is realistic rather than aspirational: it
performs the design's flagship operation with **no new library at all**, and
roughly 140 of its 200 lines are grouping and filtered totals that
`frame.summarize` already does (§4.7).

---

## 4. What Recipe 1 measured

Recipe 1 was built to test §11 of the original charter — *automatic
decomposition*, the most attractive proposal in it. The findings drive
everything below, and the first one contradicts the charter.

### 4.1 A drill-down is a search, and a search always returns a winner

The same decomposition was run over two populations: one with a real 45%
collapse planted in a known cell, one with **nothing planted at all**.

| | real cause | pure noise |
|---|---|---|
| headline | −37,495 (−1.8%) | −36,562 (−1.8%) |
| top region share | 82.6% | 80.3% |
| top store share | 57.3% | 51.2% |
| conclusion | Northeast → Northeast-2 → Outdoor | Southeast → Southeast-2 → Electronics |

Same headline to a tenth of a percent. Same confident three-level chain.
**Nothing in the output distinguishes a real cause from nothing.** The
charter's illustrative output — *"67% of the decline is attributable to…"* — is
exactly what this produces, from noise as readily as from a cause.

This does not retire the idea. It makes the null model the load-bearing part of
it rather than a refinement for later.

### 4.2 The reference distribution is the sibling cells

No new capability is needed. The other 59 leaf cells are a sample of ordinary
movement, and against that null the two runs separate cleanly: z = −3.45 against
z = −2.38.

Generalise it: *what does ordinary look like here?* is usually answerable from
the same data that raised the question.

### 4.3 Search width is part of the result

The cut separating those two is not a number a library may choose. A `z` that
is remarkable across four regions is unremarkable across two hundred product
families.

**A Finding must carry how wide the search was.** Without it the Finding cannot
be judged, and two Findings from searches of different width cannot be compared.

**Correction, made while implementing §13.** Recipe 1 used `sqrt(2 ln n)`,
which is where the largest of *n* draws lands *on average* — so roughly half of
all pure-noise populations produce a leader that clears it. Measured: **6 of 13
seeds with nothing planted cleared**. That is a coin flip with a formula in
front of it, not a correction.

The threshold is the two-sided **family-wise quantile** for a declared error
rate: reject only beyond the point a search of *n* cells would exceed with
probability `alpha` when nothing is happening. The same 13 seeds now clear
**0 of 13**. `alpha` is recorded in the Finding, because a threshold nobody can
see is a threshold nobody can argue with.

**Second correction, made while implementing §13's automation increment.** A
cell was being standardised against a spread that *included it*, so the outlier
inflated the very sd it was measured against. That puts a hard ceiling on how
extreme anything can look — `max|z| = (n-1)/sqrt(n)` — and at 8 cells the
ceiling is **2.47, below the threshold**: the test could never fire however
completely a cell had collapsed, and would report *within ordinary variation*
for a cell that had gone to zero. Cells are now judged **leave-one-out**, and
since a leave-one-out residual is *t*-distributed rather than normal the
threshold uses *t* with `n−2` degrees of freedom. Below four cells there is no
dispersion left to estimate and the call is refused.

**Third correction, made under review (2026-09-03), and the one that matters
most: the threshold was calibrated and the calibration was never checked.**
Measured over 200 null trials — nothing planted — the family-wise
false-positive rate came out **0.090 at 12 cells, 0.110 at 20 and 0.143 at 40**
against a requested 0.05, *worsening as the search widened*, which is the
opposite of what a family-wise correction is for.

The cause is **tail weight, not skew**. A cell's change is a difference of two
independent sums, so skew cancels; tail weight does not, and revenue-like data
has it. Confirmed by a control: the identical search over uniform data lands at
**0.047**, exactly α. The correction was never wrong about multiplicity — it
was wrong about the shape of the distribution it was correcting.

Two things follow, and the first is a retraction. `search.alpha` is now
**`search.alpha_requested`**: a Finding may state the error rate it was *asked*
for and must not imply it *achieved* it. `null.calibration` carries what is
actually known — the method, whether it is assumed or measured, and the
observed rates.

The second is a second declared null. **`siblings_permuted`** takes the
threshold from this data's own variability by reassigning period labels within
each cell, which follows the tails instead of assuming them: **0.085 at 20
cells and 0.050 at 40** at the default 200 draws. Raising the draw count helps
and costs proportionally: **0.070 at 20 cells with 800**, so part of the
residual is the coarseness of estimating a 95th percentile from 200 samples and
part of it is not. Better, not perfect, and every number here is pinned in
`tests/insight_calibration.bas` rather than claimed.

*An attempt that failed, kept because it is the same class of error this
library exists to refuse:* the first permutation flipped the sign of each
cell's deviation. That leaves every |deviation| intact, so the largest cell is
still the largest in every draw and the permutation distribution centres on the
statistic it is supposed to judge — the threshold came out **above** the
observed maximum (3.724 against 3.309) and the test fired **0 times in 200 null
trials**. A null that never fires is worse than one that fires too often,
because nothing reveals it.

One consequence is worth knowing, because the obvious mental model says
otherwise: **the threshold is not monotone in the search width.** Two effects
pull opposite ways — with few cells the spread is badly estimated and *t*
demands an enormous z; with many cells the search penalty grows. It is U-shaped
(8.86 at 4 cells, a minimum near 3.48 around 30, 3.73 at 200), so there is a
granularity sweet spot, and it falls out of the two corrections rather than
being chosen. Recipe 6 met the same fact from the other end: 12 cells missed a
collapse that 60 cells caught.

### 4.4 Contribution shares do not partition

In the planted run the region shares were −22.6%, −3.7%, 43.7%, 82.6%. Among
the regions that actually *declined* they sum to **126%**.

Correct arithmetic, disastrous phrasing: *"Northeast is 82.6% of the decline"*
invites *most of it was Northeast* when Midwest was independently 43.7% of it
and the difference was made up by Southeast rising. As offsetting movements
grow, shares inflate without bound while the net change approaches zero.

### 4.5 "Confidence" was three quantities sharing one word

The charter used `confidence` for how well a quantity is estimated, how well a
hypothesis accounts for evidence, and how sure we are an action is right. They
share no scale and must not share a threshold. Decided names:

| name | question | produced by |
|---|---|---|
| `confidence` | how well is this quantity estimated? | `insight` |
| `support` | how well does this hypothesis account for the evidence, against rivals? | `insight` |
| `assurance` | how sure are we the recommended action is right? | `decision` |

### 4.6 Materiality cannot live on the Finding

The charter made materiality a property of a Finding. But `insight` can say a
cell is *statistically unusual* with no business context, and cannot say it
*matters* without an objective — and objectives belong to `decision`. As
written it forced `insight` to know the organisation's goals, breaking the
separation the architecture rests on.

Decided in §5: materiality is computed by `decision`, from a **context** both
layers read.

### 4.7 The boilerplate is the API's job

`insight.explain_change` should take a frame, a measure column, a period
column and an ordered list of dimensions. Everything Recipe 1 hand-rolled
around that is `frame`'s existing work.

---

## 5. Architecture (decided)

```text
                      ┌───────────────┐
                      │ reasoning.bas │  value shapes + validation
                      └───────┬───────┘
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
   ┌─────────────┐     ┌──────────────┐   ┌────────────────┐
   │ insight.bas │ ──▶ │ decision.bas │──▶│ automation.bas │
   └─────────────┘     └──────────────┘   └────────────────┘
     observes &          evaluates &        orchestrates &
     reasons             chooses            acts
```

- **`insight`** observes and reasons. It never alters the business.
- **`decision`** evaluates and chooses. It never executes its recommendations.
- **`automation`** is the only layer authorised to change external state.

**`reasoning.bas` is new to this design** and is not a fourth layer. It owns the
value shapes and their validation — `Finding`, `Context`, `Hypothesis`,
`Decision`, `Action`, `Outcome` — because all three layers construct and
inspect them and none of them should have to depend on another to do so. It has
no behaviour beyond construction, validation and provenance.

**`Context` is the answer to the charter's own architectural question.** The
charter asked whether the three-way split was right and wondered about moving
responsibilities between layers. §4.6 shows the problem is not a misplaced
responsibility but a *missing value*: objectives, thresholds, policies and
authority are read by more than one layer and belong to neither.

```text
Context
    objectives[]        what the organisation is trying to achieve
    thresholds          what counts as material, per measure
    policies[]          what may be done, by whom, within what envelope
    authority           what THIS process may execute without a human
    null_policy         what counts as ordinary (§7)
```

A `Context` is data. It is loaded, versioned, diffed and audited like any other
business record, which is what makes "why was this allowed?" answerable later.

A convenience facade (`business.bas`) is **deferred** (§16) until the three
libraries have stabilised. Building it early would freeze boundaries the recipe
programme exists to test.

---

## 6. The shared reasoning model

The charter listed fourteen candidate objects whose representations "should
emerge from experimentation". Recipe 1 is one experiment, and it settles one of
them. The rest are staged honestly rather than all specified at once.

### 6.1 Finding — specified

```text
Finding
    subject                 what was examined
    measure                 the quantity ("revenue")
    period                  { baseline, current }
    observation             { baseline, current, change, change_pct }
    search                  { dimensions[], cells, width }      REQUIRED  §4.3
    null                    { kind, mean, sd, threshold }       DECLARED  §7
    strength                { z, clears }                       §4.2
    contributors[]          { path, change, share | unknown }   §4.4
    associations[]          measures that moved with it — never "causes"
    hypotheses[]            Hypothesis values, untested until tested
    provenance              §9
```

Note what is **absent**: no `materiality` (§4.6), no bare `confidence` (§4.5),
and no `cause` (R3).

### 6.2 Context — specified above (§5).

### 6.3 Hypothesis, Decision, Action, Outcome — shape deferred to their recipes

`Hypothesis` and the causal progression are sketched in R3 and need the recipe
that tests one. `Decision`, `Action` and `Outcome` need the first recipe that
actually decides something, which §15 schedules deliberately, because *no
recipe so far has crossed the `decision` boundary at all*.

The charter's `METRIC`, `SIGNAL`, `EVIDENCE`, `FORECAST`, `RISK`, `GOAL`,
`CONSTRAINT`, `ALTERNATIVE`, `POLICY` are **not** adopted as objects yet. Some
will turn out to be fields rather than types. Recipe 1 needed none of them.

---

## 7. Declared choices

Following `lending`'s accrual basis, `credit`'s delinquency method and
`scoring`'s WOE orientation: where two reasonable conventions give different
answers, the caller declares and the library never infers.

- **The null** — what counts as *ordinary*, and now a real choice rather than a
  single option:
  - `siblings` — the other cells, with a threshold from the *t* distribution.
    Cheap, and **exact only for light-tailed cell changes** (0.047 measured
    against a requested 0.05 for uniform data; 0.100–0.143 for lognormal
    revenue).
  - `siblings_permuted` — the same cells, with the threshold taken from the
    data's own variability by permuting period labels within each cell. Costs
    *B* extra passes and assumes only that a cell's observations are
    exchangeable between periods under the null (0.085 and 0.050 measured).

  Both are cross-sectional: they compare cells to each other at one moment and
  neither looks at time, so **both are wrong under seasonality**. That is
  Recipe 3's question and it is open.
- **The comparison** — period over period, versus same period last year,
  versus forecast. These disagree, routinely, and each is right for a different
  question.
- **The authority envelope** — what this process may do without a human.
  Never a default; an unset authority means *nothing may execute*.
- **Materiality thresholds** — per measure, in the `Context`.

---

## 8. Refusals

The charter had principles and nothing that said *no*. Every shipped library in
this tree turns on a small set of refusals, each preventing a specific
plausible-looking wrong answer rather than a crash: `money` refuses to add two
currencies, `credit` refuses to infer a delinquency convention, `scoring`
refuses to smooth an empty bin.

This domain needs them more than any of those, because it is the one where a
plausible wrong answer is **acted on automatically, at scale, with nobody
reading it**.

**R1 — a decomposition that cannot state its search width is refused.**
Not defaulted, not estimated. §4.1 and §4.3: without it the significance cut
cannot be set and the result is a confident chain built from noise.

**R2 — a contribution share is refused when the net change is small relative to
the gross movement behind it.** The honest output is the signed contributions
and a statement that no share is reportable (§4.4). A percentage that happens
to be computable is not a percentage that means anything.

**R3 — an association is not an explanation, and the model says so.**
A `Finding` carries `associations`. A `Hypothesis` is a separate value. A
hypothesis becomes an explanation only by passing a **declared test** whose
result is recorded. There is no path from correlation to a stated cause that
does not go through a recorded test.

**R4 — `confidence`, `support` and `assurance` may not be compared or
thresholded together** (§4.5). Raise rather than coerce.

**R5 — an action whose process has never been replayed against history is
refused.** Simulation is a precondition, not an eventual feature (§10).

**R6 — authority is enforced at the action, never at the decision.**
A decision may freely recommend what it is not authorised to execute; that is
useful information, and suppressing it hides exactly the cases a human most
needs to see. The refusal belongs where the authority is actually spent.

**R7 — an outcome that was never measured is not evidence.** A prior action may
be cited only if its outcome was recorded (§11). Otherwise *we did this before
and it worked* enters the system as a fact when it is a memory.

**R8 — the null must be declared, not inferred** (§7).

**R11 — two hypotheses that predict the same pattern may not be ranked against
each other on that pattern.** Produced by Recipe 8. Ordering them would invent
a preference the evidence does not support; they are reported tied, with both
discriminators offered as the observation that would separate them. And no
probability is claimed anywhere: what is computed is *set agreement* between
predicted and affected cells, which travels with its own definition, because
the charter's `confidence .91` for a cause is a number nothing in the data
supports.

**R10 — an outcome measured without a comparison is not evidence that the
action worked.** Only that something happened next. Produced by Recipe 7, and
the bias is not a tail risk: an action is taken *precisely when* a measure is
extreme, so the entire learning corpus is drawn from the one condition where
the naive measurement is systematically wrong. Measured — an intervention whose
true effect was **exactly zero** showed a **48.7% apparent recovery**, which is
the number a learning loop would have stored, and is entirely regression to the
mean. The cells nobody touched recovered just as well. `as_evidence` refuses an
uncontrolled outcome; `as_observation` reads it labelled.

**R9 — a decision may not be sized off a quantity the Finding declined to
establish.** R2's consequence one layer along, and produced by Recipe 5 rather
than reasoned to. Measured: the same intervention over the same data gives
expected value **+7,497** sized off the aggregate decline and **−4,899** sized
off the cell that actually cleared — ACT against DO NOT ACT — with the Finding
having already said the aggregate was not established. Enforced at the boundary
in `decision.evaluate` and again structurally in `reasoning.decision`, so a
hand-built Decision cannot smuggle one past.

---

## 9. Provenance

Automated reasoning must be inspectable after the fact:

```text
DATA → FINDING → EVIDENCE → DECISION → POLICY → ACTION → OUTCOME
```

A Finding records which data, which period, which method, which parameters,
which assumptions, which hypotheses were considered, and which evidence
supported the conclusion. An Action records the finding that initiated it, the
decision that recommended it, the policy permitting it, the authority under
which it ran, the parameters, the external result, and the observed outcome.

This is not a logging feature. R5, R6 and R7 are all unenforceable without it.

## 10. Simulation is a precondition

Replay historical data through a process and record what it **would** have
done. That answers: how often would this have fired, how many actions would
have needed approval, how many would later look correct, what would the impact
have been, and where would the false alarms have been.

The charter treated this as a capability to add eventually. R5 makes it a
**gate**: a process that has never been dry-run has no evidence about its own
behaviour, and is the cheapest safety property in the design.

## 11. Outcomes and learning

An action's expected outcome is recorded with it, and the observed outcome is
measured afterwards. That closes the loop and creates organisational memory —
and note that *learning* here need not mean machine learning. Simply preserving
structured organisational experience, and refusing to cite it when it was never
measured (R7), is most of the value.

## 12. LLMs assist; they do not compute

`stats`, `finance`, the optimisation and forecasting algorithms, and explicit
rules produce the quantitative evidence. An LLM may communicate findings,
interpret unstructured text, generate candidate hypotheses, summarise evidence,
talk to humans, and translate natural-language objectives into candidate
structures.

The numbers must remain independently inspectable. An LLM that turns
inspectable reasoning into an opaque text-generation pipeline has removed the
property this design exists to provide.

---

## 13. Increments

Deliberately one function at a time.

### Built

- **`reasoning.finding` + `insight.explain_change`** — below.
- **`decision.quantity`** (Recipe 10,
  [automation_recipe_10_what_price.md](automation_recipe_10_what_price.md)):
  the first recipe of a different shape, and the one that showed
  `evaluate`'s list-of-alternatives does not fit every decision. Where the
  answer is a continuous quantity produced by a model, the parameter's
  interval is carried through the model rather than around it: **R12** refuses
  a recommendation when that interval reaches a value at which the model is
  undefined, and `amplification` reports how hard the model magnifies
  uncertainty — 8.85 in the worked case, where a 1.5x parameter interval
  becomes a 13.5x price interval.
- **`decision.calibrate`** (Recipe 9,
  [automation_recipe_09_how_much_evidence.md](automation_recipe_09_how_much_evidence.md)):
  turns the loop. Takes controlled evidence and produces the assumption, and
  answers *how much evidence is enough* the only way the question has an answer
  — relative to a decision, when the interval stops straddling the break-even.
  Retires the invented `sensitivity_range`: the sweep now runs over the range
  the evidence supports.
- **`reasoning.hypothesis` + `insight.weigh`** (Recipe 8,
  [automation_recipe_08_why_might_it_be.md](automation_recipe_08_why_might_it_be.md)):
  §4's third rung, and built so it cannot reach the fifth — every hypothesis
  carries `explains: false` and only a recorded test could promote one. A
  hypothesis predicts WHICH cells should have moved; comparing that with the
  cells that cleared is a contingency, not a probability. Parsimony falls out
  rather than being imposed. A `discriminator` is required, because a
  hypothesis nobody can imagine a test for is a story.
- **`automation.assign` + controlled outcomes** (Recipe 7,
  [automation_recipe_07_did_it_work.md](automation_recipe_07_did_it_work.md)):
  the missing concept of deliberately **not** acting, so the effect of acting
  is measurable at all; deterministic in the key, because a replay must make
  the same assignments as the live run.
- **`automation.would` / `rehearsal` / `execute` / `observe`** (Recipe 6,
  [automation_recipe_06_should_we_act.md](automation_recipe_06_should_we_act.md)):
  the only layer that changes external state, and it changes nothing itself —
  the executor is a function value the caller supplies, called only past the
  gate. Enforces R5 and R6, and the dry run and the live path **share one
  gate**, or a rehearsal would describe a different program than the one that
  runs.
- **`decision.evaluate`** (Recipe 5,
  [automation_recipe_05_what_to_do.md](automation_recipe_05_what_to_do.md)):
  scores every alternative, computes materiality from the Context, states the
  authority a recommendation needs **without enforcing it** (R6), reports
  assurance as a sensitivity sweep with the crossing named, and refuses R9. Its
  value turned out not to be the choice at all — everything useful is beside
  the recommendation.

### The first one, for reference

```text
reasoning.finding(spec)                  construct + validate a Finding
insight.explain_change(frame, spec)      -> Finding
```

`spec` declares: `measure`, `period`, `dimensions`, `comparison`, and `null`.

It must:

1. compute the observation and the per-level signed contributions;
2. compute the null from the declared source and the threshold from the search
   width (§4.2, §4.3);
3. report `share` per contributor, or `unknown` with a reason (R2);
4. record the search width, the null, and full provenance (R1, R8, §9);
5. carry `associations` and never a cause (R3).

**Why this and nothing else.** Recipe 1 proved this is where the danger is, and
every other object in the model is downstream of a Finding. A `decision` layer
built on an untrustworthy Finding would be built on sand.

## 14. Validation

Self-checking, not golden, and here it is forced: every defect this design
exists to prevent produces a **confident, ordinary-looking causal story**, and a
golden would record it as expected.

- **The planted-versus-null pair**, as Recipe 1 already does: the same
  decomposition over a real planted cause and over pure noise, required to
  reach opposite verdicts while producing the same headline. This is the
  load-bearing test and it must survive every change.
- **The threshold is derived, not constant**: asserted at several search widths,
  since a hardcoded cut silently stops scaling.
- **Share refusal**: a population with large offsetting movements must report
  no share rather than a computable one, with a control beside it — an ordinary
  population must still report shares, or a library that refuses everything
  passes.
- **Provenance completeness**: a Finding that cannot answer §9's questions
  fails, checked structurally rather than by reading.
- **Refusals with controls**, each beside its nearest legal neighbour.

## 15. The recipe programme

### The breadth target

Twelve scenarios, from the charter, unchanged as the eventual coverage target:

1. **Sales decline investigation** — where is a decline concentrated, and why. *(done, §4)*
2. **Inventory shortage response** — estimate lost sales, compare replenishment, act within purchasing authority.
3. **Advertising allocation** — campaign performance with delayed effects; marginal return.
4. **Customer churn investigation** — who is leaving, what changed around them, which intervention.
5. **Unusual expense investigation** — legitimate change, error, waste, or something worse.
6. **Cash-flow protection** — forecast liquidity, detect a coming shortfall, act preventively.
7. **Staffing optimisation** — workload forecast against cost, availability and service objectives.
8. **Supplier performance** — price, timeliness, quality, and the downstream consequence.
9. **Pricing review** — margin and demand changes; effects of alternative prices under risk.
10. **External market shock** — internal and external data together.
11. **Operational bottleneck** — where delay accumulates, and what it costs.
12. **Management by exception** — review everything, involve a human only when material, uncertain, unusual, risky, or beyond authority.

Later stress tests, deliberately harder: dynamic purchasing, automated
promotions, service intervention from text plus numbers, fraud response with
explicit false-positive cost, multi-department allocation, designed business
experiments, strategic early warning — and the **nearly autonomous small
business**, which is the real architectural stress test and is expected to
force persistent goals, budgets, commitments, competing objectives,
organisational roles, cross-process coordination, exception queues and
long-term memory.

### The depth plan, which comes first

The architecture is likelier to be corrected by depth than breadth, so the next
four are chosen to **break** the model rather than broaden it:

| next | what it attacks |
|---|---|
| **2. Two causes at once** | the drill-down's greedy first step is wrong |
| **3. A seasonal measure** | "ordinary" is not stationary; R8 exercised rather than assumed |
| **4. A high-cardinality dimension** | search width dominates; almost nothing should survive |
| ~~**5. The first recipe that decides**~~ | *done — produced R9, and `decision.evaluate`* |

Recipes 5, 6 and 7 were the important ones and all three are done. The
architecture is closed and the learning loop has a mechanism — and Recipe 7
found that the obvious way to close it teaches the system something false
(R10).

Recipe 8 built the third rung of §4's ladder. What remains is to **turn** the
loop: nothing yet takes a controlled effect and
feeds it into a later decision as the recovery assumption. That is a small step
now, but it wants its own recipe, because the interesting question is what
happens when the evidence is thin — one controlled observation is not a
calibration, and there is no rule yet for how much evidence is enough. That,
and recipes 2–4.

## 16. Deferred, with reasons

- **`business.bas` facade** — would freeze boundaries the recipe programme
  exists to test. After stabilisation, not before.
- **Optimisation** (`MAXIMIZE … SUBJECT TO`) — needs a solver, and the
  gBASIC-wide question of adapting to a mature one rather than implementing
  linear programming here.
- **External context** (weather, commodities, competitor pricing) — the
  acquisition side is `web`/`market`'s job and the reasoning side changes
  nothing above; it is breadth, and it can wait for a recipe that needs it.
- **Automatic hypothesis generation** — R3 makes a hypothesis cheap to propose
  and expensive to promote, which is the right order; generating candidates is
  then a convenience, and a good early use for an LLM (§12).
- **Cost-asymmetry primitives** (`COST_OF_FALSE_POSITIVE`, `REVERSIBILITY`) —
  clearly real, and clearly belonging to `decision`; they want the first
  deciding recipe to shape them rather than a guess now.
- **`METRIC`/`SIGNAL`/`EVIDENCE`/`RISK`/`GOAL` as first-class objects** — some
  are probably fields. Recipe 1 needed none of them (§6.3).

## 17. Working philosophy

This is exploratory language and library design, not feature implementation.
The interesting question is not *how can gBASIC automate a sales report* but:

> What abstractions let a programmer encode the reasoning by which a business
> notices that something matters, discovers what is happening, decides what to
> do, safely acts, and learns whether that action worked?

Principles, unchanged from the charter and now with teeth in §8: evidence
before magic; uncertainty is information; humans are part of the architecture;
automation must have authority boundaries; actions must be auditable; outcomes
matter; composition over monoliths; business language matters; LLMs assist
rather than obscure; and when an elegant abstraction conflicts with a
demonstrated recipe requirement, investigate the requirement before defending
the abstraction.

Recipe 1 is the first instance of that last principle doing its job. The
abstraction was §11's decomposition; the requirement was that it be able to
tell a cause from noise; the requirement won.
