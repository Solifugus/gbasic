# NLQ — a question, over an estate nobody can hold in their head

**Status:** Design. Nothing is built.
**Consumes:** `discovery` (the catalog and column lineage), `retrieval`,
`llm`, `tools`, `odbc`. **Scored by:** `estateforge`'s `questions(plan)`.

---

## 1. What it is, and the one thing that can go wrong

A person asks *"what was total volume last quarter?"* and gets a number.

Between the two sits a 500-table estate that does not fit in a context window,
forty columns named `amount`, two reports that both have a `total_volume` and
are both correct, and a `customer2` table that is the live one while nothing in
the schema says so.

**NLQ's defect is never a crash.** It is a syntactically valid query that
answers a *different question* and returns an ordinary-looking number. Nobody
downstream can tell: the number has the right units, the right order of
magnitude, and a confident sentence attached. That is the same shape as the
decomposition in `automation_reasoning_design` §11 that produced an identical
three-level causal chain from a real collapse and from pure noise, as the chart
that drew a complete empty picture from a full frame, and as the ODBC read that
narrowed a `DECIMAL(19,4)` into a double.

The answer is the same one this tree keeps reaching: **make what can go wrong
sayable, and refuse where a guess is indistinguishable from knowledge.**

## 2. Why schema retrieval comes first, and is not a model problem

`discovery_design` §2 already states it: *schema retrieval must precede
prompting, since 500 tables do not fit in a context window.*

That makes retrieval a **search**, and Recipe 1's finding applies without
change: a search always returns a winner. Hand a model twenty tables chosen
badly and it will write flawless SQL about the wrong ones — and the SQL will
run, because the tables it was given are real.

So retrieval is where NLQ is won or lost, and it is the part that **needs no
model to evaluate**. `estateforge`'s `questions(plan)` records, for every
question, the `touches[]` it actually needs. Retrieval can therefore be scored
directly:

> for each question, does the selected subset contain every table the question
> touches — and how much else did it bring?

Both halves are needed. Recall alone is maximised by selecting everything,
which is precisely the failure retrieval exists to prevent; precision alone is
maximised by selecting nothing. A benchmark reporting one of them is
measuring the wrong thing.

## 3. The first increment: retrieval and grounding, no model at all

Deliberately mirroring `discovery`'s own choice of "declared facts only":

- it has a **correctness criterion that needs no null model and no LLM** —
  `touches[]` is the answer key, computed by estateforge from the plan;
- it is what everything above it depends on, so a defect here is invisible and
  fatal later;
- it is **deterministic**, so it can be a gate rather than a sampled score.

What it contains: given a catalog from `discovery` and a question in text,
return a **grounding** — the tables and columns the question is about, each
carrying why it was selected, plus what was *considered and rejected near the
boundary*. Nothing is sent to a model and no SQL is written.

Reporting the near-misses is not diagnostics. It is the difference between
"the answer is `trading.deal`" and "the answer is `trading.deal`, and
`trading.deal_archive` and `warehouse.fact_deal` were the next two" — which is
the only form in which a reader can see that a search had a close call.

## 4. What NLQ must refuse

Each of these is a place where answering is worse than declining, because the
wrong answer is indistinguishable from the right one.

- **R1 — an ambiguous measure is not resolved silently.** A question naming
  "amount" against forty columns called `amount` is refused with the candidates
  named. Picking the first, the most common, or the highest-scoring is a guess
  wearing the clothes of a lookup.
- **R2 — two lineages are two answers.** Where a measure resolves to columns
  with *different derivations* (the two `total_volume` reports), NLQ reports
  both and the derivation that separates them. `discovery.lineage` can already
  answer this; the failure is not using it.
- **R3 — SQL may not name what retrieval did not surface.** A generated query
  touching a table the grounding never selected is a hallucination that happens
  to be spelled correctly. Refused, not executed.
- **R4 — read-only, structurally.** Not by asking the model nicely.
- **R5 — an answer never travels without its query.** The SQL, the tables
  touched, and the assumptions made are part of the result, not logging. This is
  Axiom 2 of `financial_adapters_design` one domain over, and for the same
  reason: "where did this number come from" is the question a business asks
  second, immediately.
- **R6 — unanswerable is an outcome.** `discovery_design` §4: some facts are not
  in the database at all. *Which* `customer` table is live is one of them. NLQ
  reports the question as unanswerable from the catalog rather than choosing,
  and names what would settle it.

### R1 and R6 as built — and the rate that nearly sank them

`ground` **reports**; `check_answerable` **refuses**. The split matters: a
caller may legitimately want the grounding in order to show a person the
candidates and ask. What must not happen is a *number* produced from it.

The first implementation fired on **15 of 16** questions. A refusal that fires
on everything is indistinguishable from having no tool — the failure the
blind-shadow warning had at 287 false positives before it was reverted, and the
one `insight`'s first threshold had when it cleared half of all pure-noise
populations. Two causes:

- **Same name, different schema is normal.** `trading.deal`,
  `trading_apac.deal` and `trading_emea.deal` are regional partitions and the
  *schema names say so* — `apac` and `emea` are words a question can use. The
  discriminator is whether the catalog offers a distinction at all: ambiguous
  only when stripping digits makes the schema names identical, which is exactly
  `staging` / `staging_2` / `staging_3`, estateforge's planted null region.
- **A tie at the cut does not block an answer.** True of 13 of 16 questions, and
  the needed table was inside the cut anyway. It is disclosure now
  (`search.cut_tied`), not a gate.

Measured after: **0 of 16** where nothing is planted for it, **1 of 7** where it
is. Both halves are gated, since either alone is satisfied by a check that
always answers or always refuses.

**R2–R5 arrive with SQL generation**, because each is about a query: two
lineages are two answers, SQL may not name what retrieval never surfaced,
read-only is structural, and an answer never travels without its query.

## 4a. Measured: the first real model call got the literal wrong

Before writing any of the generation layer, one question was put to
`qwen3:4b` through Ollama with a hand-built grounding:

> Tables: `retail_banking.account(account_id, account_status, current_balance)`
> Question: How many accounts are open?

```sql
SELECT COUNT(*) FROM retail_banking.account WHERE account_status = 'open';
```

The SQL is perfect. **The data says `'OPEN'`.** The query returns 0, with no
error, and 0 is a number a dashboard will happily render. Reproduced across
calls at temperature 0.

This is the failure §1 describes, arriving on the first attempt and from the
direction least expected: not the wrong table, not the wrong join — **the wrong
literal**. And it is invisible to every check that looks at the query: the SQL
names the right table, the right column and the right operator, so SQL-text
scoring rates it correct. Only running it against data with a known answer
reveals anything, which is why §5 scores the value.

**The cause is a gap in the grounding, not in the model.** A catalog gives
column *names* and not column *values*, so `'open'` versus `'OPEN'` versus
`'A'` versus `'ACTIVE'` is a guess the model has no way to avoid. Asking it to
guess better is the wrong repair.

**The remedy is a declared fact.** For a low-cardinality column the distinct
values are *in the database* and can be read — declared, certain and cheap,
which is exactly the class `discovery`'s first increment is built from. A
grounding that carries `account_status ∈ {OPEN, CLOSED, FROZEN}` removes the
guess rather than improving it.

Two consequences for the increment below:

- grounding gains **value vocabulary** for low-cardinality columns, which is a
  `discovery` read and not an inference;
- and where a question's literal matches **no** known value of the column it
  was matched to, that is reportable — the same shape as `unresolved`, one
  level down.

### And the remedy was measured, not assumed

The same model, the same question, temperature 0, one variable — whether the
prompt carries the column's declared values:

```
WITHOUT: SELECT COUNT(*) FROM retail_banking.account WHERE account_status = 'open';
WITH:    SELECT COUNT(*) FROM retail_banking.account WHERE account_status = 'OPEN';
```

Declaring `account_status in (OPEN, CLOSED, FROZEN)` is the whole difference
between a query that silently returns 0 and one that answers. That is the claim
this design deliberately declined to make until it had been run: a feature whose
justification is "the model will probably use it" is decoration until the model
is observed using it.

Note what the fix is **not**. Nothing was added to the instructions, no
temperature was tuned and the model was not asked to try harder. A fact the
database already knew was put in front of it.

---

## 4b. Measured: the context budget is 4096 tokens, and overflow is silent

`qwen3:4b` declares a context length of **262144**. Ollama serves it with a
default `num_ctx` of **4096**, and the declared figure is not what is used.

Probed by putting a distinctive table name at the *head* of a growing prompt and
asking for it at the *tail*:

| prompt chars | `prompt_eval_count` | head survived |
|---|---|---|
| 572 | 228 | yes |
| 9 412 | **4095** | no |
| 28 212 | **4095** | no |
| 71 012 | **4095** | no |

**The count pins at 4095 and nothing is reported.** A 71 KB prompt is silently
cut to about 10 KB of text. Worse for this task, it is the **head** that is
discarded: the question at the tail survives while the schema above it does
not, so a model handed a large catalog answers with **no schema at all** — and
a model asked for SQL with no tables in front of it will confidently invent
table names that look exactly like the real ones.

Three consequences:

- **This is the strongest argument yet for §2.** Retrieval is not an
  optimisation for a small model, it is the difference between a grounded answer
  and a confabulated one. The budget is roughly 4096 tokens *total* — question,
  schema, value vocabulary, instructions and the model's own reply — which at
  measured density (~2.5 chars/token) is about 30–60 table descriptions, not
  121 and certainly not 500.
- **The grounding must carry a size, and the prompt builder must refuse to
  exceed it** rather than let the transport drop the schema. A refusal names a
  budget; a truncation names nothing.
- **`num_ctx` is a deployment decision, not a default to inherit.** Raising it
  costs memory and speed on hardware that is already the bottleneck; the point
  is that it must be *stated*, because the failure of leaving it unstated is
  invisible.

---

## 4c. Measured: what a 4B thinking model actually writes

Sixteen benchmark questions, grounded at `limit: 6` with value vocabulary,
recorded against `qwen3-nlq` — `qwen3:4b` with `num_ctx` **stated** at 8192,
because the stock server's 4096 is not enough for a model that reasons before
it answers.

**Thirteen of sixteen produced SQL. Three produced nothing at all**, with
`finish_reason: length`: the reasoning consumed the whole output budget. That
is an operational property of this model on this task, not a bug to tune away —
roughly one question in five is unanswerable at this size, and **an empty
answer is an outcome**, which is why the recorder refuses to save one. A
fixture built from silence is indistinguishable from a working one until the
gate is asserting against it.

Three patterns in what it did write, all of which SQL-text scoring rates as
correct:

**It prefers the warehouse copy to the source table.** `How many deals have a
status of ACTIVE?` became `SELECT COUNT(*) FROM warehouse.stg_deal`, where the
answer key says `trading.deal`. Not a hallucination — `stg_deal` *was* in the
grounding and it *does* have `deal_status`. But estateforge's own truth says
`stg_deal` is loaded from `trading.deal` through a join to `contract`, so rows
are filtered on the way and the two counts need not agree. **This is R2 arriving
on question one**, and it is exactly the case a query inspection cannot see: the
SQL is well-formed and names real columns.

**It invents literals where the vocabulary does not reach.**
`WHERE contract_ref = 1` and `WHERE acct_no IN (1100, 4000)` — both columns are
high-cardinality, so neither qualified for value vocabulary, and the model
filled the gap with plausible numbers. **Value vocabulary is therefore not a
complete remedy.** It removes the guess where a column is categorical; a
question that turns on a specific identifier is a different problem, and one
the grounding should report rather than let the model solve by invention.

**It is right about reports.** `SUM(total_volume) FROM warehouse.rpt_volume_gross`
and its `_net` counterpart are what those questions ask for, and the gap
question produced a correct join between the two. The lineage-aware questions —
the ones this design worried about most — are the ones it handled best, because
the grounding gave it exactly the two objects and the question named which.

---

## 5. Scoring, and why not on SQL text

`estateforge_design` §6 settles this and NLQ adopts it unchanged: the output is
SQL but the **answer is a number**, and scoring SQL text is weak in both
directions — many different correct queries answer one question, and a query
textually close to the reference can be semantically wrong.

So a run is scored on the **value**, against an answer estateforge computed from
the plan and the rows directly rather than by running the reference SQL. And
`exercises` is reported per capability — single-table, join, aggregate, column
disambiguation, lineage-aware, pathology-aware — because a single percentage
cannot say what to fix.

**The refusals are scored too, and this is the half a benchmark usually
misses.** A system that answers everything scores well on questions it should
have declined. Each refusal above needs a question it must refuse *and* its
nearest legal neighbour it must answer, or "NLQ is careful" is satisfied by a
system that is merely useless.

## 5a. Measured: the score, and what a disagreement turned out to be

The recorded SQL run against a real estate — `demo_plan()` built into its own
PostgreSQL database, the **same deterministic plan** the fixture was emitted
from — and compared to the answer estateforge folds over the generated rows.

**10 scored, 7 agreed, 2 failed to execute at all.**

The two disagreements are both the failure §1 describes, with numbers:

| question | model | answer key |
|---|---|---|
| `t_total_volume` | 4 025 053.56 | **6 285 487.93** |
| `w_report_gap` | 555.44 | **1 637 346.99** |

`t_total_volume` is **R2 measured**. The model summed `gross_vol_mmbtu` from
`warehouse.fact_volume`; the question is about `trading.deal`. The warehouse
fact table is built by an ETL that joins to `trading.ctp` and keeps only active
counterparties, so it holds a *subset* — and the sum of a subset is a perfectly
good number about a real table, 36% short of the one asked for. Nothing about
the query says so.

**And a claim this design made and had to withdraw.** When the fixtures were
first recorded, `t_active_deals` came back as a count over `warehouse.stg_deal`
instead of `trading.deal`, and that was written up as R2 on the grounds that
the staging copy is filtered on load so the counts "need not agree". Measured,
**they agree exactly** — 315 either way. The staging load filters nothing that
changes an `ACTIVE` count. The R2 failure is real and it landed on a different
question; the prediction about that one was wrong, and reading the ETL is not
the same as running it.

**The two that failed to execute are the better outcome — on PostgreSQL.**
`contract_ref = 1` and `acct_no IN (1100, 4000)` — the invented literals from
§4c — hit `operator does not exist: character varying = integer` and returned
nothing at all. A type mismatch is a **loud** failure, and loud is what this
whole design is trying to convert silent failures into.

**That loudness is the database's, not ours, and it does not survive a change of
target.** Measured, the same predicate on the same shape of data:

```
SQLite      where contract_ref = 1   ->  0 rows, no error
PostgreSQL  where id = 'x'           ->  ERROR: invalid input syntax for integer
```

SQLite is dynamically typed and compares the two happily. So on SQLite the
invented literal is exactly the failure §1 exists to prevent — a well-formed
query returning a plausible number nobody can check — and the two questions that
announced themselves here would have returned `0` there instead.

`run_odbc` already carries the standing form of this lesson: *a suite that runs
only against SQLite cannot see a type error at all.* The inverse is now
recorded too — **a suite that runs only against PostgreSQL cannot see that
SQLite hides one.** Which database the value tier runs against is therefore part
of what it measures, not a deployment detail.

---

## 5b. What a consumer's architecture changes

`gdash` imports the tables and views a dashboard needs into **its own SQLite
database**, and may add further views over them. It is the first real consumer,
and it moves the weight of this design in three directions at once.

**Retrieval matters less.** A dashboard imports what it needs — tens of objects,
not five hundred — so the context budget that drove §2 largely evaporates. The
grounding still earns its place (it is what R3 checks against, and what keeps a
prompt honest), but "500 tables do not fit" is not this consumer's problem.

**Literal correctness matters more.** Per §5a, SQLite will not refuse a
type-mismatched predicate, so the one class of error that announced itself on
PostgreSQL is silent on the target. Value vocabulary stops being an improvement
and becomes the main defence — and it is *cheap* here, because the tables are
small and the consumer owns them, so distinct values are one query away.

**R2 matters more, not less.** A dashboard author creating views over imported
data is manufacturing the two-reports problem deliberately: several views over
one import, each a legitimate reshaping, several of them plausibly answering the
same question with different numbers. That is `t_total_volume` by construction
rather than by accident.

One thing gets easier: the catalog needs no discovery scan against a foreign
database. The consumer performed the import, so it knows exactly what is there
and when it changed.

---

## 6. Deliberately not in the first increment

- **Any call to a model.** It comes after retrieval can be trusted.
- **Writing SQL.** Same reason.
- **Joins inferred from value overlap.** That is `discovery`'s inference
  increment, which needs a null model; NLQ consumes it and does not duplicate it.
- **Conversation.** A follow-up question that refines the previous one is a real
  requirement and a different problem; `agent` holds the run.
- **Anything that writes.** R4.
