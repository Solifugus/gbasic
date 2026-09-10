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

Named `scan` and not `read`: `read` is a built-in, and a library function
sharing a built-in's name resolves to itself inside the library and to the
built-in outside it — legal, and noted by the interpreter on every load. A name
that has to be explained at each call site is the wrong name.

An identifier containing the key separator (`.`) is **refused where it is
read**, rather than escaped — the rule `dbframe` already follows. Escaping an
identifier is a decision about a quoting dialect; refusing one is a fact.

## 4. What the catalog cannot answer, and must say so

From the estate requirements (R3b, R21, R28): some facts are **not in the
database at all**. That `tmp_rebate_2019` is load-bearing lives in a nightly
job; which of `customer`, `customer2`, `customer_new` is live lives in the
application. The correct answer is **"unanswerable from the catalog"**, not a
guess — so the value model carries that as an outcome rather than treating
absence of evidence as evidence.

## 4a. What is built, and what it was measured against

Table-level lineage, end to end: read the modules, scan their SQL for the
objects they touch, bind those references against the catalog, and walk the
result. Verified against **all four** databases, 46 checks each.

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

## 5. Deliberately not in the first increment

- **Inference of any kind.** It needs the null model, and the null model needs
  the estate fixture. Note §1a: view and procedure lineage is *declared*, not
  inferred, and therefore comes BEFORE this — it was originally sequenced after
  inference, which was wrong.
- **NLQ.** It consumes this; it is not part of it.
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
