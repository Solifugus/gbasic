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
  both and the derivation that separates them.

  **Built, and it reports rather than refuses — which was a correction.** As a
  blocker it stopped **8 of 16** benchmark questions, the same failure R6 had at
  15 of 16 before its discriminator existed. The diagnosis settles the design:
  for `t_total_volume` the two derivations differ by **36%** and the distinction
  *is* the answer; for `t_active_deals` they agree exactly (315 either way) and
  it is noise. **NLQ cannot tell those apart without running both** — which is
  expensive, and is the application's decision rather than this library's. So
  the fact travels and the choice does not, the same split as the latency and
  the cache.

  **Derivation is declared, not inferred.** An application that performed an
  import knows what it imported and from where; `discovery.lineage` can produce
  it for anyone holding module bodies. Taking it as input keeps a question
  grounding from dragging an ODBC catalog reader in behind it, and puts the fact
  in the hands of whoever actually knows it.

  **The discriminator is derivation, not shared columns.** `trading.deal` and
  `trading_emea.deal` share *every* column and are peers; `trading.deal` and
  `warehouse.stg_deal` share every column and are a chain. A check firing on
  column overlap would flag every regional partition in the estate.
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

**It gets literals wrong where the vocabulary does not reach** — and this was
written up once already, wrongly, before either query had been run. Both
`WHERE acct_no IN (1100, 4000)` and `WHERE contract_ref = 1` were called
inventions. Executed, they are two different things:

- `acct_no IN (1100, 4000)` **is correct.** `1100` and `4000` are the *only two*
  account numbers in the ledger, they were in the supplied vocabulary, and the
  model used them. The one flaw is that it wrote them unquoted against a text
  column — which PostgreSQL refuses and SQLite accepts, so a **semantically
  right query** was rejected on type strictness by one engine and answered
  correctly by the other.
- `contract_ref = 1` **is wrong.** The question asks about "contract 1"; the
  column stores `'0000000001'`, zero-padded to ten characters. On SQLite it
  returns 0 where the answer is 4.

The distinction matters because the remedies differ. The second is not a *value*
problem — `contract_ref` has hundreds of distinct values and enumerating them
would blow the budget for no gain — it is a **format** problem, and one
exemplar fixes it. **Measured**, same model, temperature 0, one variable:

```
WITHOUT: WHERE contract_ref = '1'            -- 0 rows, silently, on SQLite
WITH:    WHERE contract_ref = '0000000001'   -- correct
```

One sample value, and not one of the other contract references listed.
**Value vocabulary for categorical columns, one exemplar for the rest.**

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

## 5c. The same SQL on two engines, and why the higher score is worse

The identical recorded queries, run against the identical rows, on PostgreSQL
and on SQLite. (SQLite has no schemas; `ATTACH` gives it one per database file,
so `warehouse.fact_volume` resolves there exactly as it does on PostgreSQL and
the same SQL runs unchanged — one query, two places to run it, rather than a
re-recording that would vary the query as well.)

| | scored | agreed | failed to execute |
|---|---|---|---|
| PostgreSQL | 10 | 7 | **2** |
| SQLite | 12 | **8** | 0 |

**SQLite scores higher and is not doing better.** The two queries PostgreSQL
refused both ran there, and they went opposite ways:

- `f_ledger_sums_zero` was **right all along** — the unquoted `1100, 4000` are
  the real account numbers — so PostgreSQL's refusal was a false negative and
  SQLite's answer is a true positive.
- `t_deals_for_contract` returned **0 where the answer is 4**, silently. On
  PostgreSQL that same query announced itself with a type error.

So one engine converts a correct answer into an error, and the other converts an
error into a wrong number. Neither is a property of NLQ, and **the value tier
cannot be run against only one of them** and still be said to measure what a
consumer will see. `gdash` runs on SQLite, which is the side where a bad literal
is silent.

---

## 5d. The library does not get to decide how long is too long

Every question varies in what it costs — the model by a factor of three on the
same hardware depending on load, the database by whatever the query turns out to
be. That is the reality, and **a library's job is to let an application be
graceful about it, not to be graceful on the application's behalf.**

One application will show a spinner and say this will take a moment. Another
will accept the question, return immediately, and email the answer. A third will
refuse anything it estimates will take too long. Those are different products,
and none of them is NLQ's call.

**So there is no single answer-it-all entry point** — no `nlq` function taking a catalog, a connection and a question and handing back a number. A single call that
runs the model and the query and hands back a number has made the decision by
hiding it: it blocks, and every consumer inherits blocking. This design proposed
exactly that function and withdraws it.

The shape instead is `agent`'s, which solved the same problem in this tree for
the same reason — an approval may take a minute and the wait spans HTTP
requests, so `agent.apply(run, event)` performs **no I/O** and returns the new
run plus the actions its caller must perform:

```
plan   = nlq.plan(catalog, vocab, question)   ' pure. grounding + prompt, or a refusal
                                              ' the application calls the model, however it likes
reading = nlq.interpret(plan, model_text)     ' pure. the SQL, and R3/R4 on it
                                              ' the application executes it, however it likes
answer = nlq.settle(reading, rows)            ' pure. the value, with its provenance
```

Three steps, none of which performs I/O, with the application owning both waits.
It may block on them, park a stream, hand them to a worker, put them on a queue,
or come back tomorrow — and NLQ neither knows nor needs to.

**Two consequences fall out, and both are requirements rather than
observations.**

A plan must **survive `encode`**, because an application that answers later must
store it in between. That is the constraint `agent` already discovered from the
same direction: a run holds `tools.schema` rather than a toolset because `encode`
refuses function values, and `expires_at` is a number because it refuses a
datetime. A plan that cannot be stored is a plan no deferring application can
use, and the suite should assert a plan round-trips and then produces the same
reading — a difference, since "it encodes" alone is satisfied by a plan that
encodes and then behaves differently.

And a plan should carry **what it is about to cost**, as far as that is knowable
— the token estimate is already there, the table count is already there. An
application that wants to say "this will take a while" needs something to decide
that from, and the alternative is every consumer re-deriving it from internals.

---

## 5e. Caching: facilitated, not owned

Nothing in NLQ caches, and nothing should. But a question costs seventeen to
sixty seconds of model time, and a dashboard asks **the same question every
refresh** — so not facilitating a cache would make the library unusable for its
first consumer while pretending to be neutral.

The same split as §5d applies: NLQ supplies the **key**, the application owns
the **store**.

### What may be cached is the SQL, never the answer

The question-to-SQL mapping is stable. The answer changes every time the data
does. An application that caches the *number* renders a stale figure that looks
exactly like a fresh one — which is §1's failure with a timestamp attached. So
a cache sits between `plan` and `interpret`, and never after `settle`.

### The key must cover everything the SQL depends on

Keying on the question text alone is the trap, because the SQL does not depend
on the question alone. `gdash` re-imports per dashboard; an import that renames
a column, drops one, or adds a view that now answers the question *better*
leaves cached SQL that **still runs** and quietly answers a question about the
old shape. A cached query surviving a schema change is the silent wrong answer
one level down from the one this design started with.

So `nlq.plan` returns a `key` over everything that determined its output:

- the question, normalized the way `terms` normalizes it;
- the **grounded objects and their columns** — not the whole catalog, which
  would invalidate on every unrelated import, and not the object names alone,
  which would survive a column being dropped;
- the **vocabulary those objects contributed**, since a changed value list
  changes the prompt and can change the literal;
- the synonyms in force, which are the estate owner's declaration and can be
  edited.

Canonically rendered with sorted keys, so it cannot depend on the order a
record was built in — the rule `llm.canonical_request` already follows, and for
the same reason.

### What the application decides, and NLQ cannot

Whether to keep a store at all, where, for how long, and when to drop it.
`gdash`'s answer is probably "until the next import" — a moment its importer
knows exactly and NLQ has no way to observe. An application pointed at a live
database might re-validate instead; one answering by email might cache nothing,
having already paid the wait.

**A miss must be indistinguishable from never having cached.** The key changing
should cost a slower answer, never a different one — so the suite should assert
that a plan built fresh and a plan whose key was hit produce the **same
reading**, which is the same difference-shaped assertion the `encode`
round-trip needs.

---

## 5f. At enterprise scale, the failure is crowding — not ranking

517 objects against the demo estate's 121, from `estateforge`'s own exporter.
`id` names **331 columns** and seventeen tables have `deal` in the name. The
questions are the same sixteen — estateforge flagged that itself: the enterprise
estate is harder **retrieval**, not harder questions, which is exactly what it
is wanted for.

Recall at `limit: 8` fell to **10 of 16**. Raising the limit says why:

| limit | recall | selected |
|---|---|---|
| 8 | 10/16 | 128 |
| 16 | 14/16 | 256 |
| 32 | 15/16 | 501 |
| 64 | 15/16 | 914 |

**The right tables ranked highly enough all along.** They were simply below the
cut, and 64 buys nothing over 32, so this is not a ranking failure. Every miss
had one cause: `archive.gl_account_2019`, `_002`, `_003`, `_004` and `_005`
score **identically**, fill six of eight slots, and crowd out `finance.gl_entry`
entirely. A ranked list spending six slots to say one thing six times has not
ranked badly — it has spent its budget on repetition.

**Collapsing numbered siblings takes `limit: 8` from 10/16 to 13/16** — the same
recall the 121-object estate gets at the same limit, at 4.3× the objects. The
rule is deliberately narrow: same schema, same name but for a trailing `_NNN`.
`fact_volume_daily`, `_hourly` and `_monthly` are granularity **variants**,
different tables answering different questions, and folding those would lose a
real candidate. And the siblings a selection stands for are **reported**, because
collapsing silently is the same class of defect as truncating a prompt.

Two things this does not claim. Raising the limit was nearly free here — even
32 cost 1804 tokens against a 3000 budget — but that is a property of this
estate's narrow tables, and the real price of a larger limit is the **thinking**
budget it takes from a reasoning model, which is not measured. And "collapse
numbered siblings" is a rule about one specific naming habit; an estate that
distinguishes copies some other way gets no help from it.

---

## 5g. Scored on estateforge's rubric — and two findings about this design

estateforge added what §5f asked for: **ambiguous** questions carrying every
side with its `via` and `because`, **traits** orthogonal to capability
(`encoded_literal`, `two_defensible_answers`, `not_in_the_catalog`), and
**unanswerable** questions drawn from `truth.undiscoverable`. Nineteen
questions, identical at both scales, scored right / partial / wrong with
**partial never folded into right** — "answered defensibly without noticing"
and "got it right" are different results.

| estate | objects | attempted | right |
|---|---|---|---|
| demo | 127 | 17 | **16** |
| enterprise | 517 | 17 | **14** |

**The ambiguous question scores right, and that is the first external check on
R2.** Both objects surfaced *and* the derivation between them disclosed — which
is exactly what `alternatives` was built to say and what no test of ours could
grade, because we wrote both the tool and the expectation.

### R6 was refusing a question that had answered itself

*"How many rows in `staging.tmp_load_notes` belong to a counterparty?"* **names
the schema.** There was nothing for the catalog to be unable to tell apart, and
R6 refused it anyway — a refusal of a question that settled itself, which is the
worst kind because it looks like rigour. Fixed: the same-name check now skips
when the question names one of the schemas.

### One of the two unanswerable passes was luck — and the gap is now closed

**First, the finding.**

Both unanswerable questions ask *which **job** reads this table, and what breaks
if it is dropped* — about jobs and dependencies, which a catalog does not model
at all. **There is no catalog-only signal for that**, and `unresolved` is not
one: `f_ledger_sums_zero` leaves 6 of 9 words unresolved and is perfectly
answerable, while the unanswerable pair leaves 4 of 9.

Before the fix above, one of the two *passed* — because the grounding happened
to pull in an unrelated numbered-schema collision and R6 fired on **that**. A
pass for the wrong reason.

**Then the fix, which is the same one everything else here uses.** "No
catalog-only signal" is true and is *not* the same as "no way". Whoever owns the
estate knows what it does not model — an application that performed an import
knows its import carried no job metadata — and `discovery_design` §4 already
carries "not in the database at all" as an outcome. So `not_modelled` is
**declared**, exactly as synonyms, vocabulary, exemplars and derivation are.

**The guard matters as much as the list.** A term counts as out-of-catalog only
when it *also reaches nothing*. An estate with a real `job` column models jobs
whatever a list says — the word resolved, so the catalog holds it — and that is
asserted as a control beside the refusal.

Measured on both estates: fires on **2 of 2** unanswerable and **0 of 17**
answerable.

| estate | attempted | right | unanswerable declined |
|---|---|---|---|
| demo | 19 | **18** | 2 for the declared reason |
| enterprise | 19 | **16** | 2 for the declared reason |

And the scorer credits a decline **only when the refusal names
`not_in_the_catalog`**, checking *any* of the reasons rather than the first,
since a question can be unanswerable twice over — on the demo estate one of
these also collides with the numbered staging schemas. Without that, the
accident described above would still be scoring.

### And a note on the crowding finding

estateforge points out that the numbered siblings which crowded out
`finance.gl_entry` come from their uniqueness guard appending `_002` on a name
collision — realistic, since real archives do exactly this, but an **artefact of
name generation rather than a planted pathology**. The crowding is real and the
fix is right; what it is not, is a difficulty anyone designed. Worth knowing
before treating "handles enterprise scale" as a claim about adversarial estates.

---

## 6. Deliberately not in the first increment

- **Any call to a model.** It comes after retrieval can be trusted.
- **Writing SQL.** Same reason.
- **Joins inferred from value overlap.** That is `discovery`'s inference
  increment, which needs a null model; NLQ consumes it and does not duplicate it.
- **Conversation.** A follow-up question that refines the previous one is a real
  requirement and a different problem; `agent` holds the run.
- **Anything that writes.** R4.
