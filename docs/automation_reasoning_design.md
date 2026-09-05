# Business Automation Reasoning

Status: **Partial, and reconciled against the code 2026-09-04.** All three
layers have a first increment — `stdlib/reasoning.bas`, `stdlib/insight.bas`
(`tests/run_insight.sh`), `stdlib/decision.bas` (`tests/run_decision.sh`) and
`stdlib/automation.bas` (`tests/run_automation.sh`), plus the manual calibration
tier (`tests/run_insight_calibration.sh`). The architecture is closed end to
end: a Finding becomes a Decision becomes a gated Action becomes a measured
Outcome, and a measured Outcome becomes the assumption a later Decision rests
on.

**Eleven recipes are built, executable and asserted** — `examples/automation_lab/`,
each with a write-up in `docs/`. The depth plan §15 set out is discharged; what
is not built is **breadth**, which is §15's twelve scenarios and mostly untouched.

What every recipe still has in common, and it is the largest standing
limitation: **all of them run on data this project generated.** The
measurements are real measurements of this library's behaviour and none of them
is a measurement of a real business.

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

### 4.8 Seasonality does not cause false alarms. It causes blindness.

*Measured by Recipe 3 (`examples/automation_lab/09_a_seasonal_measure.bas`,
asserted in `tests/run_insight.sh`).* The expectation going in was the obvious
one: a cross-sectional null under a seasonal measure would flag the
high-amplitude cells and produce confident nonsense about toys. It does not,
and what it does instead is worse.

Twenty-four cells, December against January, categories with their ordinary
seasonal profiles — toys triple into Christmas and collapse after it. Two runs
of the same population, one with a real 45% collapse planted in a
flat-seasonal cell:

| | run A, nothing wrong | run B, a real 45% collapse |
|---|---|---|
| headline | −50.1% | −51.5% |
| net *t* | −4.49 | −4.65 |
| leader | West / Toys, z −3.35 | West / Toys, z −3.33 |
| cells clearing | 0 | 0 |
| the collapsed cell's z | +0.52 | **−0.09** |

The two runs are the same answer. And the cell that lost 45% of its revenue
did not merely fail to stand out — **it moved towards the middle**. The
mechanism is general and has nothing to do with seasons: a shift shared by
every cell inflates the cross-sectional spread *in proportion to itself*, so it
raises the detection floor for everything. Seasonality is one way to get one.
Isolated in the simplest possible population — every cell down 40%, nothing
else — the same planted collapse that clears on its own no longer clears.

Two corrections came out of it.

**R13**, above: the population moving together is visible in the data at no
cost, and a movement common to every cell is not explained by any of them.

**`versus_last_year` was declared and not implemented.** §7 had named three
comparisons since the rewrite, `reasoning.comparisons()` returned one, and the
error message beside it listed all three in prose. The remedy for a seasonal
measure could not be spelled. With it, the same data, library and threshold
that found nothing at December-against-January recovers the planted cell at
z −4.91, ranked first, clearing. **The fix for seasonality is the comparison,
not the null** — which is the opposite of where this section expected to end
up, and is why a third, temporal null is *not* being added.

A third result arrived unasked: run D plants nothing, compares January with
January, and a cell clears anyway at z 3.59 — the *t* threshold's known
miscalibration (§4.3), seen live rather than in a Monte Carlo. Repeating runs
C and D under `siblings_permuted` removes the false one and keeps the true one,
which is a confirmation of that null on a population it was never tuned
against.

### 4.9 The test finds a problem only while it is nearly the only problem

*Measured by Recipe 2 (`examples/automation_lab/10_two_causes_at_once.bas`,
asserted in `tests/run_insight.sh`).* §4.8 found that a movement shared by every
cell blinds the cross-sectional test. Recipe 2 asks the narrower and more
ordinary question: what if just *two* things have gone wrong?

The experiment holds one cell literally constant and varies only its
neighbours, so anything that moves its verdict is a fact about cells it has
nothing to do with. It moves a great deal — the table is in **R14** below. One
unrelated cause takes a third of the watched cell's z away; three take two
thirds; at four it is no longer reported at all.

This is the same mechanism as §4.8 in miniature, and the two together are one
result: **the reference population is contaminated by exactly the thing being
looked for.** §4.8's version is contamination by a shared shift, §4.9's is
contamination by other instances of the same failure. Leave-one-out, added in
Recipe 6, addressed only the special case where the contaminating cell *is* the
cell under test.

The correction (R14) is a declared bound on how many cells may be wrong at
once, and its interest is that it is **priced**: allowing for more causes
raises the bar for every cause, and the recipe's last row is the one where the
allowance has outrun the evidence. That row is deliberately in the golden — a
recipe that showed only successful recoveries would teach that `max_causes`
repairs the problem, when what it does is move it.

### 4.10 The multiplicity correction is cheap; the support is not

*Measured by Recipe 4 (`examples/automation_lab/11_a_high_cardinality_dimension.bas`,
asserted in `tests/run_insight.sh`).* R1 made the search width a first-class
part of every Finding on the argument that a z remarkable across four regions is
unremarkable across two hundred product families. That argument is sound and its
practical weight was overestimated: **the correction it buys costs 18% of the
threshold across a fiftyfold widening.**

The recipe was written to confirm the depth plan's prediction and refuted it,
which is the whole reason for running rather than reasoning. What actually
limits a fine-grained decomposition is that the cells stop containing enough
business to say anything about. The correction (R15) is not a refusal but a
disclosure, and the case for it is that today's silence is indistinguishable
from health.

Together with §4.8 and §4.9 this completes a picture the design did not have.
Three separate things degrade the cross-sectional test, and **only one of them
was in the original architecture**: multiplicity (R1, and it is the mild one),
contamination of the reference by the thing being sought (R13, R14), and lack of
support (R15). The first is priced into the threshold; the second is a declared
bound; the third can only be reported.

### 4.11 An effect is a bare number, and bare numbers average

*Measured by Recipe 11 (`examples/automation_lab/12_what_was_this_about.bas`,
asserted in `tests/run_decision.sh`).* Recipe 9 turned the loop: controlled
outcomes become the assumption a later decision rests on. It measured **one**
intervention, so the question a real learning loop meets on its first day never
came up — an organisation runs several campaigns at once and stores every
measured action in one place.

Two campaigns, both gated, both measured against a holdout, both read back
through `as_evidence`, so R10 is satisfied and every observation is controlled.
*Send a manager* truly recovers 0.35; *cut the price* truly recovers 0.05;
against a 16,835 loss an action costing 2,000 breaks even at 0.119. Asked one
at a time the library gets both right — ACT and DO NOT ACT, each with assurance
1. Pooled, it recommends **both**.

| pool | estimate | 95% interval | width |
|---|---|---|---|
| 12 | 0.180 | 0.063 to 0.297 | 0.234 |
| 24 | 0.197 | 0.108 to 0.287 | 0.178 |
| 48 | 0.190 | 0.128 to 0.252 | 0.125 |
| 120 | 0.198 | 0.161 to 0.235 | 0.074 |

**The standard error falls with the size of the pool**, so gathering more
mismatched evidence makes the wrong number more confident. By 120 observations
the interval contains neither truth, and the decision sized off it reports
`assurance 1` — DECISIVE, from 120 controlled observations, about an
intervention that loses money every time it is taken. That is precisely the
shape §4 taught us to trust: a narrow interval well clear of the break-even.

Nothing in the data objects. All 24 outcomes are revenue, all from gated
actions, all measured the same way. **The pooled mean is a real quantity — it
is simply not the answer to the question asked**, and no property of the
arithmetic can notice that. What was missing was upstream: the evidence could
not say what it was about, because an Action did not record it. §9 had asked
for exactly that from the start and called it *not a logging feature*; this is
what it was not a logging feature **for**.

### 4.12 `assurance` is a sensitivity to one dial

*Measured while writing the definition down (`tests/decision_test.bas`, R17
tier). Not a lab recipe — this one came out of the 0.1 audit, and it is
recorded here because the measurement is the same kind.*

Two of the three derived numbers this design produces already travel with their
own definition: `insight.weigh` reports `agreement_is` ("set agreement between
predicted and affected cells, NOT a probability that the hypothesis is true")
and `decision.quantity` reports `amplification_is`. **`assurance` carried
none** — and it is the one most likely to be misread, because a bare scalar in
[0, 1] printed beside a recommendation reads as the probability that the
recommendation is right.

Writing the definition is what exposed the limitation. Assurance is *the share
of the swept range of the recovery assumption over which the recommendation does
not change* — and everything else is held at a point estimate. Measured, with
the recovery calibrated to [0.13, 0.17] from 40 controlled outcomes:

| sized-off loss | expected value | recommendation | assurance |
|---|---|---|---|
| −16,835 (the cell's own change) | +525 | send a manager | **1** |
| −12,000 | 0 | do nothing | — |

`assurance 1`, no sensitivity reported, and a third off the quantity the
recovery is a fraction **of** reverses the answer. R9 established that *which*
quantity a decision is sized off turns +7,497 into −4,899; this is the same
knife one turn along, and assurance is silent about it by construction.

There is a second thing the number does not say on its own. `assurance 1` over
an interval that 40 controlled outcomes support is a different claim from
`assurance 1` over a span the caller declared, and until now nothing in the
value distinguished them — the sweep simply ran over whichever range it was
given.

**The remedy is disclosure, not a wider sweep.** The library must not invent an
interval for the sized-off quantity: the cell's change is *observed*, not
estimated, and what is genuinely uncertain — how much of an ongoing loss an
action recovers — is precisely what the calibration already covers. What was
missing was a reader being told which dial was turned.

### 4.13 The correction is per run, and a campaign is many runs

*Measured by `tests/insight_calibration.bas` (manual tier — ~5 minutes over
freshly generated null data).* R1 made the search width a first-class part of
every Finding, and every tier that has ever checked the resulting threshold has
checked **one search**. A monitoring process asks the same question every
month. Twelve searches are twelve families, and nothing anywhere said the
correction covered only one.

Measured over a population with **nothing wrong in it**, twelve monthly runs,
firing if any month raises a finding:

| | campaign false-alarm rate |
|---|---|
| repetition undeclared | **0.725** |
| `repetitions: 12` declared | **0.175** |

**This is not the tail-weight problem §4.3 already records.** Even a perfectly
calibrated 0.05 per run gives 1 − 0.95¹² = **0.46** over a year. It is
arithmetic about repetition, and it applies to a correctly calibrated library
just as much as to this one.

The remedy is a declaration, and it is priced. At 20 cells the bar goes 3.51 →
4.63 for a year of monthly runs, 5.31 weekly, 6.25 daily — and by R15 the bar
*is* the smallest change the search can find, so declaring a year of monitoring
raises the smallest detectable change by about a third. That is a trade to make
deliberately, which is why it is declared rather than inferred.

And the honest third fact: **0.175 is not 0.05.** Declaring the repetition cuts
the campaign rate roughly fourfold and does not deliver alpha, for the same
tail-weight reason `null.calibration` already carries, one level deeper into
the tail. Whether `siblings_permuted` closes that gap at campaign level is
**not measured** — the run was attempted and is expensive enough (~1.4 s a
call, so ~6 minutes for a usable estimate) to want its own tier rather than a
guess here.

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
    authority           what THIS process may execute without a human
    approval            who signed off, where one was required
```

A `Context` is data. It is loaded, versioned, diffed and audited like any other
business record, which is what makes "why was this allowed?" answerable later.
It is therefore not *constructed* by `reasoning.bas` — but its field names are
checked there, by both layers that read one, and **an unrecognised name is
refused rather than ignored**. Measured: a context written with `objective` and
`threshold` instead of the plurals produced a decision with `direction:
"unstated"` and `materiality: unknown` and raised nothing — and `materiality:
unknown` is the *designed* honest answer when no threshold was declared (§4.6),
so a typo was indistinguishable from a deliberate omission. The decision layer
accepts `approval` even though only `automation` reads it, because one context
serves both.

Two fields sketched in earlier drafts are **not** implemented and are not
placeholders for work in progress: `policies[]`, because nothing has yet needed
a policy separate from the authority envelope, and `null_policy`, because the
null is declared per search in the `insight` spec (§7) where the search width
that sets its threshold lives.

A convenience facade (`business.bas`) is **deferred** (§16) until the three
libraries have stabilised. Building it early would freeze boundaries the recipe
programme exists to test.

---

## 6. The shared reasoning model

The charter listed fourteen candidate objects whose representations "should
emerge from experimentation". Eleven recipes later, six are specified and the
rest are still not objects.

**These shapes are what the code produces**, not what an earlier draft hoped
for; §6 has been reconciled against `reasoning.bas` rather than re-read.

### 6.1 Finding

```text
Finding
    subject                 what was examined
    measure                 the quantity ("revenue")
    period                  { baseline, current }
    comparison              period_over_period | versus_last_year      §7
    observation             { baseline, current, change, change_pct }
    search                  { dimensions[], cells, width,
                              alpha_requested, correction, repetitions,
                              max_causes, detectable }        REQUIRED  §4.3
    null                    { kind, mean, sd, threshold, net_t,
                              standardized, df, common_movement,
                              calibration }                   DECLARED  §7
    strength                { z, clears, leader, clearing }             §4.2
    contributors[]          { path, baseline, current, change,
                              share | unknown, z, clears }              §4.4
    shares_reportable       false when R2 or R13 withheld them
    shares_withheld_because the reason, in words
    associations[]          measures that moved with it — never "causes"
    hypotheses[]            Hypothesis values, untested until tested
    provenance              { method, rows, parameters, assumptions }   §9
```

Every field beyond the original sketch was added by a recipe that measured why
it was needed: `alpha_requested` and `null.calibration` by the calibration
tier, `repetitions` by §4.13, `max_causes` by §4.9, `detectable` by §4.10,
`common_movement` by §4.8, `strength.clearing` by §4.9.

Note what is **absent**: no `materiality` (§4.6), no bare `confidence` (§4.5),
no `alpha` that would imply a rate was delivered rather than requested, and no
`cause` (R3).

### 6.2 Context — §5.

### 6.3 Decision

```text
Decision
    objective               { measure, direction }, from the Context
    finding                 the Finding that initiated it              §9, R16
    materiality             computed from the Context, `unknown` if
                            no threshold was declared                    §4.6
    alternatives[]          each scored
    recommendation          the best alternative, NOT the best affordable  R6
    expected_value
    authority_required      stated here, enforced at the action           R6
    authority_reason
    assurance               a share of a swept range                    §4.12
    assurance_is            { definition, swept, over, from, steps,
                              held_fixed, is_not }                   §4.12, R17
    sensitivities[]         where the recommendation changes
    sized_off               { quantity, established, value }              R9
    provenance
```

`decision.quantity` answers a different shape of question — a continuous
quantity from a model rather than a choice among alternatives (§4, Recipe 10) —
and returns `{ recommended, model, defined, low, high, parameter,
parameter_spread, quantity_spread, amplification, amplification_is }`, or
`{ recommended: unknown, defined: false, broke_at, point_estimate_would_say,
why }` where the model has no answer over the whole interval (R12).

### 6.4 Action and Outcome

```text
Action
    decision                the Decision, which carries the Finding      §9
    context                 the policy in force when it ran              §9
    rehearsal               { periods, fired, false_alarms, missed,
                              needed_approval, false_alarm_rate }        R5
    authority               { needed, granted_by, reason }               R6
    result                  what the executor reported
    provenance
    outcome                 added later by automation.observe            R7

Outcome
    expected, observed, measured_at, met
    controlled              false unless a holdout was supplied         R10
    holdout, effect         present only when it was
```

An Action read back through `reasoning.as_evidence` becomes
`{ kind, decision, about: { measure, intervention }, expected, observed,
holdout, effect, met, controlled }` — and R10 refuses to produce one at all
without a comparison, while R16 uses `about` to refuse pooling it with evidence
about a different question. `reasoning.as_observation` reads the same Action
honestly labelled, carrying its own caveat.

### 6.5 Hypothesis

```text
Hypothesis
    name
    predicts                dimension -> value constraints, declarative
    discriminator           REQUIRED: the observation that would separate
                            this from its rivals
    rationale
    explains                false, for its whole life                    R3
```

`insight.weigh` returns `{ affected_cells, hypotheses[], leader,
leader_is_separable, indistinguishable[], agreement_is, next_test }`. No
probability appears anywhere in it (R11).

### 6.6 Still not objects

The charter's `METRIC`, `SIGNAL`, `EVIDENCE`, `FORECAST`, `RISK`, `GOAL`,
`CONSTRAINT`, `ALTERNATIVE`, `POLICY` are **not** adopted as objects. Eleven
recipes have needed none of them as types: `EVIDENCE` turned out to be what
`as_evidence` returns from an Action, `ALTERNATIVE` a record in a list,
`GOAL` and `CONSTRAINT` fields of the Context. `FORECAST` is the one with a
real gap behind it — `versus_forecast` is absent from §7 because nothing in
this tree produces a forecast.

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
  neither looks at time. **Recipe 3 answered what that costs, and the answer
  was not the expected one** (§4.8): under seasonality the cross-sectional null
  does not produce false alarms about cells, it produces *blindness* — a cell
  that lost 45% of its revenue became **more** ordinary-looking, because a
  common shift inflates the spread every cell is judged against in proportion
  to itself. The fix is the comparison, not the null.
- **The comparison** — `period_over_period` or `versus_last_year`. These
  disagree routinely and each is right for a different question, and comparing
  like with like is the whole remedy for a seasonal measure: the same data,
  library and threshold that found nothing at December-against-January recovers
  the planted cell at z −4.91, ranked first, at January-against-January.
  `versus_forecast` is still absent because it needs a forecast, which nothing
  in this tree produces yet.
- **How many causes at once** — `max_causes`, default 1. The number of cells
  that may be simultaneously wrong is not discoverable from the data: allowing
  for more is what lets a second cause be seen at all, and it raises the bar
  for every cause (§4.9, R14). Above 1 it requires the permuted null.
- **How many times the search will be run** — `repetitions`, default 1. The
  correction is family-wise over the cells of one search, and a monitoring
  process asks the same question every month; twelve runs are twelve families
  (§4.13, R18). Not discoverable from the data, and priced: the bar rises with
  the family, and by R15 the bar is the smallest change the search can find.
- **The authority envelope** — what this process may do without a human.
  Never a default; an unset authority means *nothing may execute* — and a
  misspelled one is an unset one, which is why an unrecognised Context field is
  refused by name (§5).
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

**R13 — a share is refused when the movement is common to the population.**
Produced by Recipe 3. R2 asks whether the net moved *at all*; R13 asks whether
it moved *everywhere*, and the two are not the same question. Measured: an
ordinary December-to-January comparison over a population with **nothing wrong
in it** fell 50.1%, the net test established it at t = −4.49, and shares were
duly reported — a confident attribution of an ordinary January to whichever
cell sells the most toys. The signature costs nothing to look for: **22 of 24
cells moved the same way**, a one-in-thirty-thousand event under a null where a
cell is as likely to rise as to fall. A sign test decides it, and assumes less
than the t test beside it already assumes — only the direction, never the shape
of how far.

It is **not a seasonality detector and must not be read as one.** A real
company-wide collapse moves 22 of 24 cells down, and so does a broken feed.
What is established is what R2 establishes one level down: *the decomposition
has not located this*. Which of the three it is, is outside the data.

**R14 — how many things may be wrong at once is declared, not assumed.**
Produced by Recipe 2. Leave-one-out removes a cell from *its own* reference and
nothing else. Holding one cell literally constant — the same collapse, the same
change of −25,728 — and varying only how many **unrelated** cells collapsed
beside it:

| others broken | reference mean | reference sd | z | verdict |
|---|---|---|---|---|
| 0 | −666 | 6,974 | −5.70 | found |
| 1 | −1,643 | 8,472 | −3.65 | found |
| 2 | −2,663 | 9,668 | −2.83 | nothing |
| 4 | −4,942 | 12,213 | −1.86 | nothing |

Both terms move against detection at once: the reference mean slides *towards*
the anomaly and the reference spread inflates. **The test finds a problem only
while it is nearly the only problem** — and deepening the collapse does not
help, because the cells contaminating the reference deepen with it.

Two obvious repairs were measured and both failed, which is why this is a
declared choice rather than a better default. **Sequential peeling** does not
help, because the *first* test is the most contaminated and it is the one that
decides whether anything is reported at all. **A robust median/MAD scale** does
not help either: at 24 cells the MAD itself rose 28% between one cause and two,
and once its own null threshold is measured honestly (4.18 against the t
formula's 3.49) the robust statistic is *further* from clearing.

What works is excluding the other candidates from the **reference** — not
blessing them as findings, merely declining to let them define what ordinary
looks like. It restores the statistic to a property of the cell: the trimmed z
is −5.59, −5.46, −5.48 across the same runs whose untrimmed z fell from −3.65 to
−1.86. **And it is not free.** The bar rises with what is allowed for — 4.35,
4.84, 6.46 — so by five causes in twenty-four cells it has outrun the evidence.
A trade with a limit, not a repair.

`max_causes` therefore belongs to the caller, defaulting to 1 because that is
what this library has always silently assumed; naming it changes no existing
answer, and that is asserted. Above 1 it **requires `siblings_permuted`**:
trimming the reference changes the statistic, and the t quantile is a formula
for the untrimmed one — not approximately right but a threshold for a different
quantity, erring towards reporting causes that are not there.

**R15 — a Finding states the smallest change it could have found.**
Produced by Recipe 4, which was run to confirm a prediction and refuted it. The
depth plan said *search width dominates; almost nothing should survive*. It does
not dominate: cutting one business of fixed size into 24, 240 and 1,200 cells
moves the threshold only 3.49 → 3.77 → 4.11, because `sqrt(2 ln n)` grows about
as slowly as anything in statistics. **Paying for the multiplicity of a wide
search is nearly free.**

What dominates is **support**. Cut the same business finer and each cell holds
less of it, so each cell's ordinary variation grows relative to its size. The
same planted collapse — 75% of a cell, in all three runs — falls from z −14.6 to
−3.3 and stops being reported; at the finest cut a *different* cell clears
instead.

| cells | a typical cell bills | smallest detectable change |
|---|---|---|
| 24 | 635,515 | 113,554 (18% of a cell) |
| 240 | 65,011 | 34,303 (53% of a cell) |
| 1,200 | 12,349 | 18,096 (**147% of a cell**) |

At 1,200 cells the bar exceeds a cell's entire revenue: **no decline, however
complete, could be reported.** A cell could go to zero and the answer would
still be *within ordinary variation* — in exactly the words used when a business
is healthy.

So every Finding carries `search.detectable`: the smallest change the search was
capable of finding, in the units of the business and as a share of a typical
cell, **reported whether or not anything cleared** — because *nothing cleared*
is precisely when a reader needs to know whether the search could clear
anything at all. It is a falsifiable number, not a formula echo: a collapse
below the stated bar does not clear and one above it does, and that is asserted.

**R9 — a decision may not be sized off a quantity the Finding declined to
establish.** R2's consequence one layer along, and produced by Recipe 5 rather
than reasoned to. Measured: the same intervention over the same data gives
expected value **+7,497** sized off the aggregate decline and **−4,899** sized
off the cell that actually cleared — ACT against DO NOT ACT — with the Finding
having already said the aggregate was not established. Enforced at the boundary
in `decision.evaluate` and again structurally in `reasoning.decision`, so a
hand-built Decision cannot smuggle one past.

**R16 — a calibration may not pool evidence about different questions.**
Produced by Recipe 11 (§4.11). Every item must agree on the **measure** and the
**intervention**, and the resulting calibration carries both. Cells are
deliberately *not* part of the rule: pooling across the places a thing was
tried is the entire purpose of a calibration, and a refusal that took that away
would be indistinguishable from having no calibration at all — so the control
is asserted beside the refusal.

The rule is only expressible because the chain carries it. A Decision names the
Finding it came from, so it knows the measure; an Action carries the Decision
and the policy that permitted it; evidence read back off an Action can
therefore say what it was about. Each link is required rather than encouraged,
because a rule that only the happy path obeys is a convention.

**R17 — `assurance` travels with its definition, including what it held
fixed.** §4.12. A Decision carries `assurance_is`: the definition, what was
swept, over which range, **where that range came from** (a calibration, naming
how much evidence it rests on, or a span the caller declared), and the list of
things held at a point estimate — the sized-off quantity and its value, the
costs, and the set of alternatives considered. It states plainly that it is not
a probability. Where nothing was sized by a recovery assumption there is no
assurance at all, and the value says why rather than reporting a number nobody
could interpret.

The same discipline is already applied to `agreement` and `amplification`.
`amplification_is` names what it is but does not yet list what it holds fixed,
which is the same gap one library along and is recorded here rather than
quietly fixed — `decision.quantity` deserves its own measurement first.

**R18 — how many times a search will be run is declared, and the correction
names its family.** §4.13. `repetitions` defaults to 1, which is what the
library always silently assumed, so naming it changes no existing answer — and
that is asserted. `search.correction` no longer says "bonferroni" and leaves the
reader to imagine the family; it says *bonferroni over the cells of this one
search*, or *over the cells of this search times 12 runs*. The declaration is
priced, like `max_causes`: the bar rises with the family and by R15 the bar is
the smallest change the search can find.

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

This is not a logging feature. R5, R6 and R7 are all unenforceable without it,
and R16 is not even *statable* without it — Recipe 11 measured what that costs.

**The finding reaches the Action through the Decision, not beside it.**
`reasoning.decision` requires a `finding` (at minimum its subject and measure,
which a Finding satisfies by being one), so containment carries it and there is
no second copy to disagree with the first. What `reasoning.action` checks is
that the link is actually there — a hand-built Decision naming no finding may
not enter the action layer, the same structural treatment R9 gets.

`context` is the policy: the objectives, thresholds and authority in force when
the action ran. An action that cannot say which regime permitted it cannot be
re-judged when the regime changes.

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

- **`reasoning.finding` + `insight.explain_change`** — below, and since
  extended by Recipe 3 (§4.8,
  [automation_recipe_03_a_seasonal_measure.md](automation_recipe_03_a_seasonal_measure.md)):
  **R13** refuses a share when the movement is common to the population, and
  `versus_last_year` became a comparison the code will actually accept rather
  than one this document merely named. The recipe's own finding is that a
  cross-sectional null under a seasonal measure does not raise false alarms —
  it goes **blind**, because a shift shared by every cell inflates the spread
  each cell is judged against in proportion to itself. Extended again by
  Recipe 2 (§4.9,
  [automation_recipe_02_two_causes_at_once.md](automation_recipe_02_two_causes_at_once.md)):
  **R14** and `max_causes`, after the same contamination was found in its
  smaller and far more ordinary form — two things wrong at once. And again by
  Recipe 4 (§4.10,
  [automation_recipe_04_a_high_cardinality_dimension.md](automation_recipe_04_a_high_cardinality_dimension.md)):
  **R15**, `search.detectable`, after the prediction that search width would
  dominate turned out to be wrong and support turned out to be everything.
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
  the evidence supports. Extended by Recipe 11 (§4.11,
  [automation_recipe_11_what_was_this_about.md](automation_recipe_11_what_was_this_about.md)):
  **R16** refuses to pool evidence about different questions, after the same
  machinery was measured recommending a loss-making intervention with
  `assurance 1` from 120 controlled observations. Making that refusal
  *statable* is what closed §9's gap — a Decision now names its Finding and an
  Action records the policy that permitted it.
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

- **From the 0.1 audit rather than a recipe** (2026-09-04): **R17**, which gave
  `assurance` the definition every other derived number in the layer already
  carried, and in doing so measured that a recommendation reporting
  `assurance 1` can reverse on a dial the sweep does not turn (§4.12); **R18**,
  `repetitions`, after measuring that an ordinary year raises a finding 72.5% of
  the time because the correction covers one search and a campaign is twelve
  (§4.13); and the Context field check, after measuring that a plural typo in a
  Context reports `materiality: unknown`, which is exactly what an honestly
  undeclared threshold reports (§5).

### The first increment, for reference

*Historical: this is what was specified before any of it was built, kept
because the sequencing argument is still the right one.*

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

The standing rules, each of which has caught something:

- **The planted-versus-null pair**, as Recipe 1 does: the same decomposition
  over a real planted cause and over pure noise, required to reach opposite
  verdicts while producing the same headline. This is the load-bearing test and
  it must survive every change.
- **The threshold is derived, not constant**: asserted at several search widths
  and now at several *repetition* counts, since a hardcoded cut silently stops
  scaling.
- **Refusals with controls**, each beside its nearest legal neighbour. A
  refusal tier with no control is satisfied by refusing everything, and that has
  been the near-miss more than once — R16 without its control would forbid the
  pooling a calibration exists for.
- **Provenance completeness**: a Finding that cannot answer §9's questions
  fails, checked structurally rather than by reading.
- **Assert a DIFFERENCE between two runs, not a value in a range.** This is the
  rule the programme learned the hard way and it is written down because it was
  violated four times: the sensitivity sweep (§4, Recipe 5), the calibration
  interval (Recipe 9), the model amplification (Recipe 10) and an interval
  bracket in Recipe 11's own tier each first got an assertion that passes on the
  broken implementation. What separates a right answer from a plausible wrong
  one is almost never that a number is large or has moved in the right
  direction.
- **A no-op default is asserted as a no-op.** `max_causes: 1` and
  `repetitions: 1` are what the library always silently assumed; a tier requires
  that naming them moves no existing answer, because a default that quietly
  moved one would be a behaviour change wearing a declaration's clothes.
- **Calibrations are checked, not trusted.** `tests/insight_calibration.bas` is
  a manual tier because measuring a false-positive rate costs hundreds of
  searches, and it is the only reason this design knows that its own threshold
  delivers 0.10–0.14 against a requested 0.05, and that an ordinary year raises
  a finding 72.5% of the time.

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
| ~~**2. Two causes at once**~~ | *done — produced R14, and `max_causes`* |
| ~~**3. A seasonal measure**~~ | *done — produced R13, and `versus_last_year`* |
| ~~**4. A high-cardinality dimension**~~ | *done — refuted the prediction; produced R15* |
| ~~**5. The first recipe that decides**~~ | *done — produced R9, and `decision.evaluate`* |
| ~~**11. What was this evidence about**~~ | *done — produced R16, and closed §9's provenance gap* |

Recipes 5, 6 and 7 were the important ones and all three are done. The
architecture is closed and the learning loop has a mechanism — and Recipe 7
found that the obvious way to close it teaches the system something false
(R10).

Recipe 8 built the third rung of §4's ladder. Recipe 9 **turned** the loop, and
Recipe 11 found what the loop does when more than one thing is being learned at
once: it averages them, confidently. Recipes 2, 3 and 4 then stressed the
insight layer and produced R13, R14 and R15.

The depth plan is discharged. What it leaves behind is a short list the
recipes themselves raised and did not settle:

- ~~**The correction is per-call, not per-campaign.**~~ *— done (R18, §4.13).
  Measured at 0.725 that an ordinary year raises a finding, against 0.175 once
  the repetition is declared. Open behind it: whether `siblings_permuted`
  closes the remaining gap to alpha at campaign level.*
- ~~**`assurance` is a bare scalar in [0, 1]**~~ *— done (R17, §4.12), and
  writing the definition is what found that a recommendation reporting
  `assurance 1` can reverse on a dial the sweep does not turn.*
- **Every executable recipe still runs on data this project generated.** The
  measurements are real measurements of the library's behaviour, and none of
  them is a measurement of a real business.

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
