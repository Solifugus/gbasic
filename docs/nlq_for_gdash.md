# For the gdash session: adding natural-language questions

gBASIC now ships `stdlib/nlq.bas` — **a question in English, over a database
estate**. It is ready for you to build on. This brief is what you need to start;
it is deliberately explicit about what the library does *not* do, because most
of the design decisions were about refusing to do things on your behalf.

Read these, in this order:

- `docs/nlq_cookbook.md` — eight runnable recipes, none of which needs a model
  or a database. Start here; it is the fastest way to see the shape.
- `docs/nlq_design.md` — why it is shaped that way, and every measurement
  behind it, including the ones that contradicted the first design.
- `docs/discovery_cookbook.md` — five recipes for the catalog layer underneath.
- `docs/reference.md`, the `nlq` and `discovery` sections.
- `tests/run_nlq.sh` and `tests/nlq/nlq_test.bas` — what is actually asserted.

## The one finding that shapes everything

**Retrieval is a search, and a search always returns a winner.**

Hand a model twenty tables chosen badly and it writes flawless SQL about the
wrong ones — and the SQL *runs*, because the tables it was given are real. The
number that comes back has the right units and the right order of magnitude and
nobody downstream can tell. Measured end to end against an independently
computed answer key: one question returned **4 025 053.56 where the answer was
6 285 487.93** — a 36% shortfall from summing a warehouse fact table that an ETL
had already filtered to active counterparties. Valid SQL, real table, wrong
question, nothing in the output saying so. That is the defect this library
exists to make visible, and it is why a grounding carries its search width, its
near misses, and a refusal it will not talk you out of.

Where it currently stands, scored by `estateforge` on a rubric **we did not
write**, 19 questions including deliberately ambiguous and deliberately
unanswerable ones, partial never folded into right:

| estate | objects | attempted | right |
|---|---|---|---|
| demo | 127 | 17 | 16 |
| enterprise | 517 | 17 | 14 |

Grounding only — no model, no database — so that is a gate rather than a
forecast. With a real 4B model's SQL run against live databases: **7 of 10 on
PostgreSQL, 8 of 12 on SQLite.**

## What `nlq` does, and what it leaves to you

`nlq` **performs no I/O at all**. No model call, no database connection, no
execution. Four things happen in your code between its four steps:

```
nlq.plan(cat, vocab, question, options)   ->  a prompt + a cache key
    ... you call the model ...
nlq.interpret(plan, model_text, options)  ->  checked SQL
    ... you run the SQL ...
nlq.settle(reading, rows)                 ->  the answer, with its query
```

That split is the design, not an omission. How long a model may take, where the
cache lives, whether to run two candidate queries and compare them, and what to
show a person when the catalog cannot settle a question are all decisions with
business consequences, and they belong where somebody can be accountable for
them. `nlq` supplies the facts those decisions need and does not make them.

## Your situation is the easy one, and that is worth exploiting

You import each dashboard's data into its own SQLite tables and views. That
means three things that are not true of a general estate:

1. **The estate is small.** Recall is a function of how many objects compete.
   At 517 objects `limit: 8` fell from 13/16 to 10/16 — and raising the limit
   showed the right tables had ranked highly enough all along, they were simply
   below the cut, crowded out by numbered near-duplicates spending six slots to
   say one thing six times. A dashboard's imported set is far smaller than
   either estate. You are unlikely to need a large `limit`, and you should
   measure rather than assume.
2. **You own the data, so sampling is free.** `nlq.vocabulary` and
   `nlq.exemplars` want a handful of values per column, and you can produce
   them from the tables you just imported. This matters more than it sounds:
   a schema says a column is `varchar(16)`; it does not say the thing in it is
   spelled `ACTIVE`, and a query that gets that wrong is valid SQL matching
   zero rows. Measured, same model, temperature 0, one variable: without an
   exemplar the model wrote `contract_ref = '1'` and got 0 rows silently where
   the answer is 4; with one, it wrote `'0000000001'` and got it right.
3. **You know when the data changed.** Which makes the cache tractable — see
   below.

And one that cuts the other way, which is the most important sentence in this
brief for you specifically:

> **SQLite is the engine on which a wrong literal is silent.**

The identical recorded queries were run against identical rows on PostgreSQL and
SQLite. SQLite scored *higher* (8 of 12 against 7 of 10) and was not doing
better. `t_deals_for_contract` returned **0 rows where the answer is 4** because
the column stores `'0000000001'` and the query said `= 1`; PostgreSQL announced
that with a type error, and SQLite — dynamically typed — compared the two
happily and returned an empty result that looks exactly like *"no matching
rows"*. You do not get the type error. So `nlq.exemplars` is not an optional
refinement in your architecture; it is the thing standing between a user and a
confident zero.

## Building the catalog

`discovery.scan(conn, options)` reads a live database through ODBC and gives you
a catalog keyed by object id. `nlq.from_discovery(cat)` converts it to the rows
`nlq` takes, preserving the id exactly. If you would rather build the catalog
from your own import metadata, an `nlq` catalog is just two arrays:

```basic
cat = { tables:  [ { schema: "main", table: "orders" } ],
        columns: [ { schema: "main", table: "orders", column: "status" } ] }
```

Either is fine. Use `discovery.scan` if you want the estate's own account of
itself; build it yourself if your importer already knows.

## The facts nobody can derive, and where they go

Some things a question needs are in nobody's schema:

- `ctp` means counterparty.
- `rpt_volume_gross` is built *from* `fact_volume`, so their totals can
  legitimately differ.
- This estate holds nothing about forecasts.

`discovery.annotate(cat, notes)` is where a dashboard author writes those down —
**once** — and `nlq.options_from(cat, options)` reads them back. The same notes
serve generated documentation and a lineage walk, which is why they live on the
catalog rather than beside the question. An explicit option always wins over a
standing note.

This is probably your most valuable UI surface. A dashboard author already knows
what their columns mean; today that knowledge is in their head and the model
cannot see it. Measured: declaring one synonym took a question from *"the word
`counterparty` matched nothing"* to the right table.

## What it refuses, and what you must do about each

Every refusal travels as a **value**, never a raise, precisely so you can put it
in front of a person.

| what comes back | what it means | what to do |
|---|---|---|
| `g.unresolved` | a question word matched **nothing** | offer to record a synonym; this is the likeliest place a grounding is quietly wrong |
| `plan.ok = false` with `refused_because` | the catalog cannot tell which object is meant (same name, schemas differing only by a number) | show the candidates and ask |
| `g.ambiguous` kind `not_in_the_catalog` | the estate holds nothing about this | say so; no query can supply it |
| `g.alternatives` | two objects both answer, one derived from the other | disclose; whether the difference matters cannot be known without running both, and that is your call |
| `interpret` problems `ungrounded_table` | the model named a table nobody offered | do not run it |
| `interpret` problems `not_read_only` | the statement would write | do not run it |
| `nlq.prompt` raising over budget | the schema will not fit | narrow the grounding — **never** truncate, because it is the schema that gets dropped, not the question |

**Updated 2026-09-14, from your step-0 report.** Two of the three findings are
fixed in the platform:

- **`plan` now refuses a question that grounds nothing**, with
  `refused_because` naming the words that reached nothing. `plan.ok` means
  *worth asking a model*. Your `check_answerable` call between `plan` and the
  wire is now belt and braces rather than the only guard — keep it; your suite
  asserting the premise is the right shape.
- **`options_from` carries `notes`**, built from `discovery.annotate`'s `means`
  and `unit`, and `plan` forwards them into the prompt under *"What the columns
  mean"*. That is your preferred option and it needed no new surface. Generate
  the note from `_gdash_meta` and hand it over:

  ```basic
  ann = discovery.annotate(cat, {
      "main.orders.amount": { means: "stored in US cents; 125075 means 1250.75",
                              unit: "cents" } })
  p = nlq.plan(nlq.from_discovery(ann), vocab, question, nlq.options_from(ann, {}))
  ```

  Worse than you reported, as it happens: `options_from` carried **nothing**
  at all, so `means` and `unit` reached no consumer despite both being note
  kinds `annotate` had always accepted.

  The stronger version you offered — `check_sql` testing a literal's magnitude
  against a stated scale — is **not** built. It is the right idea and it is the
  same sentence as `check_literals`; it is also the kind of rule that is easy
  to make fire on correct queries, so it wants its own measurement first.

- **The library-shadowing finding is not closed**, and is with Matthew, since
  precedence is a language decision rather than a library one. What did change
  is the warning, which named only the *ignored* path and never said which file
  was in force: it now reads `library 'x' resolved to A; ALSO FOUND and not
  used: B`. Measured here in support of your case: across this repository's
  whole gate, **zero** libraries resolve via the recursive search below the
  loading file — so restricting it would cost this tree nothing.

The refusal that fires on everything is worth knowing about: built without a
discriminator, the ambiguity refusal fired on **15 of 16** benchmark questions,
which is indistinguishable from having no tool. The current rule refuses only
when the catalog offers no distinction at all (`staging` / `staging_2`), and
lets regional partitions through (`trading_emea` / `trading_apac`), because
`emea` is a word a question can use. If you find it over-firing on your estates,
that is a measurement worth sending back.

## The cache: yours to own, keyed by us

A question costs seconds of model time and a dashboard asks the same one every
refresh, so a cache is not optional. `plan.key` is what you key on.

**Keying on the question text alone is the trap.** An import that drops a column
leaves cached SQL that still parses and quietly answers about a shape that is
gone. `plan.key` moves when the grounded objects' **columns** change, and does
*not* move when an unrelated table arrives — so an import does not invalidate
every question on the dashboard. Where the store lives and when it drops are
yours; *"until the next import"* names a moment your importer knows and this
library cannot observe.

`plan.estimated_tokens` is there so you can say *"this will take a while"*
before you start.

## Honest limits, so you do not discover them as bugs

- **Recall is lexical.** A question whose answer is reachable only through
  reading an ETL's `where` clause will not be found — measured, 1 of 16. The
  remedy is lineage, not more words.
- **`check_sql` is lexical too, and says so.** It reads the identifiers after
  `from` and `join`. It is not a SQL parser, and a query hiding a table name in
  a construct it does not model would pass. `discovery`'s own statement reader
  parses SQL properly; wiring the two together is work not yet done.
- **Answer quality tracks the model.** The measurements above used a small
  local model through Ollama. A caution from that work: Ollama's default
  `num_ctx` is 4096 regardless of what the model card declares, and a prompt
  over it is **silently truncated from the head** — which drops the schema and
  keeps the question, producing a confident answer about nothing. Set it
  explicitly.
- **Nothing here has met your estates.** Every measurement is against
  `estateforge`'s generated estates, run on a real PostgreSQL database and a
  real SQLite one. Recall, refusal rate and answer agreement on a real
  dashboard's imported tables are unmeasured, and measuring them is the most
  useful thing you could send back.

## What would help most, sent back

1. **Recall and refusal rate on a real dashboard's estate**, with the questions
   its users actually ask. Both numbers, not one: recall alone is maximised by
   selecting everything, and a low refusal rate alone by refusing nothing.
2. **A refusal that fired where it should not have**, with the catalog shape
   that caused it. That is how the 15-of-16 discriminator was found.
3. **A question that was answered confidently and wrongly** — the R2 case. Those
   are the expensive ones and they do not announce themselves.
