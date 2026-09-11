# `discovery`: what a database estate says about itself

**Status:** Design (2026-09-08). First increment not yet built.

## 1. The split that governs everything

Two kinds of claim, and the library must never let the second wear the clothes
of the first:

- **Declared** — *read* from the catalog or from a view definition. Certain.
- **Inferred** — the result of a **search**, and therefore carrying its width,
  its null model and its threshold, exactly as `reasoning.finding` requires.

The failure this prevents is Recipe 1's, one domain over. Point a tool at a
schema and ask where a column came from, and value overlap plus name
similarity will produce a confident answer **on an estate where no such
relationship exists**. A 500-table estate at 20 columns each is 10,000 columns
— roughly 50 million candidate pairs. At that width, coincidences are not a
risk, they are a certainty: every `status` overlaps every other `status`, and
every surrogate key is 1..N.

## 1a. The question this exists to answer

*Added 2026-09-09, from working with real company data. It reframes the
library's purpose and reorders what gets built.*

The headline question is **not** "what is related to what". It is:

> Two reports both show a number called `revenue`. **Why are they different?**

Today that costs someone a day of manual research. The answer is never "these
are unrelated" — both numbers are correct. It is *"this one excludes cancelled
orders"*, or *"this one books at order date and that one at ship date"*, or
*"this one joins a lookup that filters out inactive counterparties"*.

Three consequences, and each changes the design:

**Lineage is path-valued, not edge-valued.** Two columns that disagree usually
share an ancestor and **diverge at a step**. What the user needs is the
divergence point, so a set of edges is not an answer — the ordered path is.

**The predicate is part of the lineage.** A `where` clause is not metadata
about a derivation, it *is* the reason two numbers differ. Filters, join
conditions and grain changes must be carried along the path, or the library can
say two columns are related and still not answer the only question asked.

**The interesting hops are code, not constraints.** A real flow runs tables →
views → stored procedures → tables, with several tables feeding in and several
resulting. The catalog records none of that. **But the code is declared and
readable** — a view's SQL and a procedure's text can be parsed — so this is
*not* an inference problem. It belongs to the declared-facts family, where the
answer is certain rather than searched for.

That last point reorders the roadmap: **column-level lineage and predicates
extracted from view and procedure source come before any value-overlap
inference.** They answer a question people actually ask, and they answer it
with certainty.

A lookup table also plays a role the catalog cannot show: used as a **filter**
it contributes no column to the output while changing which rows reach it, so
it is part of the derivation context while being invisible in the result
(estate R34).

## 1b. Parse the SQL. Do not trust dependency metadata.

*Added 2026-09-09 on Matthew's report of having built this before, in Python,
against SQL Server: tracing forward and backward through long successions of
procedures, views and tables. **The metadata approach failed repeatedly** — it
changed between SQL Server versions and parts of it were never explicable.
**Parsing the SQL succeeded.***

That experience is corroborated by the product's own history:
`sys.sql_dependencies` was deprecated precisely because it could not cope with
deferred name resolution, and its replacement behaves differently again — so a
lineage tool built on it is built on a moving floor.

**The source text, by contrast, is retrievable everywhere.** Measured
2026-09-09 against all four:

| | view source | procedure source |
|---|---|---|
| SQLite | `sqlite_master.sql` | *no stored procedures* |
| MariaDB | `information_schema.views.view_definition` | `information_schema.routines.routine_definition` |
| PostgreSQL | `pg_get_viewdef()` | `pg_proc.prosrc` |
| SQL Server | `sys.sql_modules.definition` | `sys.sql_modules.definition` |

Two findings from that run, and the first inverts the expected difficulty:

**Views come back rewritten on MariaDB and PostgreSQL, verbatim on SQLite and
SQL Server.** MariaDB returns ``select `db`.`t`.`id` AS `id` …`` — fully
qualified, with the column-level mapping *already resolved by the server*.
PostgreSQL pretty-prints and makes casts explicit (`state::text = 'OPEN'::text`).
So the two databases that rewrite hand you lineage the other two make you
derive. A parser must handle both shapes, and "show me the source" for
documentation is a *different* question from "parse this for lineage", because
on two of four the stored text is not what the author wrote.

**Procedure bodies come back verbatim on all three that have them.** A body is
stored as text rather than compiled into a queryable tree, so there is no
metadata shortcut even in principle. Parsing is not a preference here; it is
the only route.

### References are not always qualified, so parsing is two phases

Reported from the same experience — SQL Server references were often
unqualified and the Python implementation had to correct for it. That is not a
parsing problem, it is a **resolution** problem, and separating the two is the
architecture:

> **The parser produces unresolved references. A resolver binds them using the
> catalog.**

`discovery.scan` already provides the catalog, so the resolver has what it
needs — but it needs one thing more, and this is a requirement on the module
reader rather than on the parser: **the schema that OWNS the code**. An
unqualified name is resolved relative to its container, so a view or procedure
must be captured with its schema, which is load-bearing rather than decoration.

Measured 2026-09-09 against SQL Server, both halves:

- a procedure in schema `alt` referencing bare `res_probe`, where **both**
  `alt.res_probe` and `dbo.res_probe` exist, binds to **`alt.res_probe`** — its
  own schema wins;
- the same procedure referencing a table that exists **only** in `dbo` binds to
  `dbo` — so the search path is `[own schema, dbo]`, in that order.

The stored source in both cases reads `from res_probe`. Nothing in the text
says which table it means.

Per dialect:

| | how a bare name resolves |
|---|---|
| SQL Server | the containing object's schema, then `dbo` — **measured** |
| PostgreSQL | the `search_path`, measured here as `"$user", public`; a function may carry its own via `pg_proc.proconfig` |
| MariaDB | no schemas — the database is the only qualifier, so a bare name is the current database |
| SQLite | no schemas |

**Where resolution is genuinely ambiguous, it is refused, not guessed.** A
cross-database reference with no qualifier depends on the connection's current
database; a PostgreSQL `search_path` is session state and may not be the one
the author had. Those are R3b outcomes — *unanswerable* — and binding them to a
plausible candidate would produce a lineage graph that is confident and wrong,
which is the failure this whole design is arranged against.

### What the parser must do, and what it must refuse

Lineage does not need a complete SQL implementation. It needs: which objects
are **read** (`from`, `join`, subqueries, CTEs), which are **written**
(`insert`, `update`, `delete`, `merge`, `select into`), the column mapping in
the projection, and **the predicates** — which §1a establishes are not metadata
about the derivation but the answer to the question being asked.

It must be **partial, and say so**. Dynamic SQL assembled at run time, `select *`
through several hops, a procedure branching on a parameter — these are not
parse failures to be guessed past. They are the R3b outcome: *unanswerable from
the source*, reported as such. A tracer that silently drops a hop it could not
read produces a lineage graph that is confidently incomplete, which is worse
than one that names its gaps.

### What it is for

Three uses, in the order they pay off:

- **Impact analysis** — what does changing this column affect, upstream and
  downstream? This is what a developer needs *before* altering anything, and it
  is the use with the shortest path to value.
- **Documentation** — flows that are currently reconstructed by hand, written
  down and kept current because they are derived rather than maintained.
- **NLQ** — which needs to know not just what tables exist but which one holds
  the measure the question is about, and under which derivation.

## 2. The first increment is declared facts only

No inference at all. Tables, columns, types, nullability, primary keys, foreign
keys, and view definitions — read, never guessed.

Three reasons it comes first, in order of weight: it has a correctness
criterion that needs no null model; it is what NLQ needs before anything else,
since schema retrieval must precede prompting (500 tables do not fit in a
context window); and the declared sources are *free and certain*, so a tool
that reaches for value overlap before exhausting them is doing the hard, wrong
thing first.

`information_schema.view_column_usage` in particular gives real **column-level**
lineage for every view, with no inference whatsoever.

## 3. The value model, and why it looks like this

Every constraint below was **measured** on 2026-09-08 against four drivers, not
reasoned about.

**A column's identity is `(source, catalog, schema, table, column)`, with
catalog and schema both optional.** Not `schema.table.column`:

| | `TABLE_CAT` | `TABLE_SCHEM` |
|---|---|---|
| MariaDB | the database | *empty* |
| PostgreSQL / SQL Server | the database | `public` / `dbo` |
| SQLite | *empty* | *empty* |

A key built as `schema.table` yields `nothing.orders` on MariaDB — silently, in
a library whose entire job is identifying columns.

**Type comparison across sources needs its own mapping.** The same
`varchar(20)` reports `DATA_TYPE` 12 on three drivers and −9 on MariaDB, and
`TYPE_NAME` is `int4` / `int` / `INT` / `INTEGER` for one column. Both
PostgreSQL drivers agree on 12, so it is not an ANSI/Unicode split — drivers
simply disagree. A type prefilter comparing codes across sources rejects real
relationships before any value is examined (estate R30).

**Nullability: read the numeric `NULLABLE`, and not from SQLite.**
`IS_NULLABLE` is empty on PostgreSQL, and SQLite reports a primary key as
nullable, which is false.

**The graph is ID-indexed, never pointer-linked.** gBASIC has no references and
is not getting them, so nodes are keyed records and edges are `(from, to)`
pairs. That is the better representation here regardless: it encodes (an edge
list with a cycle serialises; a pointer graph with one cannot), it **diffs** —
which is how "did the ETL change?" is answered — and it maps one-to-one onto
rows when the catalog is persisted for NLQ retrieval. `PLAT-RECIDX` made
records hash-indexed above a small field count, so keyed lookup at 10,000
columns is O(1).

**An estate is several databases, not one.** One organisation commonly runs
Postgres, SQL Server and MySQL at once, and lineage crosses them. `source` is
therefore part of every identity from the first line of code, not retrofitted.

## 3a. The surface

| Call | What it does |
| --- | --- |
| `discovery.scan(connection, {source, catalog, schema, table})` | read one source's declared facts into a catalog. `source` is required |
| `discovery.estate(catalogs)` | merge several sources into one estate; two catalogs claiming one source name are refused |
| `discovery.columns_of(catalog, table_id)` | that table's columns, in **ordinal** order |
| `discovery.is_primary_key(catalog, column_id)` | is this column part of its table's declared primary key |
| `discovery.id_of(parts)` | the stable id for a table or column |
| `discovery.modules(connection, {source})` | views and procedures, **with their owning schema** — per-dialect, chosen from `odbc.info` |
| `discovery.references(sql)` | `{reads, writes, gaps}` — the objects a statement touches |
| `discovery.trace(catalog, modules, dbms)` | `{edges, unresolved, gaps}` — references bound to real objects |
| `discovery.impact(trace, object_id, "downstream"\|"upstream")` | what a change to that object affects |
| `discovery.projection(sql)` | each output column, the expression, the columns it reads, whether it aggregates |
| `discovery.predicates(sql)` | `where` / `having` / join `on` — which **are** the lineage, not metadata about it |
| `discovery.explain(a, b)` | why two same-named columns disagree |
| `discovery.statements(sql)` | a body split into statements: `{kind, target, sql}` each |
| `discovery.derivations(sql)` | per statement, each **output column paired with the expression that fills it** |
| `discovery.lineage(catalog, modules, dbms, column_id, options)` | `{steps, origins, unresolved, gaps}` — where **this column** came from, across hops |

Named `scan` and not `read`: `read` is a built-in, and a library function
sharing a built-in's name resolves to itself inside the library and to the
built-in outside it — legal, and noted by the interpreter on every load. A name
that has to be explained at each call site is the wrong name.

An identifier containing the key separator (`.`) is **refused where it is
read**, rather than escaped — the rule `dbframe` already follows. Escaping an
identifier is a decision about a quoting dialect; refusing one is a fact.

### 3b. A procedure body is not one statement

Everything above `statements` reads one statement, which is all a view ever is.
The ETL lives in procedures, and there the interesting facts are per statement:
`truncate table x; insert into x select ... from y` writes `x` twice for two
different reasons, and a function that found "the first select" would describe
the second and silently drop the first.

**Splitting is not a solved problem and this does not pretend it is.** T-SQL
makes the semicolon optional, so a body may be several statements with nothing
between them, and deciding where one ends needs a grammar this library does not
have. What it has instead is a rule about verbs: `insert`, `update`, `delete`,
`merge` and the rest start a statement when they appear at bracket depth zero —
with four exceptions, **each of which was a wrong answer before it was an
exception**:

1. `... then insert` — a MERGE arm, and a T-SQL `if ... then`, put a verb where
   it begins nothing.
2. A MERGE owns every arm it declares. Missing this produced a statement whose
   target was `set` — a phantom table, reported with the confidence of a real
   one.
3. The `select` that **feeds** a write belongs to that write. Treating it as a
   second statement loses the pairing that makes column lineage possible at all.
   A set operation is likewise one statement with several selects in it.
4. A CTE belongs to the statement it feeds. Split off, `with c as (...) insert
   into t select x from c` leaves an insert that reads a table called `c`.

Stated limits, rather than papered over: a conditional's **condition is not
attached** to the statements inside it, so `if @x = 1 insert into a ...` reports
the write without the condition — the edge is real and the circumstance is lost.
And a body whose statements are separated by neither a semicolon nor a leading
verb cannot be split at all.

### 3c. Pairing an output column with what fills it

`projection` reads a select list, which names its own outputs. An INSERT does
not: the names are on one side of the statement and the expressions on the
other, and **pairing them is the whole difficulty**. It is positional, and the
two shapes that cannot be paired are reported rather than guessed:

- **A count mismatch** — the column list names three and the source produces two.
- **No column list at all** — which column each expression fills is decided by
  the target's ordinal order, which is a fact about the **catalog**, not about
  the statement. `lineage`, which has a catalog, settles it by the same rule
  the database itself applies; `derivations`, which does not, says so. If the
  counts disagree the statement is not valid SQL and the refusal stands.

Each derivation records **how** the pairing was made — `positional`,
`assignment`, `select`, `ordinal`, `refused` — as a field rather than as prose
a caller would have to sniff for. `ordinal` is the one that matters: it means
the output names in `columns` are the *select list's own* inferred ones, which
are meaningless as target column names. Inside `lineage` an `ordinal` pairing
that the catalog has discharged becomes `ordinal_settled`, so "the catalog
answered this" and "the statement answered this" stay distinguishable.

Both produce a perfectly ordinary set of expressions **with the wrong names
attached** if paired anyway, and a column lineage built on that is confident and
wrong in a way nothing downstream can detect.

### 3d. Column lineage, and why the object level is not enough

`trace` and `impact` answer at the level of **objects**: this procedure reads
that table. That is the level a dependency graph is usually drawn at, and it is
one level too coarse for the question people actually ask. "Which table does
this report read" has an easy answer and rarely settles anything; "where did
**this column** come from" is the one that costs a day.

The difference is not cosmetic. `fact_volume` reads `stg_deal`, and that says
nothing about whether `avail_after_pvr` came from `gross_vol_mmbtu`, from
`pvr_pct`, from both, or **from a constant** — and a column that turns out to be
a constant is one of the commonest reasons a number is wrong and nobody can see
why. So a column with a writer and no sources is reported as a **step with an
empty source list**, which is a different fact from having no writer at all.

Three things it will not do:

- **It will not guess.** A source column that could belong to two of the objects
  a statement reads is reported `unresolved`, by name. The wrong attribution is
  indistinguishable from the right one downstream, and picking one turns a known
  unknown into an unknown wrong answer.
- **It will not assume one writer per column.** A restatement is an ordinary
  thing, and reporting only the first writer hides it.
- **It will not assume the graph is acyclic.** `update t set x = y * 0.97` reads
  the very table it writes.

A **view is included as the writer of itself**: a view has no `insert`, and its
select list is nonetheless the derivation of every column a consumer sees —
which is the whole reason a report's number can be traced past the view it was
read from.

**A target that binds to nothing is still a hop.** `select ... into #tmp` then
`insert ... select ... from #tmp` is how a great deal of real ETL is written,
and a temp table is never in a catalog — so without this the chain dead-ends at
the first one, which in T-SQL is most procedures. Nothing is guessed: the
statement that *fills* the temp table is what says which columns it has, and
the resolution is confined to the module that wrote it. Such an id carries
`::`, which no catalog id ever does, and `intermediates` names every local
object a walk passed through — a caller has to be able to tell a local object
from a real one.

Two defects in the existing reader were found by building this, both invisible
until something tried to *resolve* what it had reported:

- **A numeric literal was read as a column name.** Digits are legal inside an
  identifier, so `0.97` arrived as the three tokens `0` `.` `97` and looked
  exactly like a qualified column; `_sources_in` had been reporting `1` as a
  source of `x * (1 - y)` since it was written. Harmless until it became an
  entry in the `unresolved` list — which is the one field a caller has to be
  able to trust.
- **`cast(x as int)` truncated the expression** and named the output column
  `int`. Only a depth-zero `as` is an alias. With an alias present the last
  `as` wins and the answer comes out right *by accident*, which is why the
  first test written for it passed on the broken reader.

An UPDATE and a MERGE now report their target as a **read as well as a write**:
`set x = y * 0.97 where status = 'A'` takes both `y` and `status` from the
target itself, and reported as a write alone a restatement looks like a module
with no inputs. A DELETE is deliberately not in this — it reads the table to
choose rows and puts no value into anything.

## 4. What the catalog cannot answer, and must say so

From the estate requirements (R3b, R21, R28): some facts are **not in the
database at all**. That `tmp_rebate_2019` is load-bearing lives in a nightly
job; which of `customer`, `customer2`, `customer_new` is live lives in the
application. The correct answer is **"unanswerable from the catalog"**, not a
guess — so the value model carries that as an outcome rather than treating
absence of evidence as evidence.

### 3e. Two costs the estateforge session measured (2026-09-11)

**`modules` took no schema filter** where `scan` takes three, so it returned
every routine in the database. Measured on a fresh PostgreSQL 17 holding an
11-table estate: **119 rows, of which 114 were `vector_*`** — pgvector lives in
that machine's `template1`, so every new database inherits it into `public`.
That is not `_is_system_schema` failing; `public` is correctly not a system
schema and an extension legitimately lives there. It meant every caller
re-filtered, and one comparing counts was green or red for reasons unrelated to
what it was testing. `modules` now takes `schema` (a LIKE pattern, **bound**
rather than pasted) and `catalog`, matching `scan`. One pattern, not a list:
"any of these six schemas" is several calls, stated here rather than
discovered. SQLite **refuses** a filter rather than ignoring it — silently
answering a question that was not asked is how a caller comes to trust a
narrower answer than it got.

**A scan fetched every system column and discarded it.** Measured here on SQL
Server 2025: `%` returns **10,006 columns of which 9,927 are `sys` and
`INFORMATION_SCHEMA`**, every one carried over the wire and then dropped by
`_is_system_schema`. estateforge measured 9,927 against a 550-object estate —
the same figure, so it is a fixed cost of the catalog, not of the estate.

**And PostgreSQL pays it too**, which corrects the report that raised it:
psqlODBC filters *tables* server-side and **not columns** — 2,278 of 2,679 here
are system.

Columns are now fetched **one schema at a time**, from the schema list the
*table* scan just produced, so nothing that scan found can be missed by
construction. **The table scan's `%` is untouched**, deliberately: it is the fix
for psqlODBC's search-path defect, which silently returned 17 `public` tables
for a 17-schema estate, and narrowing *that* reintroduces a complete-looking
answer missing most of the database — worse than being slow. Only the second
call narrows, to schemas the first already reported. Same answer, measured
identical before and after on both databases.

The risk is losing a schema rather than being slow, so the single-`%` form is
the **oracle**: every column belonging to a catalogued table must be in the
catalog and nothing else may be. `run_discovery`'s own fixture builds everything
in one schema and so passes on a split that keeps only the first — the check
that bites lives in the estate's live tier, which spans four, with a control
asserting it really does.

**Times are not quoted here.** estateforge's absolute figures are
deployment-confounded — PostgreSQL over a unix socket against SQL Server in a
VM over TCP, a 14x baseline penalty on `select 1` before any query does work —
so the row counts are the part that transfers and the times are not.

## 4a. What is built, and what it was measured against

Table-level lineage, end to end: read the modules, scan their SQL for the
objects they touch, bind those references against the catalog, and walk the
result. Verified against **all four** databases, 46 checks each.

**Column-level lineage** (§3b–§3d) on top of it, live-verified 2026-09-09 on
PostgreSQL 17 and SQL Server 2025 — the two that can carry the estate at all,
since SQLite has no stored procedures and MariaDB has no schemas. SQL Server
runs one check more, because the dynamic-SQL module is declared T-SQL only and
its **gap** is asserted where it exists. The reader itself needs no database and
is tested without one.

The resolution measured earlier is visible in the output on SQL Server:

```
s.alt.p_res  reads  s.gbasic_test.alt.res_probe    ' own schema wins
s.alt.p_fall reads  s.gbasic_test.dbo.fall_probe   ' falls back to dbo
```

Both procedure bodies read `from res_probe` / `from fall_probe` — bare. Nothing
in the text says which table is meant.

**A driver limitation found the same way**, and it had been invisible because
every earlier test happened to pass a filter: FreeTDS refuses `odbc.columns`
with no table pattern (*"sp_columns expects parameter '@table_name'"*), where
the other three honour ODBC's "absent means any". A whole-schema scan was
therefore broken on one database and green on the other three. `scan` now
passes an explicit `"%"`.

**Not yet built, and each is a separate increment:** column-level lineage
(cheaper on MariaDB and PostgreSQL, which return views already resolved), the
predicates that answer §1a's question, and cross-database references.

## 4b. §1a's question, answered

`discovery.explain({name, body}, {name, body})` reports **differences only**,
and says so when there are none. Against the estate's two `total_volume`
columns:

```
shared ancestor: warehouse.fact_volume
why they differ:
  - the expression differs: sum ( gross_vol_mmbtu )   versus   sum ( avail_after_pvr )
  - they are built from different columns: [gross_vol_mmbtu] versus [avail_after_pvr]
  - only the second narrows on -- where: status = 'ACTIVE'
```

The estate independently records: *"rpt_volume_net filters status = 'ACTIVE'
and sums avail_after_pvr; rpt_volume_gross sums gross_vol_mmbtu over every
row."* That prose was written when the estate was designed; the explanation
above is derived from the SQL alone. **Agreement between two independently
written statements is the oracle**, and both fixtures assert it.

Three decisions worth naming:

- **String literals keep their contents.** They are never read as object names
  — the token kind prevents that — but in a predicate the literal *is* the
  explanation: `status = 'ACTIVE'` answers the question and `status = '...'`
  does not.
- **`select *` is reported, never expanded.** Expanding it needs the shape of
  something the function was not given.
- **It reports differences only, and the control is that two identical
  derivations report none.** Inventing a distinction so as to have something to
  say is the failure this design is arranged against, and a tool that always
  finds a reason is indistinguishable from one that guesses.

## 5. Deliberately not in the first increment

- **Inference of any kind.** It needs the null model, and the null model needs
  the estate fixture. Note §1a: view and procedure lineage is *declared*, not
  inferred, and therefore comes BEFORE this — it was originally sequenced after
  inference, which was wrong.
- **NLQ.** It consumes this; it is not part of it.
- **Column lineage through dynamic SQL.** It is a gap and is reported as one.
- **The condition a conditional write sits under** (§3b).
- **Cross-database references.** An estate is several databases; a reference
  from one to another is not yet bound.
- **Writing anything.** Discovery reads.

## 6. How it will be tested

- **Against the four real catalogs already reachable** — SQLite, MariaDB,
  PostgreSQL, SQL Server. Declared facts have a correctness criterion that does
  not need a fixture: create a known schema, read it back, compare.
- **Against the estate fixture, when it exists**, for the questions only ground
  truth can pose — and its null region for the question that matters most,
  which is whether anything is invented where there is nothing.
- The standing caution: a generated schema is regular in ways a real one is
  not, so neither retires the other.
