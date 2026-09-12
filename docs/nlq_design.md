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

## 6. Deliberately not in the first increment

- **Any call to a model.** It comes after retrieval can be trusted.
- **Writing SQL.** Same reason.
- **Joins inferred from value overlap.** That is `discovery`'s inference
  increment, which needs a null model; NLQ consumes it and does not duplicate it.
- **Conversation.** A follow-up question that refines the previous one is a real
  requirement and a different problem; `agent` holds the run.
- **Anything that writes.** R4.
