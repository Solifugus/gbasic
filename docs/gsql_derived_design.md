# G-SQL derived tables and `WITH` (Phase 2, increment 6)

**Status:** designed and built 2026-11-01 (`src/gsql/sql_parse.c`, `sql_plan.c`, `sql_exec.c`; `tests/run_gsql_sql.sh`). The sixth increment of `gsql_design.md` §11 Phase 2. It follows subqueries in expressions (`gsql_subquery_design.md`), which left these two as its §9.

## 1. What it is

```sql
with big as (select customer, sum(total) as spent from orders group by customer having sum(total) > 100)
select c.name, b.spent
from customer c join big b on b.customer = c.id
where c.region in (select region from target)
```

- **A derived table** is a subquery in `FROM` or in a `JOIN`: `(select ...) [AS] d [(a, b, ...)]`. It must have an alias, as standard SQL requires, because nothing else could name its columns.
- **`WITH name [(a, b, ...)] AS (select ...), ...`** goes before a `SELECT`, at the top of a statement or of a subquery. Each name can then be used in `FROM` and `JOIN` like a table, in the main query, in its subqueries, and in later `WITH` queries.
- **A column list** renames the outputs in order. It must name exactly as many columns as the query has.
- **Its columns** are the query's output names and kinds. A name that two outputs share, such as two unaliased `count(*)`, can be read only once it is renamed; reading it is refused as ambiguous.
- Everything a table supports in a `SELECT` works over one: `WHERE`, joins, grouping, `DISTINCT`, set operations, `ORDER BY`, `*` and `d.*`.

**It is not:**

- `WITH RECURSIVE`.
- `LATERAL`: a derived table cannot read the tables before it in the same `FROM`.
- `WITH` before `INSERT`, `UPDATE` or `DELETE`, or a derived table in one.
- An index on a derived table's rows. It is always read whole.

Each of these is refused by name.

## 2. Semantics, which are SQL's

- A derived table is the rows its query answers. Their order is SQL's: none, unless the outer query has its own `ORDER BY`.
- **A `WITH` name shadows a table of the same name** within the statement, nearest definition first.
- **A `WITH` query reads only tables and earlier `WITH` queries.** It may not read an enclosing query's columns, so its rows are the same wherever it is used. A derived table may read an enclosing query's columns when it sits inside a subquery: it is then correlated with that subquery, and recomputed with it.
- **Each use of a `WITH` name is read from the same plan**, materialized at most once per execution of the query that reads it. Reading it twice gives the same rows twice, because a G-SQL statement reads one snapshot.

## 3. Binding

**A scan gains a second kind of source.** A scan is a table read from storage or, now, a plan whose output rows are its rows. Every place the binder asks a table about its columns (names, kinds, count, which column has fields) goes through four helpers. Those answer from the table layer for a table, and from the plan's outputs for a derived scan. The table layer itself is untouched.

- **A derived table's** plan is bound with the same enclosing binder as the query it is in, as a compound's sides are, so its outer references count levels the same way.
- **A `WITH` query** is bound once, where it is defined, with no enclosing binder at all. A name it cannot find is refused with a sentence saying a `WITH` query reads only tables and earlier `WITH` queries.
- **A table name in `FROM`** is looked for among the `WITH` names in reach, innermost first, then in the catalog.

## 4. Running it

A derived scan is read by materializing its plan's rows the first time the scan is opened in an execution, through the child cursor a subquery already uses, and walking them. Because a derived table cannot read the tables before it, its rows do not change between openings in one execution. So a derived table on the inner side of a join is computed once, not once per outer row. A column is read by position out of the materialized output row.

**The cost is stated:** the rows are held in memory. Spill-to-disk is a later Phase 2 item, and a derived table is one of the places it will be needed.

**`EXPLAIN`:**

```
Nested Loop
  Scan on customer AS c
  Derived Scan on b, CTE big
Filter ...
CTE big
  Scan on orders
    Aggregate: group by customer
    Having: (sum(total) > 100)
```

## 5. How it is tested

`tests/run_gsql_sql.sh`:

- **parse:** each form printed back canonically, and each refusal of §1 at its position.
- **semantics:** hand-written answers. They cover:
  - a derived table over a grouped query, joined;
  - a column list;
  - an ambiguous duplicate name, and its rename;
  - `*` and `d.*`;
  - a `WITH` used twice, and one used by a later `WITH`;
  - a `WITH` name shadowing a table;
  - a `WITH` inside a subquery;
  - a derived table inside a correlated subquery, reading its outer row;
  - a `WITH` query reading an outer column, refused;
  - a missing alias, refused;
  - a column-list count mismatch, refused;
  - `EXPLAIN`.
- **differential and oracle:** a random predicate over `r`, written as a derived table `(select * from r where P) d`, joined to `q` and grouped. It is checked against the same query written directly against `r`. Both are run with access paths and forced to scans. Rewriting a query through a derived table must not change its answer. The direct query's correctness is established by the tiers before it.

## 6. What building it found

- **A subquery's cursor must know its enclosing row before it starts.** The first version opened a derived table's cursor and then set its outer pointer. But starting a cursor already reads rows: a grouped query folds every row when it starts. So a grouped derived table inside a correlated subquery would have read the wrong enclosing row. The outer pointer is now an argument to the cursor's opening.
- **The printer needs the scan helpers, and the parse tier links the parser alone.** The helpers live in `sql_parse.c` for that reason, beside the printer that names bound columns.
- **The random tier passed first time:** five seeds of 300 rewrites each, every one equal to the direct query, with 32 to 52 answered with rows in each form.
- **Nine perturbations, all red**, each caught by the hand-written case for it:
  - materializing again on every opening, so rows accumulate;
  - a derived table reading its own query as its enclosing one;
  - a column list ignored;
  - a `WITH` query bound inside its statement;
  - a duplicate name read as its first;
  - fields refused on a derived column;
  - every column read from position 0;
  - the first row skipped, which the random tier also caught;
  - `WITH` names never consulted.
