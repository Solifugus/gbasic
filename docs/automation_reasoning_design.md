# gBASIC Business Automation Reasoning

## Project Charter and Initial Design Brief

### 1. Purpose

This project explores and develops a new capability for gBASIC: **Business Automation Reasoning**.

The central idea is that many business activities currently performed by managers, analysts, and other employees consist of repeatable reasoning over data:

* observing business conditions,
* detecting meaningful changes,
* investigating why they occurred,
* evaluating alternatives,
* making decisions under uncertainty,
* acting within defined authority,
* escalating decisions requiring human judgment,
* and measuring whether previous actions worked.

Traditional business intelligence systems generally stop at presenting information to a human:

```text
DATA
  ↓
REPORT / DASHBOARD
  ↓
HUMAN OBSERVATION
  ↓
HUMAN INVESTIGATION
  ↓
HUMAN DECISION
  ↓
ACTION
```

Business Automation Reasoning attempts to make much more of that process programmable:

```text
DATA
  ↓
INSIGHT
  ↓
DECISION
  ↓
AUTOMATION
  ↓
ACTION
  ↓
NEW BUSINESS STATE
  └──────────────→ INSIGHT ...
```

The programmer is not necessarily encoding the answer.

The programmer is encoding **a process for discovering, evaluating, and acting upon answers**.

This distinction is fundamental.

---

# 2. Vision

The long-term goal is to allow a gBASIC programmer to express portions of organizational reasoning directly in software.

For example, rather than merely writing:

```basic
IF sales < 100000 THEN
    EMAIL manager
END IF
```

a programmer should eventually be able to express something conceptually closer to:

```basic
WATCH sales

WHEN MATERIAL_CHANGE
    INVESTIGATE
    EXPLAIN
    CONSIDER alternatives
    DECIDE USING business_objectives
    ACT WITHIN delegated_authority
END
```

The implementation syntax above is illustrative only. One purpose of this project is to discover what the correct gBASIC API and syntax should actually be.

A sufficiently complete implementation could theoretically automate substantial portions of a business.

That is not equivalent to eliminating humans.

Humans remain particularly valuable where decisions involve:

* subjective judgment,
* ethics,
* relationships,
* negotiation,
* creativity,
* ambiguous objectives,
* organizational politics,
* novel circumstances,
* legal accountability,
* high-consequence authorization,
* and preferences that cannot reasonably be reduced to quantitative objectives.

A major architectural objective should therefore be **selective autonomy**.

Routine, measurable, well-understood decisions may be automated.

Uncertain, unusual, subjective, or consequential decisions may be escalated to humans.

---

# 3. Relationship to Existing gBASIC Libraries

gBASIC already contains many useful building blocks, including libraries concerned with areas such as:

* statistics,
* finance,
* accounting,
* lending,
* credit,
* deposits,
* markets,
* fundamentals,
* SEC/EDGAR information,
* data frames,
* persistence,
* scheduling,
* mail,
* web access,
* charting,
* grids,
* cryptography,
* and LLM interaction.

Business Automation Reasoning should generally **compose these capabilities rather than duplicate them**.

For example:

```text
stats.bas
finance.bas
market.bas
frame.bas
dbframe.bas
web.bas
schedule.bas
mail.bas
llm.bas
persist.bas
        │
        ▼
 ┌─────────────┐
 │ insight.bas │
 └──────┬──────┘
        ▼
 ┌──────────────┐
 │ decision.bas │
 └──────┬───────┘
        ▼
 ┌────────────────┐
 │ automation.bas │
 └────────────────┘
```

This architecture is provisional.

The cookbook exercises described later should be used to test whether these are the correct boundaries.

---

# 4. insight.bas

## Core Question

**What is happening, and why might it be happening?**

`insight.bas` transforms business data into structured findings.

It should operate above lower-level statistical, financial, database, and analytical libraries.

Possible responsibilities include:

* trend detection,
* anomaly detection,
* change-point detection,
* distribution changes,
* seasonality,
* segmentation,
* decomposition,
* cohort comparison,
* correlation,
* lagged relationships,
* forecast deviations,
* identification of contributing variables,
* hypothesis generation,
* hypothesis ranking,
* suggested tests,
* evidence gathering,
* significance evaluation,
* business-materiality evaluation,
* and summarization.

Example conceptual operations:

```basic
trend = FIND_TRENDS(sales)

anomalies = FIND_ANOMALIES(transactions)

explanation = EXPLAIN_CHANGE(revenue)

hypotheses = GENERATE_HYPOTHESES(churn)

investigation = INVESTIGATE(margin)
```

These names are placeholders rather than final API decisions.

## Structured Findings

Results should generally be structured objects rather than simple scalar values.

A finding might contain:

```text
Finding
    subject
    observation
    magnitude
    direction
    time_range
    affected_segments
    statistical_significance
    business_materiality
    confidence
    evidence[]
    contrary_evidence[]
    contributors[]
    hypotheses[]
    recommended_tests[]
    provenance
```

An important distinction should exist between **statistical significance** and **business significance**.

A tiny effect may be statistically undeniable but economically irrelevant.

Conversely, an uncertain event with catastrophic potential may demand attention.

## Causality

`insight.bas` must not casually convert correlation into causation.

The desired progression is closer to:

```text
OBSERVATION
     ↓
ASSOCIATION
     ↓
CAUSAL HYPOTHESIS
     ↓
TEST / EVIDENCE
     ↓
SUPPORTED OR REJECTED EXPLANATION
```

The library may suggest possible causes and experiments without claiming certainty that the available evidence does not justify.

---

# 5. decision.bas

## Core Question

**Given what we currently know, what should we do?**

`decision.bas` consumes findings and combines them with:

* objectives,
* alternatives,
* constraints,
* costs,
* benefits,
* uncertainty,
* risk,
* business policies,
* and authority rules.

Possible responsibilities include:

* alternative generation,
* alternative scoring,
* expected-value calculations,
* cost-benefit analysis,
* scenario analysis,
* sensitivity analysis,
* risk analysis,
* optimization,
* prioritization,
* resource allocation,
* decision tables,
* multi-objective decisions,
* constraint handling,
* and policy evaluation.

Conceptually:

```basic
goal = MAXIMIZE profit
    SUBJECT TO retention >= .90
    AND service_level >= .97

options = DECIDE(goal, possible_actions)
```

A decision should not merely contain a winning alternative.

It might contain:

```text
Decision
    objective
    findings[]
    alternatives[]
    recommendation
    expected_value
    expected_cost
    downside
    upside
    risk
    uncertainty
    confidence
    assumptions[]
    sensitivities[]
    rationale
    authority_required
```

This permits the system to say something much more useful than:

```text
Option A is best.
```

It could instead conclude:

```text
Option A has the greatest expected return.

Option B produces approximately 8% less expected return
but reduces downside exposure by approximately 40%.

The recommendation is highly sensitive to the assumed
customer churn response.

Human review is recommended.
```

That distinction is important for trustworthy business automation.

---

# 6. automation.bas

## Core Question

**When and how should something actually happen?**

`automation.bas` is responsible for orchestration and execution.

Possible responsibilities include:

* watching data,
* event detection,
* triggers,
* schedules,
* workflow execution,
* dependencies,
* queues,
* retries,
* timeouts,
* state machines,
* approval requests,
* escalation,
* notifications,
* idempotency,
* action execution,
* audit logging,
* simulation,
* and failure recovery.

Conceptually:

```basic
WATCH daily_sales
EVERY 1 HOUR

WHEN ANOMALY(daily_sales)

    finding = INVESTIGATE(daily_sales)

    decision = EVALUATE(finding)

    IF decision.auto_approved THEN
        EXECUTE decision.action
    ELSE
        REQUEST_APPROVAL decision
    END IF

END WHEN
```

Again, syntax is provisional.

## Human Review

Human participation should be a first-class concept rather than an exception bolted onto automation later.

For example:

```basic
APPROVAL_REQUIRED IF
    action.value > 10000 OR
    confidence < .90
```

A process may therefore operate automatically within a delegated envelope while escalating other cases.

---

# 7. Separation of Responsibilities

The boundaries between the three libraries are important.

A useful general rule is:

```text
insight.bas
    OBSERVES AND REASONS

decision.bas
    EVALUATES AND CHOOSES

automation.bas
    ORCHESTRATES AND ACTS
```

Or, anthropomorphically:

```text
Insight     = analyst
Decision    = manager
Automation  = operator
```

`insight.bas` should normally not alter the business.

`decision.bas` should normally not execute its recommendations.

`automation.bas` is the layer authorized to change external state.

This separation also makes testing and simulation substantially easier.

---

# 8. Shared Business Reasoning Model

The libraries will probably require common objects.

Initial candidates include:

```text
METRIC
SIGNAL
FINDING
HYPOTHESIS
EVIDENCE
FORECAST
RISK
GOAL
CONSTRAINT
ALTERNATIVE
DECISION
POLICY
ACTION
OUTCOME
```

Their exact representations should emerge from experimentation.

One possible relationship is:

```text
Metric
   ↓
Signal
   ↓
Finding
   ↓
Hypothesis
   ↓
Evidence
   ↓
Decision
   ↓
Action
   ↓
Outcome
```

An outcome can subsequently become evidence for future reasoning.

This creates an important learning cycle:

```text
OBSERVE
   ↓
HYPOTHESIZE
   ↓
DECIDE
   ↓
ACT
   ↓
MEASURE OUTCOME
   ↓
LEARN
   ↓
OBSERVE ...
```

Learning here does not necessarily mean machine learning.

Simply preserving structured organizational experience could be extremely valuable.

---

# 9. Provenance and Auditability

Automated reasoning must remain inspectable.

A finding should be able to answer questions such as:

* Which data produced this conclusion?
* What period was analyzed?
* Which statistical method was used?
* Which parameters were supplied?
* Which assumptions were made?
* Which hypotheses were considered?
* Which evidence supported the conclusion?

Likewise, an automated action should record:

* the finding that initiated it,
* the decision that recommended it,
* the policy permitting it,
* the authority under which it executed,
* the parameters used,
* the external result,
* and subsequent observed outcome.

This produces a reasoning chain:

```text
DATA
  ↓
FINDING
  ↓
EVIDENCE
  ↓
DECISION
  ↓
POLICY
  ↓
ACTION
  ↓
OUTCOME
```

The chain should be inspectable after the fact.

---

# 10. Materiality, Confidence, Risk, and Authority

These should probably become recurring concepts throughout the system.

## Materiality

Does this matter enough to care about?

## Confidence

How strongly does available evidence support the conclusion?

## Risk

What happens if the conclusion or decision is wrong?

## Authority

Is this process permitted to execute the proposed action?

These concepts allow automation to behave differently according to circumstances.

For example:

```basic
IF finding.materiality > HIGH THEN

    decision = DECIDE(finding)

    IF decision.risk < LOW AND
       decision.confidence > .95 AND
       decision.value < auto_limit THEN

        EXECUTE decision.action

    ELSE

        REQUEST_APPROVAL decision

    END IF
END IF
```

The exact API is intentionally unresolved.

---

# 11. Automatic Decomposition

A particularly promising capability is automatic investigation across dimensions.

Humans currently perform this manually in dashboards.

For example:

```text
Revenue declined.
       ↓
Which region?
       ↓
Which stores?
       ↓
Which category?
       ↓
Which products?
       ↓
Which customer segment?
```

A Business Automation Reasoning system should potentially perform that investigation automatically.

Conceptually:

```basic
finding = EXPLAIN_CHANGE(revenue, PERIOD="30d")
```

A result might resemble:

```text
Revenue declined 8.1%.

67% of the decline is attributable to:

    Northeast
        Syracuse stores
            Outdoor category
                Product family X

Associated changes:

    inventory availability   -18%
    advertising impressions  -23%
    competitor price index    -6%

Possible explanations:

    inventory availability   confidence .91
    reduced advertising      confidence .73
    competitor pricing       confidence .58
```

This illustrates the broader goal: replace dashboard spelunking with explicit analytical reasoning.

---

# 12. Goals and Optimization

Some processes should reason from explicit business goals.

Conceptually:

```basic
goal = MAXIMIZE gross_margin
    SUBJECT TO customer_retention >= .92
    AND inventory_days < 45
```

The system might evaluate alternatives:

```basic
options = OPTIMIZE(
    goal,
    pricing,
    advertising,
    inventory
)
```

This introduces operations-research concepts into the broader reasoning architecture.

The conceptual distinction is:

```text
STATISTICS
    What appears to be happening?

CAUSAL INFERENCE
    Why might it be happening?

FORECASTING
    What is likely to happen next?

OPTIMIZATION
    What should we do?

AUTOMATION
    Should we actually do it, and how?
```

---

# 13. External Context

Business reasoning should not be restricted to internal databases.

Potential sources include:

* weather,
* commodity prices,
* economic indicators,
* competitor pricing,
* advertising platforms,
* search trends,
* web traffic,
* market data,
* demographic data,
* regulatory information,
* supplier information,
* logistics information,
* and other relevant external sources.

A process might conceptually observe:

```basic
WATCH sales, inventory, web_traffic,
      weather, unemployment,
      competitor_prices, commodity_prices
```

The insight layer could then discover relationships that cross organizational boundaries.

---

# 14. Role of LLMs

LLMs may be valuable but should not become the mathematical foundation of the system.

Whenever practical:

```text
stats.bas
finance.bas
optimization algorithms
forecasting algorithms
rules
structured data
```

should produce the actual quantitative evidence.

An LLM may be useful for:

* communicating findings conversationally,
* interpreting unstructured information,
* generating candidate hypotheses,
* summarizing evidence,
* interacting with humans,
* translating natural-language objectives into candidate structures,
* and explaining results.

For example:

```text
Something unusual happened in Northeast sales yesterday.

I traced approximately 74% of the decline to two
distributors.

Their order volumes dropped shortly after a price
increase.

This does not establish that the price increase caused
the decline.

I have identified three analyses that could test that
hypothesis.
```

The underlying numbers should remain independently inspectable.

---

# 15. Why Start With a Cookbook?

The API should **not be finalized yet**.

The project should first construct a collection of realistic business scenarios.

Each scenario should be implemented conceptually using provisional APIs.

This allows actual use cases to reveal:

* missing abstractions,
* awkward abstractions,
* incorrect library boundaries,
* recurring patterns,
* useful common objects,
* necessary metadata,
* and opportunities for simpler syntax.

In other words:

```text
SCENARIOS
    ↓
PROVISIONAL DESIGN
    ↓
COOKBOOK EXPERIMENTS
    ↓
DISCOVER PATTERNS
    ↓
REVISE DESIGN
    ↓
IMPLEMENT
    ↓
TEST AGAINST COOKBOOK
    ↓
FINALIZE API
    ↓
FINAL COOKBOOK + REFERENCE
```

The cookbook is therefore initially a **design laboratory**.

Later it becomes user documentation.

---

# 16. Cookbook Recipe Structure

Each recipe should contain approximately the following sections.

## Business Problem

Describe the real-world situation without reference to implementation.

## Business Objective

What outcome is the organization attempting to achieve?

## Available Data

Identify internal and external information available to the process.

## Insight Process

What should the system observe, detect, investigate, compare, or explain?

## Decision Process

What alternatives, objectives, constraints, costs, risks, and uncertainties matter?

## Automation Policy

What can happen automatically?

What requires approval?

What should merely generate information?

## Human Role

Where does human judgment remain useful or necessary?

## Provisional gBASIC Implementation

Write realistic candidate gBASIC code.

Do not distort the architecture merely to preserve an earlier syntax proposal.

## Expected Findings and Behavior

Describe what a successful execution would produce.

## Failure and Edge Cases

Consider:

* missing data,
* bad data,
* contradictory evidence,
* insufficient evidence,
* false positives,
* extreme events,
* unavailable external systems,
* failed actions,
* and changing business conditions.

## Design Lessons

Most importantly, record what this recipe teaches us about the library design.

Questions include:

* What primitive was missing?
* What operation was awkward?
* What concept appeared repeatedly?
* Which layer should own it?
* Should something become a first-class object?
* Was too much boilerplate required?
* Was important information lost between layers?

---

# 17. Initial Cookbook

The first experimental cookbook should include at least the following scenarios.

### Recipe 1: Sales Decline Investigation

Detect a meaningful sales decline and automatically determine where the decline is concentrated and what factors may explain it.

### Recipe 2: Inventory Shortage Response

Detect inventory conditions likely to cause lost sales, estimate consequences, compare replenishment alternatives, and act within purchasing authority.

### Recipe 3: Advertising Allocation

Measure campaign performance, consider delayed effects, estimate marginal return, and recommend or perform budget reallocation.

### Recipe 4: Customer Churn Investigation

Detect increasing churn, identify affected customer populations, investigate associated changes, and evaluate retention interventions.

### Recipe 5: Unusual Expense Investigation

Detect abnormal expenditures and determine whether they represent legitimate changes, errors, waste, or potentially suspicious behavior.

### Recipe 6: Cash-Flow Protection

Forecast liquidity, detect likely future shortfalls, evaluate alternatives, and recommend preventive action.

### Recipe 7: Staffing Optimization

Forecast workload and determine staffing requirements while respecting cost, availability, labor constraints, and service objectives.

### Recipe 8: Supplier Performance

Monitor price, timeliness, quality, and downstream business consequences and determine when purchasing behavior should change.

### Recipe 9: Pricing Review

Detect margin or demand changes, estimate effects of alternative prices, and recommend changes subject to risk and authority constraints.

### Recipe 10: External Market Shock

Combine internal and external data to recognize when external conditions materially alter expected business behavior.

### Recipe 11: Operational Bottleneck

Detect increasing processing time or declining throughput, identify where delays accumulate, estimate consequences, and evaluate corrective actions.

### Recipe 12: Management by Exception

Continuously review a broad set of business measures and bring humans into the process only when something is sufficiently material, uncertain, unusual, risky, or outside delegated authority.

---

# 18. Advanced Cookbook Scenarios

After the initial recipes, deliberately stress-test the architecture.

Possible advanced recipes include:

### Dynamic Purchasing

Continuously balance demand forecasts, supplier prices, lead times, inventory carrying cost, cash position, and stockout risk.

### Automated Promotion Management

Detect excess inventory or weak demand and design bounded promotional responses.

### Customer Service Intervention

Detect emerging service problems from quantitative and textual information and determine appropriate intervention.

### Fraud / Abuse Response

Detect suspicious behavior while explicitly considering false-positive cost and escalation requirements.

### Multi-Department Resource Allocation

Allocate limited resources among competing organizational objectives.

### Business Experiment

Identify uncertainty important enough to justify an experiment, design the experiment, measure its result, and update future decisions.

### Strategic Early Warning

Combine weak signals across internal and external information to identify emerging threats or opportunities.

### Nearly Autonomous Small Business

Model a hypothetical small organization in which the system handles as much routine management as reasonably possible while escalating appropriate decisions to humans.

This scenario is particularly useful as an architectural stress test.

It may reveal the need for concepts such as:

* persistent goals,
* budgets,
* commitments,
* contracts,
* competing objectives,
* organizational roles,
* delegated authority,
* cross-process coordination,
* exception queues,
* and long-term memory.

---

# 19. Simulation and Dry-Run Mode

Automation should eventually support simulation.

For example:

```basic
SIMULATE business_process
    FROM "2025-01-01"
    TO "2025-12-31"
```

Historical business data could be replayed through a process.

Instead of actually performing actions, the system records what it **would have done**.

This would allow questions such as:

```text
How often would this system have acted?

How many actions would have required human approval?

How many recommendations would subsequently appear correct?

What would the estimated financial impact have been?

Where would false alarms have occurred?
```

Simulation may become one of the most important tools for safely developing automated business reasoning.

---

# 20. Outcome Tracking

Actions should not disappear into history after execution.

The system should be capable of asking:

**Did the action work?**

For example:

```text
Finding:
    Inventory shortages are reducing sales.

Decision:
    Expedite shipment.

Action:
    2,000 units expedited.

Expected outcome:
    Availability > 97%
    Lost-sales rate < 2%

Observed outcome:
    Availability = 98.1%
    Lost-sales rate = 1.4%

Conclusion:
    Intervention appears successful.
```

This closes the reasoning loop and creates organizational memory.

---

# 21. Cost of Being Wrong

Confidence alone is insufficient.

The system should eventually consider the asymmetric consequences of mistakes.

For example:

```text
False positive:
    We unnecessarily investigate a sales anomaly.
    Cost: $50.

False negative:
    We fail to detect a major fraud pattern.
    Cost: potentially $500,000.
```

Those situations should not use identical decision thresholds.

This suggests eventual primitives involving:

```text
COST_OF_ACTION
COST_OF_INACTION
COST_OF_FALSE_POSITIVE
COST_OF_FALSE_NEGATIVE
REVERSIBILITY
IMPACT
```

The cookbook should test whether these belong in `decision.bas`, a shared reasoning model, or elsewhere.

---

# 22. Development Method

Do not begin by implementing hundreds of functions.

The recommended sequence is:

## Phase 1: Exploration

Develop the cookbook scenarios using pseudocode and provisional APIs.

Goal: discover the conceptual model.

## Phase 2: Domain Model

From the cookbook, identify recurring objects and operations.

Define provisional representations for:

```text
Metric
Signal
Finding
Hypothesis
Evidence
Goal
Risk
Decision
Policy
Action
Outcome
```

## Phase 3: API Design

Design `insight.bas`, `decision.bas`, and `automation.bas` around demonstrated needs.

Prefer small composable primitives over enormous magical functions.

## Phase 4: Prototype

Implement enough functionality to execute several cookbook scenarios using real or generated datasets.

## Phase 5: Revision

Use implementation experience to revise:

* object representations,
* naming,
* library boundaries,
* error handling,
* syntax,
* and abstractions.

Breaking provisional APIs at this stage is acceptable.

## Phase 6: Expansion

Implement the broader set of cookbook scenarios.

Add capabilities only when justified by actual scenarios or clearly reusable foundations.

## Phase 7: Stabilization

Once the architecture has survived sufficiently diverse scenarios, stabilize the public API.

## Phase 8: Documentation

Turn the experimental cookbook into polished user documentation.

Create a formal API reference.

---

# 23. Design Principles

The project should follow several principles.

### Evidence Before Magic

Quantitative conclusions should be grounded in inspectable algorithms and data whenever possible.

### Uncertainty Is Information

Do not hide uncertainty merely to produce clean answers.

### Humans Are Part of the Architecture

Human review, discussion, approval, and override should be designed in from the beginning.

### Automation Must Have Authority Boundaries

Being technically capable of an action does not imply being authorized to perform it.

### Actions Must Be Auditable

Important automated actions should be traceable to the evidence, decision, and policy that produced them.

### Outcomes Matter

A decision process is incomplete if the system never determines whether its action helped.

### Composition Over Monoliths

Prefer reusable analytical and decision primitives that programmers can combine.

### Business Language Matters

The eventual API should be understandable to programmers thinking about business problems rather than requiring them to think like statistical-library implementers.

### LLMs Assist Rather Than Obscure

LLMs may provide substantial value, but should not turn inspectable reasoning into an opaque text-generation pipeline.

### Design From Real Problems

When elegant abstractions conflict with demonstrated cookbook requirements, investigate the cookbook requirement before defending the abstraction.

---

# 24. An Important Architectural Question

During exploration, determine whether the three-library division is actually correct.

The current hypothesis is:

```text
insight.bas
    What is happening?
    Why might it be happening?

decision.bas
    What should be done?

automation.bas
    When and how should it happen?
```

This is a strong starting model, not a sacred constraint.

Cookbook development may reveal that responsibilities should move, split, merge, or require another library.

Likewise, a future convenience facade such as:

```text
business.bas
```

could potentially expose common functionality while the implementation remains separated internally.

Do not make this decision prematurely.

---

# 25. What Success Looks Like

A successful result should allow a programmer to express something conceptually like:

```basic
WATCH company.sales

WHEN MATERIAL_CHANGE

    finding = INVESTIGATE(company.sales)

    IF finding.requires_attention THEN

        decision = DECIDE(
            finding,
            OBJECTIVES = company.goals,
            POLICIES = company.policies
        )

        IF decision.authorized AND
           decision.risk <= acceptable_risk THEN

            result = EXECUTE(decision.action)

        ELSE

            result = REQUEST_APPROVAL(decision)

        END IF

        TRACK_OUTCOME result

    END IF

END WHEN
```

The syntax will almost certainly change.

The important achievement is the programming model:

> A programmer can encode how an organization observes its environment, investigates meaningful conditions, reasons about alternatives, makes bounded decisions, involves humans where appropriate, acts, and learns from the resulting outcomes.

---

# 26. Immediate Next Task

Do **not** begin large-scale implementation yet.

Begin with the cookbook.

Take the initial recipes one at a time.

For each recipe:

1. Define a realistic business scenario.
2. Identify required data.
3. Describe how a competent human analyst or manager would currently reason through it.
4. Determine which parts are objectively computable.
5. Determine which parts require uncertainty or judgment.
6. Express the process using provisional gBASIC APIs.
7. Identify the underlying statistical, financial, optimization, workflow, and other primitives required.
8. Identify which existing gBASIC libraries can supply those primitives.
9. Record missing capabilities.
10. Record awkward API patterns.
11. Record concepts that repeatedly appear.
12. Revise the provisional architecture as evidence accumulates.

Start with **Recipe 1: Sales Decline Investigation**.

Do not assume the current proposed API is correct.

The purpose of Recipe 1 is partly to solve the scenario and partly to discover what the Business Automation Reasoning API wants to become.

---

# 27. Working Philosophy

This project should be treated as exploratory language and library design rather than straightforward feature implementation.

The interesting question is not:

> How can gBASIC automate a sales report?

It is:

> What abstractions would let a programmer encode the reasoning process by which a business notices that something matters, discovers what is happening, determines what to do about it, safely acts, and learns whether that action worked?

The cookbook is where those abstractions should be discovered.

The libraries are what we build from what the cookbook teaches us.

The finalized cookbook then demonstrates what those libraries make possible.

---

# 28. Findings from Recipe 1 (added 2026-09-01)

§26 said to start with Recipe 1 and not to assume the proposed API is correct.
Recipe 1 is done, and it is **executable** —
`examples/automation_lab/01_sales_decline.bas`, asserted by
`tests/run_automation_lab.sh`, written up in
[automation_recipe_01_sales_decline.md](automation_recipe_01_sales_decline.md).
The findings below are measurements, not opinions, and the first one
contradicts §11.

## 28.1 §11's automatic decomposition, as specified, cannot be built

The experiment ran the same drill-down over two populations: one with a real
45% collapse planted in a known cell, one with **nothing planted at all**.

| | real cause | pure noise |
|---|---|---|
| headline | −37,495 (−1.8%) | −36,562 (−1.8%) |
| top region share | 82.6% | 80.3% |
| top store share | 57.3% | 51.2% |
| conclusion | Northeast → Northeast-2 → Outdoor | Southeast → Southeast-2 → Electronics |

Same headline to the tenth of a percent, same confident three-level chain,
same shape of evidence. **The output cannot distinguish a real cause from
nothing**, because a drill-down is a *search*, and a search always returns a
winner. §11's illustrative output is exactly what this produces — from noise as
readily as from a cause.

This does not retire §11. It makes the null model the load-bearing part of it,
rather than a refinement to add later.

## 28.2 The reference distribution is the sibling cells

The fix needs no new capability. The other 59 leaf cells are a sample of
ordinary movement, and against that null the two runs separate cleanly
(z = −3.45 against z = −2.38).

Generalise the pattern: *what does ordinary look like here?* is usually
answerable from the same data that raised the question.

## 28.3 Search width is part of the result

The cut that separates those two is not a number a library may choose. The
largest of *n* draws lands near `sqrt(2 ln n)` when nothing is happening —
2.33 at 15 cells, 2.86 at 60, 3.26 at 200. A `z` that is remarkable across four
regions is unremarkable across two hundred product families.

So **a Finding must carry how many cells were searched to produce it.** Without
it the Finding cannot be judged, and two Findings from searches of different
width cannot be compared.

## 28.4 Contribution shares do not partition

In the planted run the region shares were −22.6%, −3.7%, 43.7% and 82.6%. Among
the regions that actually *declined* they sum to **126%**.

Correct arithmetic, disastrous phrasing: "Northeast is 82.6% of the decline"
invites *most of it was Northeast* when Midwest was independently 43.7% of it.
As offsetting movements grow the shares inflate without bound while the net
change goes to zero.

Report gross alongside net, and refuse the share when they diverge.

## 28.5 "Confidence" is three quantities sharing one word

The document currently uses `confidence` for:

1. how well a quantity is **estimated** (§10),
2. how well a hypothesis **accounts for the evidence** (§11's `confidence .91`),
3. how sure we are that a recommended **action is right** (§25's `> .95`).

These do not share a scale and must not share a threshold. Provisional names:
`confidence` for the first (keep it where it is well understood), `support` for
the second, `assurance` for the third. The names matter less than the split.

## 28.6 Materiality cannot live on the Finding

§10 makes materiality a property of a Finding. But the insight layer can say a
cell is *statistically unusual* with no business context at all, and cannot say
it *matters* without an objective — and objectives are `decision.bas`'s input.
As written, §10 forces `insight.bas` to know the organisation's goals, which
breaks the §7 separation the architecture rests on.

Two ways out, and §24 should choose deliberately:

- move materiality to the decision layer, leaving Findings purely descriptive; or
- give the insight layer an explicit **policy** argument, so the dependence is
  visible in the call rather than hidden in the library.

The second suggests the architecture may want a fourth thing that is not a
layer — a shared, declared **context** (objectives, thresholds, authority)
that both insight and decision read. That is a better answer to §24's question
than moving responsibilities between the three.

## 28.7 The boilerplate is the API's job

Of the experiment's ~200 lines, roughly 60 are the decomposition and the rest
is grouping, distinct-value extraction and filtered totals — all of which
`frame.summarize` already does. `insight.explain_change` should take a frame, a
measure column, a period column, and an ordered list of dimensions.

---

# 29. Refusals (added 2026-09-01)

This document had principles and no refusals. Every shipped library in this
tree turns on a small set of things it will **not** do, each preventing a
specific plausible-looking wrong answer rather than a crash — `money` refuses
to add two currencies, `credit` refuses to infer a delinquency convention,
`scoring` refuses to smooth an empty bin.

This domain needs them more than any of those, because it is the one where a
plausible wrong answer is *acted upon automatically, at scale, without a human
reading it*. The following are proposed as binding.

**R1 — a decomposition that cannot state its search width is refused.**
Not defaulted, not estimated. Established by 28.1 and 28.3: without it the
significance cut cannot be set and the result is a confident chain built from
noise.

**R2 — a contribution share is refused when the net change is small relative to
the gross movement behind it.** The honest output is the signed contributions
and a statement that no share is reportable (28.4). A percentage that happens
to be computable is not a percentage that means anything.

**R3 — an association is not an explanation, and the type system should say so.**
§4 states this as an aspiration with no mechanism. The mechanism: a Finding
carries associations; a Hypothesis is a separate object; a Hypothesis becomes an
Explanation only by passing a **declared test** whose result is recorded. There
is no path from correlation to `finding.cause` that does not go through a
recorded test.

**R4 — confidences of different kinds may not be compared or thresholded
together** (28.5). Raise rather than coerce.

**R5 — an action whose process has never been replayed against history is
refused.** §19 treats simulation as a capability to add eventually. It should
be a **precondition**: a process that has never been dry-run has no evidence
about how often it would fire, how often it would have been wrong, or what it
would have cost. Promoting this from feature to gate is the cheapest safety
property in the document.

**R6 — authority is enforced at the action, never at the decision.**
A decision may freely recommend what it is not authorised to execute; that is
useful information and suppressing it hides the cases a human most needs to
see. The refusal belongs at execution, where the authority is actually spent.

**R7 — an outcome that was never measured is not evidence.**
§20 closes the loop and §8 lets an outcome become evidence for future
reasoning. A prior action may be cited as evidence only if its outcome was
recorded. Otherwise "we did this before and it worked" enters the system as a
fact when it is a memory.

**R8 — the null must be declared, not inferred.**
Following `credit`'s delinquency method and `scoring`'s WOE orientation: what
counts as *ordinary* is a modelling choice that changes every answer downstream.
Sibling cells (28.2) are a good default for a stationary measure and a wrong one
under seasonality — which Recipe 1 assumed away and a later recipe must not.

---

# 30. What the next recipes should attack

Recipe 1 used one measure, one period comparison, three dimensions and a single
planted cause. It says nothing about the cases most likely to break the model,
and the next recipes should be chosen to break it rather than to broaden it:

- **two causes at once**, where the drill-down's greedy first step is wrong;
- **a seasonal measure**, where "ordinary movement" is not stationary and R8 has
  to be exercised rather than assumed;
- **a high-cardinality dimension** (hundreds of products), where search width
  dominates and almost nothing should survive;
- **a cause that moves between periods**, which no single before/after
  comparison can see;
- and the first recipe that **decides** something, since Recipes 1–N so far all
  stop at the finding and the `decision.bas` boundary is entirely untested.

§17's list is a good breadth-first plan. This is the depth-first one, and the
architecture is likelier to be corrected by depth.

