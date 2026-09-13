# The gBASIC nlq cookbook

Eight recipes for `stdlib/nlq.bas` — **a question in English, over an estate
nobody can hold in their head**. Every code block below is a real program in
`examples/nlq_cookbook/`, and every output block is its committed golden;
`tests/run_nlq_cookbook.sh` fails while this page disagrees with either. The
page cannot lie.

**None of these recipes calls a model or touches a database.** That is not a
convenience for the tutorial — it is what the first increment *is*. Grounding is
a pure function of a catalog and a question; the prompt is text; `interpret`
reads SQL that somebody else produced. The part that decides **which tables a
question is about** could therefore be built and measured before any model was
involved at all, which matters because that part is where the expensive mistakes
live.

The governing finding, and the reason this library exists in this shape:
**retrieval is a search, and a search always returns a winner.** Hand a model
twenty tables chosen badly and it writes flawless SQL about the wrong ones — and
the SQL *runs*, because the tables it was given are real. The number that comes
back has the right units and the right order of magnitude and nobody downstream
can tell. So a grounding carries its own **search width** and its **near
misses**, and refuses rather than guesses when the catalog cannot settle a
question. Design and rationale: [`nlq_design.md`](nlq_design.md).

## 1. A first grounding

Give it a catalog and a question; it tells you which objects the question is
about, what it nearly chose instead, and how wide the search was.

A catalog here is two arrays. `discovery.scan` produces one keyed by id
instead — recipe 8 converts it.

<!--CODE:01_a_first_grounding-->

```basic
' Grounding a question -- no model, no database.
load nlq

cat = { tables: [ { schema: "trading",   table: "deal" },
                  { schema: "trading",   table: "counterparty" },
                  { schema: "finance",   table: "gl_entry" },
                  { schema: "warehouse", table: "rpt_volume_gross" } ],
        columns: [ { schema: "trading",   table: "deal",             column: "deal_status" },
                   { schema: "trading",   table: "deal",             column: "gross_vol_mmbtu" },
                   { schema: "trading",   table: "counterparty",     column: "name" },
                   { schema: "finance",   table: "gl_entry",         column: "amount" },
                   { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" } ] }

g = nlq.ground(cat, "What is the gross volume by counterparty?", { limit: 2 })

print "terms:     " + join(g.terms, ", ")
print "selected:  " + join(g.tables, ", ")
print "near miss: " + join(g.near_misses, ", ")
print ""
print ("searched " + string(g.search.width) + " tables, " + string(g.search.matched) +
       " matched, kept " + string(g.search.limit) + ", cut at score " + string(g.search.cut))
print ""
for each row in g.detail
    print "  " + row.id + "  score " + string(row.score)
end for
```

<!--OUT:01_a_first_grounding-->

```
terms:     gross, volume, counterparty
selected:  warehouse.rpt_volume_gross, trading.counterparty
near miss: trading.deal

searched 4 tables, 3 matched, kept 2, cut at score 3

  warehouse.rpt_volume_gross  score 7
  trading.counterparty  score 3
```

`near miss` is the half a ranked list cannot show. *"The answer is
`warehouse.rpt_volume_gross`"* and *"the answer is
`warehouse.rpt_volume_gross`, and `trading.deal` was next"* are different
claims, and only the second lets a reader see that the search had a close call.

## 2. A word the schema never heard

`unresolved` names a question term that matched **nothing**. It is the likeliest
place a grounding is silently wrong, because a ranked list looks identical
whether a word reached everything or nothing at all.

<!--CODE:02_a_word_the_schema_never_heard-->

```basic
' A question word that matched nothing, and the synonym that closes it.
load nlq

cat = { tables: [ { schema: "warehouse", table: "rpt_volume_gross" },
                  { schema: "trading",   table: "ctp" } ],
        columns: [ { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" },
                   { schema: "warehouse", table: "rpt_volume_gross", column: "ctp_code" },
                   { schema: "trading",   table: "ctp",              column: "code" },
                   { schema: "trading",   table: "ctp",              column: "name" } ] }

question = "gross volume by counterparty"

g = nlq.ground(cat, question, {})
print "unresolved: " + join(g.unresolved, ", ")
print "selected:   " + join(g.tables, ", ")

' Nobody outside this business knows that `ctp` means counterparty. Say so once.
syn = { counterparty: [ "ctp" ] }

g2 = nlq.ground(cat, question, { synonyms: syn })
print ""
print "with the synonym declared --"
print "unresolved: " + string(count(g2.unresolved))
print "selected:   " + join(g2.tables, ", ")
```

<!--OUT:02_a_word_the_schema_never_heard-->

```
unresolved: counterparty
selected:   warehouse.rpt_volume_gross

with the synonym declared --
unresolved: 0
selected:   warehouse.rpt_volume_gross, trading.ctp
```

Nobody outside this business knows that `ctp` means counterparty, and no amount
of reading the SQL will discover it. Declaring the synonym once closes the gap —
and the object it was hiding is now in the answer.

## 3. When it refuses

Three staging schemas differing only by a **number**. Which one is live is a
fact that exists in somebody's head, not in the catalog, so ranking one above
the others would be a guess wearing the clothes of a lookup. `plan` refuses, and
the refusal travels as a **value** — an application can show a person the
candidates and ask.

The second half is the control, and it is not decoration. Built without a
discriminator, this refusal fired on **15 of 16** benchmark questions; a refusal
that fires on everything is indistinguishable from having no tool. The
discriminator is whether the catalog offers a distinction **at all**: `emea` and
`apac` are words a question can use, a trailing `_2` is not.

<!--CODE:03_when_it_refuses-->

```basic
' A question the catalog cannot settle, and a near neighbour it can.
load nlq

' Three staging schemas differing only by a NUMBER. Nothing here says which one
' is live -- that fact exists in somebody's head, not in the catalog.
staged = { tables: [ { schema: "staging",   table: "deal_load" },
                     { schema: "staging_2", table: "deal_load" },
                     { schema: "staging_3", table: "deal_load" } ],
           columns: [ { schema: "staging",   table: "deal_load", column: "deal_id" },
                      { schema: "staging_2", table: "deal_load", column: "deal_id" },
                      { schema: "staging_3", table: "deal_load", column: "deal_id" } ] }

p = nlq.plan(staged, {}, "how many rows are in the deal load?", {})
print "ok: " + string(p.ok)
for each r in p.refused_because
    print "  " + r.kind + ": " + join(r.candidates, ", ")
    print "  why: " + r.why
end for

' Now three REGIONAL schemas. Same shape, same column, same repeated table
' name -- but `emea` and `apac` are words a question can use, so the catalog
' does offer a distinction and the refusal must not fire.
regional = { tables: [ { schema: "trading",      table: "deal" },
                       { schema: "trading_emea", table: "deal" },
                       { schema: "trading_apac", table: "deal" } ],
             columns: [ { schema: "trading",      table: "deal", column: "deal_id" },
                        { schema: "trading_emea", table: "deal", column: "deal_id" },
                        { schema: "trading_apac", table: "deal", column: "deal_id" } ] }

p2 = nlq.plan(regional, {}, "how many deals are there?", {})
print ""
print "regional partitions -- ok: " + string(p2.ok)
print "tables: " + join(p2.tables, ", ")
```

<!--OUT:03_when_it_refuses-->

```
ok: false
  same_name_different_schema: staging.deal_load, staging_2.deal_load, staging_3.deal_load
  why: these schemas differ only by a number, so the catalog does not say which is live; ranking one would be a guess

regional partitions -- ok: true
tables: trading.deal, trading_apac.deal, trading_emea.deal
```

## 4. The values a column holds

A schema says a column is `varchar(16)`. It does not say the thing in it is
spelled `ACTIVE`. Measured, that is where a generated query goes quietly wrong:
the SQL is valid, the table is right, and it matches zero rows.

<!--CODE:04_the_values_a_column_holds-->

```basic
' What a column actually contains -- and the literal the question got wrong.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" } ],
        columns: [ { schema: "trading", table: "deal", column: "deal_status" },
                   { schema: "trading", table: "deal", column: "deal_ref" } ] }

' Sampled from the database, however you like to sample it.
samples = [ { schema: "trading", table: "deal", column: "deal_status",
              values: [ "ACTIVE", "SETTLED", "CANCELLED" ] },
            { schema: "trading", table: "deal", column: "deal_ref",
              values: [ "D-0001", "D-0002", "D-0000000917" ] } ]

vocab = nlq.vocabulary(samples, { max_values: 5 })
for each k in keys(vocab)
    print k + ": " + join(vocab[k], ", ")
end for

' A column with too many distinct values has NO vocabulary -- a truncated list
' would report a real value as unknown. It gets an exemplar instead: not which
' values exist, but what one LOOKS like. The longest is chosen, because a
' format is best shown by its fullest instance.
wide = nlq.vocabulary(samples, { max_values: 2 })
print ""
print "capped at 2 -- columns with a vocabulary: " + string(count(keys(wide)))

ex = nlq.exemplars(samples, { min_distinct: 2 })
print "deal_ref looks like: " + ex["trading.deal.deal_ref"]

' The measured failure: the question says `active`, the column stores `ACTIVE`.
g = nlq.ground(cat, "how many deals are active?", {})
for each c in nlq.check_literals(g, vocab, "how many deals are active?")
    print ""
    print "said '" + c.said + "' but " + c.column + " stores '" + c.means + "'"
    print "why: " + c.why
end for
```

<!--OUT:04_the_values_a_column_holds-->

```
trading.deal.deal_status: ACTIVE, SETTLED, CANCELLED
trading.deal.deal_ref: D-0001, D-0002, D-0000000917

capped at 2 -- columns with a vocabulary: 0
deal_ref looks like: D-0000000917

said 'active' but trading.deal.deal_status stores 'ACTIVE'
why: the question's wording differs in case from the stored value
```

Two rules worth the space:

- A column with **too many** distinct values gets **no** vocabulary rather than
  a truncated one. A partial list is worse than none, because a real value
  missing from it would be reported as unknown when it is merely unlisted.
- Such a column gets an **exemplar** instead — not which values exist, but what
  one *looks like*. The longest is chosen, because a format is best shown by its
  fullest instance. Measured: a question about *"contract 1"* against a column
  storing `'0000000001'` produced `contract_ref = 1`, which is 0 rows where the
  answer is 4, silently. One example fixes it.

## 5. The prompt it builds

What the model is actually shown. There is no magic in it, which is the point:
it is the grounded objects with their columns, the values those columns hold,
the formats they take, and the question.

<!--CODE:05_the_prompt_it_builds-->

```basic
' What the model is actually shown -- and the budget it must fit.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" },
                  { schema: "trading", table: "counterparty" },
                  { schema: "finance", table: "gl_entry" } ],
        columns: [ { schema: "trading", table: "deal",         column: "deal_status" },
                   { schema: "trading", table: "deal",         column: "ctp_id" },
                   { schema: "trading", table: "deal",         column: "gross_vol_mmbtu" },
                   { schema: "trading", table: "counterparty", column: "ctp_id" },
                   { schema: "trading", table: "counterparty", column: "name" },
                   { schema: "finance", table: "gl_entry",     column: "amount" } ] }

samples = [ { schema: "trading", table: "deal", column: "deal_status",
              values: [ "ACTIVE", "SETTLED" ] } ]
vocab = nlq.vocabulary(samples, {})

question = "gross volume by counterparty for active deals"
g = nlq.ground(cat, question, { limit: 2 })
p = nlq.prompt(g, cat, vocab, question, { dialect: "SQLite SQL" })

print p.system
print ""
print p.user
print ""
print "about " + string(p.estimated_tokens) + " tokens, budget " + string(p.budget)

' A budget that cannot hold the schema is a REFUSAL, not a truncation. Truncate
' and it is the schema that gets dropped, never the question -- so the model
' answers confidently about the tables that survived the cut.
on error goto next
nlq.prompt(g, cat, vocab, question, { dialect: "SQLite SQL", budget_tokens: 20 })
if error then
    print ""
    print "refused: " + error.message
end if
```

<!--OUT:05_the_prompt_it_builds-->

```
You write one SQLite SQL SELECT statement that answers the question. Use ONLY the tables and columns listed. Use the exact column values given. Reply with SQL only: no markdown, no fence, no explanation.

Tables:
trading.deal(deal_status, ctp_id, gross_vol_mmbtu)
trading.counterparty(ctp_id, name)
Column values:
trading.deal.deal_status in (ACTIVE, SETTLED)

Question: gross volume by counterparty for active deals

about 166 tokens, budget 3000

refused: nlq.prompt: this prompt is about 166 tokens against a budget of 20 -- narrow the grounding (limit is 2, 2 tables selected). Sending it would be truncated silently, and it is the SCHEMA that would be dropped, not the question
```

A budget that cannot hold the schema is a **refusal**, not a truncation.
Truncate and it is the schema that gets dropped, never the question — so the
model answers confidently about whichever tables survived the cut, and nothing
in the answer says so.

## 6. Reading the answer back

`plan` → `interpret` → `settle`. The model is a string literal here; nothing
calls out.

<!--CODE:06_reading_the_answer_back-->

```basic
' plan -> interpret -> settle. The model is a string here; nothing calls out.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" },
                  { schema: "archive", table: "deal_2015" } ],
        columns: [ { schema: "trading", table: "deal",      column: "deal_status" },
                   { schema: "trading", table: "deal",      column: "gross_vol_mmbtu" },
                   { schema: "archive", table: "deal_2015", column: "gross_vol_mmbtu" } ] }

question = "total gross volume of active deals"
p = nlq.plan(cat, {}, question, { limit: 1, dialect: "SQLite SQL" })
print "planned: " + join(p.tables, ", ")

' What the model said. A fence is stripped rather than failed on -- that is
' punctuation, not a refusal to follow the instruction.
said = ("```sql" + chr(10) +
        "select sum(gross_vol_mmbtu) from trading.deal where deal_status = 'ACTIVE'" + chr(10) +
        "```")
r = nlq.interpret(p, said, {})
print "ok:  " + string(r.ok)
print "sql: " + r.sql

' R3: a table the grounding never surfaced. It is spelled correctly and it
' EXISTS, so it runs -- and answers about 2015.
bad = nlq.interpret(p, "select sum(gross_vol_mmbtu) from archive.deal_2015", {})
print ""
print "off-plan -- ok: " + string(bad.ok)
for each pr in bad.problems
    print "  " + pr.kind + " (" + pr.detail + "): " + pr.why
end for

' R4: a statement that writes is refused by inspection, before anything runs
' it. The system prompt asks for a SELECT, and an instruction is not a control.
writes = nlq.interpret(p, "delete from trading.deal where deal_status = 'ACTIVE'", {})
print ""
print "a write -- ok: " + string(writes.ok)
for each pr in writes.problems
    print "  " + pr.kind + " (" + pr.detail + "): " + pr.why
end for

' Rows came back from wherever you run SQL. R5: an answer never travels
' without the query that produced it.
a = nlq.settle(r, [ { total: 41250.5 } ])
print ""
print "answer: " + string(a.value) + " (" + a.column + "), from " + string(a.row_count) + " row"
print "asked:  " + a.question
print "by:     " + a.sql
```

<!--OUT:06_reading_the_answer_back-->

```
planned: trading.deal
ok:  true
sql: select sum(gross_vol_mmbtu) from trading.deal where deal_status = 'ACTIVE'

off-plan -- ok: false
  ungrounded_table (archive.deal_2015): the grounding never surfaced it; the query names a table nobody offered, which runs if the name happens to exist

a write -- ok: false
  not_read_only (delete): this statement would change the database
  not_a_query (): no select at all

answer: 41250.5 (total), from 1 row
asked:  total gross volume of active deals
by:     select sum(gross_vol_mmbtu) from trading.deal where deal_status = 'ACTIVE'
```

Three things happened there worth naming:

- **A fence is stripped, not failed on.** That is punctuation, not a refusal to
  follow the instruction.
- **A table the grounding never surfaced is refused** (R3). It is spelled
  correctly and it *exists*, so it runs — and answers about 2015. Checked
  against what the grounding actually selected, not against the catalog,
  because the catalog contains every wrong answer too. This check is lexical
  and says so: it reads the identifiers after `from` and `join`, and is not a
  SQL parser.
- **A statement that writes is refused by inspection** (R4), before anything
  runs it. The system prompt asks for a `SELECT`; an instruction is not a
  control.

And `settle` returns the value **with its query**: *where did this number come
from* is the question a business asks second, immediately.

## 7. The cache key

A question costs seconds of model time and a dashboard asks the same one every
refresh. `nlq` facilitates a cache and does not own one — where the store lives,
how long it keeps and when it drops are the application's, and its answer
("until the next import") names a moment this library cannot observe.

What `nlq` owes is a key that moves when, and only when, the cached SQL stops
being right.

<!--CODE:07_the_cache_key-->

```basic
' A question costs seconds of model time and a dashboard asks it every refresh.
' nlq facilitates a cache; it does not own one.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" } ],
        columns: [ { schema: "trading", table: "deal", column: "deal_status" },
                   { schema: "trading", table: "deal", column: "gross_vol_mmbtu" } ] }

question = "total gross volume of active deals"
base = nlq.plan(cat, {}, question, {}).key
print "key: " + base

' Asking again changes nothing, which is the point.
print "same question again:        " + string(nlq.plan(cat, {}, question, {}).key = base)

' An unrelated table arrives in the next import. The cached SQL is still right,
' so the key must not move -- keying on the whole catalog would invalidate
' every question on every import.
more = cat
append(more.tables, { schema: "finance", table: "gl_entry" })
append(more.columns, { schema: "finance", table: "gl_entry", column: "amount" })
print "an unrelated table arrives: " + string(nlq.plan(more, {}, question, {}).key = base)

' A column the question's own table lost. The cached SQL STILL RUNS on some
' databases and answers about a shape that is gone -- so the key must move.
' Keying on table NAMES alone would survive this, which is the trap.
thin = { tables: cat.tables,
         columns: [ { schema: "trading", table: "deal", column: "deal_status" } ] }
print "its own column is dropped:  " + string(nlq.plan(thin, {}, question, {}).key = base)

' And a synonym in force is part of the question's meaning.
syn = nlq.plan(cat, {}, question, { synonyms: { volume: [ "mmbtu" ] } }).key
print "a synonym is declared:      " + string(syn = base)
```

<!--OUT:07_the_cache_key-->

```
key: bd7806f7
same question again:        true
an unrelated table arrives: true
its own column is dropped:  false
a synonym is declared:      false
```

**Keying on the question text alone is the trap.** An import that drops a column
leaves cached SQL that still parses on some databases and quietly answers about
a shape that is gone. So the key covers the grounded objects **with their
columns** — not the whole catalog, which would invalidate every question on
every unrelated import, and not the names alone, which would survive exactly the
change that matters.

## 8. What a person knows

The facts a database cannot state about itself: what `ctp` is called in English,
which of two identical column names is the derived one, and a subject the estate
simply does not hold.

These are properties of the **estate**, not of any one question, so they are
written down once in `discovery.annotate` — where they also serve generated
documentation and a lineage walk — and `nlq` reads them rather than keeping a
second copy.

<!--CODE:08_what_a_person_knows-->

```basic
' The facts a database cannot state about itself, written down once.
load nlq
load discovery

' A catalog as `discovery.scan` produces one: objects keyed by id.
cat = { source: "warehouse",
        tables: { "warehouse.rpt_volume_gross": { schema: "warehouse", table: "rpt_volume_gross", column: "" },
                  "warehouse.fact_volume":      { schema: "warehouse", table: "fact_volume", column: "" },
                  "trading.ctp":                { schema: "trading", table: "ctp", column: "" } },
        columns: { "warehouse.rpt_volume_gross.total_volume": { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" },
                   "warehouse.fact_volume.total_volume":      { schema: "warehouse", table: "fact_volume", column: "total_volume" },
                   "trading.ctp.code":                        { schema: "trading", table: "ctp", column: "code" } },
        primary_keys: [], edges: [] }

' Three things nobody could derive from the SQL: what `ctp` is called in
' English, which of two identical column names is the derived one, and a
' subject the estate simply does not hold.
ann = discovery.annotate(cat, {
        "trading.ctp": { synonyms: [ "counterparty" ], means: "a trading counterparty" },
        "warehouse.rpt_volume_gross": { derived_from: [ "warehouse.fact_volume" ] },
        "": { not_modelled: [ "forecast" ] } })

' The same notes serve documentation, a lineage walk, and a question. nlq reads
' them rather than keeping a second copy.
opts = nlq.options_from(ann, { limit: 3 })
print "synonyms:     " + join(keys(opts.synonyms), ", ")
print "derived_from: " + join(keys(opts.derived_from), ", ")
print "not_modelled: " + join(opts.not_modelled, ", ")

' A discovery catalog keys by id; this library takes rows. Converting preserves
' the id exactly, which is what makes the notes above line up with what a
' grounding produces.
qcat = nlq.from_discovery(ann)

g = nlq.ground(qcat, "gross volume by counterparty", nlq.options_from(ann, { limit: 3 }))
print ""
print "selected:   " + join(g.tables, ", ")
print "unresolved: " + string(count(g.unresolved))

' R2 TRAVELS AS A FACT, NOT A REFUSAL. Two objects both offer a
' `total_volume` and one is derived from the other, so the two numbers may
' legitimately differ -- nlq cannot tell whether that difference is the whole
' answer or noise without running both, and that is the application's call.
for each alt in g.alternatives
    print "alternative: " + join(alt.candidates, " and ") + " -- " + alt.why
end for

' And a question about something the estate does not model is said so, rather
' than answered from whatever happened to score highest.
g2 = nlq.ground(qcat, "what is the volume forecast?", nlq.options_from(ann, { limit: 3 }))
for each a in g2.ambiguous
    print ""
    print a.kind + ": " + join(a.candidates, ", ")
    print "why: " + a.why
end for
```

<!--OUT:08_what_a_person_knows-->

```
synonyms:     counterparty
derived_from: warehouse.rpt_volume_gross
not_modelled: forecast

selected:   warehouse.rpt_volume_gross, warehouse.fact_volume, trading.ctp
unresolved: 0
alternative: warehouse.rpt_volume_gross and warehouse.fact_volume -- warehouse.rpt_volume_gross is built from warehouse.fact_volume, so it can hold fewer rows and their totals can differ; the query cannot say which was meant

not_in_the_catalog: forecast
why: this estate holds nothing about forecast; the question asks for a fact that is not in the database, which no query can supply
```

Two shapes meet here. `discovery.scan` keys its objects by id;
this library takes rows. `nlq.from_discovery` converts, preserving the id
exactly — which it must, because the notes above are written against discovery
ids and a grounding has to look them up under the same name.

The last two outputs are the two ways a question can be *about something that is
not there*:

- **An alternative is disclosed, not refused** (R2). Two objects both offer a
  `total_volume` and one is built from the other, so the totals may legitimately
  differ. Whether that difference is the whole answer or noise cannot be known
  without running both — measured, for one question the two derivations differ
  by 36% and the distinction *is* the answer; for another they agree exactly.
  `nlq` cannot tell those apart, so the fact travels and the choice does not.
- **A subject the estate does not model is said so**, rather than answered from
  whatever scored highest. No query can supply a fact that is not in the
  database, and the confident wrong answer is the one this library exists to
  prevent.

## Where this stops

This is the first increment, and the line is worth stating plainly. `nlq`
**grounds** a question, **builds** a prompt, **checks** the SQL that comes back
and **settles** the answer with its provenance. It does not call a model, does
not connect to a database, and does not execute anything — an application does
those, because how long a model may take, where the cache lives, and whether to
run two candidate queries and compare them are decisions with business
consequences that belong where somebody can be accountable for them.
