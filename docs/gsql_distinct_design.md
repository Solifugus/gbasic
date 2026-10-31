# G-SQL `SELECT DISTINCT` (Phase 2, increment 3)

**Status:** designed and built 2026-10-31 (`src/gsql/sql_parse.c`, `sql_plan.c`, `sql_exec.c`; `tests/run_gsql_sql.sh`). `gsql_aggregate_design.md` §7 named it as the short step after aggregates.

## 1. What it is

```sql
select distinct region, status from orders order by region
```

`SELECT DISTINCT` keeps one of each set of output rows that are the same. `SELECT ALL` is accepted and means what a bare `SELECT` means. **It is not** `DISTINCT ON (…)`, PostgreSQL's extension, which is refused as not in this release.

## 2. What "the same" means: the index key's equality, again

Two output rows are the same when every column is the same value, and "the same value" is the one `GROUP BY`, `count(DISTINCT …)` and `UNIQUE` already use: the index key encoding (`gsql_storage_design.md` §3.2). So all `NULL`s are one value, `3` and `3.0` are one value, and text compares byte for byte. Four places in G-SQL ask whether two values are one, and all four answer it the same way. A column whose kind has no key (a duration, an array, a record) cannot be made distinct, and the refusal says so, as it does for grouping.

## 3. Where it sits: on the output rows

The deduplication is applied to the **rows the select list produces**, after `WHERE`, grouping and `HAVING`, and before `LIMIT` and `OFFSET`. That is SQL's order of evaluation, and it is why this is not a rewrite into `GROUP BY` (which looks simpler). A rewrite gives the right answer only for an ungrouped query: `select distinct count(*) from t group by k` asks for the distinct group sizes, and no `GROUP BY` of the original rows says that. Applying it to the output covers grouped and ungrouped queries, `*`, and joins with one rule, at the cost of one more set of keys held in memory, as grouping already holds its groups.

**`LIMIT` and `OFFSET` count distinct rows.** `select distinct k from t limit 2` answers two different values, not the distinct values among the first two rows.

**Without `ORDER BY`, a row comes out where its first copy did**, in the order the access path read them. SQL leaves that order unspecified and a program must not rely on it; it is still reproducible, which the tests need.

## 4. `ORDER BY` names an output

With `DISTINCT`, an `ORDER BY` term must be one of the output columns: by position, by alias, or as an expression whose canonical print is an output's. `select distinct a from t order by b` is refused, because each `a` stands for many rows with different `b`s and there is no one `b` to sort it by. This is PostgreSQL's rule, and its message names the term.

## 5. `EXPLAIN`

A `Distinct` line, after the grouping and before the order:

```
Scan on orders
  Distinct: region, status
  Order: region (sorted)
```

## 6. How it is tested

`tests/run_gsql_sql.sh`:

- **parse:** `DISTINCT` and `ALL` printed back canonically, and `DISTINCT ON` refused at its position.
- **semantics:** hand-written answers covering duplicates removed, `NULL`s as one value, `3` and `3.0` as one value, several columns, `*`, a join, a grouped query, `LIMIT`/`OFFSET` counting distinct rows, `ORDER BY` by position, alias and expression, the refusal of an `ORDER BY` that is not an output, and a key-less kind refused.
- **differential (oracle):** random `SELECT DISTINCT` queries answered by the engine, and by the test fetching the same query without `DISTINCT` and removing duplicates itself, comparing values through the host's equality. It shares no key or hash code with the engine. Both run in one access mode, the lesson from aggregates.

## 7. What it found

Seven perturbations, all red: no deduplication, a key over the first column only, `OFFSET` counted before deduplication, the sorted path or the grouped path skipping it, the `ORDER BY` rule removed, and the parser ignoring the word. The oracle caught five. The grouped path and the `ORDER BY` rule were caught by the hand-written cases alone.

**Adding the oracle found a defect in the test harness, not the engine.** The new tier draws random numbers before the join tier does, so the join tier saw different data, and some of its joins now answered more than 4096 rows. The harness's row renderer read at most 4096 and rendered a larger batch as `(none)`, so those joins failed against their oracle. HEAD's library failed the same way with the same stream. The renderer reads any number of rows now. The worse half is what it could have hidden: two large answers compared with access paths and forced to a scan both rendered as `(none)`, and agreed whatever they held.
