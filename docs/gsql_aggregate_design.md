# G-SQL aggregates: `GROUP BY`, `HAVING` and five aggregate functions (Phase 2, increment 1)

**Status:** designed and built 2026-10-30 (`src/gsql/sql_parse.c`, `sql_plan.c`, `sql_exec.c`; `tests/run_gsql_sql.sh`). The first increment of `gsql_design.md` §11 Phase 2. It comes first because it is what the first program written against the `sql` blocks reached for (`select count(*) from item`), and because every later Phase 2 item (joins, unions, views) produces rows that people then group.

## 1. What it is

```sql
select region, count(*) as orders, sum(total) as revenue, avg(total) as typical
from orders
where placed >= :since
group by region
having count(*) > 10
order by revenue desc
```

- **`count(*)`, `count(e)`, `sum(e)`, `avg(e)`, `min(e)`, `max(e)`**, each also with `DISTINCT` (`count(distinct customer)`).
- **`GROUP BY e, …`**: any expressions, not only columns.
- **`HAVING e`**: keeps a group whose condition is **true**, as `WHERE` keeps a row.
- An aggregate with no `GROUP BY` makes the whole input **one group**, so `select count(*) from t` answers one row even when `t` is empty.

**It is not** `stddev`/`variance` (still refused by name), `FILTER (WHERE …)`, `GROUPING SETS`/`ROLLUP`/`CUBE`, window functions, `SELECT DISTINCT` (a separate item, `gsql_distinct_design.md`), or spilling to disk.

## 2. Every aggregate is gBASIC's arithmetic, applied by the host

The rule of `gsql_sql_design.md` §2 holds unchanged. An aggregate is SQL's (it decides which values are combined and what an empty set answers), and the combining is gBASIC's operator, through the host:

| aggregate | combines with | over no values | over values |
|---|---|---|---|
| `count(*)` | — | `0` | the rows |
| `count(e)` | — | `0` | the values that are not `NULL` |
| `sum(e)` | gBASIC's `+` | `NULL` | so exact integers stay exact, money keeps its guard digits and **refuses to add two currencies** (`EXPRESSION`, gBASIC's sentence), and an overflow warns (2112) exactly as in a program |
| `avg(e)` | the sum, then gBASIC's `/` by the exact count | `NULL` | so `avg` of `1, 2` is `1.5` (there is no integer division) and `avg` of money is money |
| `min(e)`, `max(e)` | gBASIC's order | `NULL` | the least or greatest, by the order an index uses |

**`NULL` is skipped by every aggregate but `count(*)`**, the standard rule. **No implicit casts:** `sum` over a text column, or `min` over a column holding both a number and text, is refused by the same check `+` and `<` already make (§9). `count` answers an exact integer.

## 3. What a group is: the index key's equality

Two rows are in one group when their `GROUP BY` values are **the same value**, and "the same value" is decided by **the index key encoding** (`gsql_storage_design.md` §3.2): each row's group values are encoded as one composite key, and equal keys are one group. That is the equality a `UNIQUE` index already uses, so grouping and uniqueness cannot disagree about whether two values are one. It gives SQL's answers where SQL is specific: **all `NULL`s are one group** (grouping treats NULLs as not distinct, unlike `=`), `3` and `3.0` are one group, and text is compared byte for byte, case and all.

**What has no key cannot be grouped by:** a duration, an array, a record. Grouping by one is refused with the reason, as indexing one is.

**`DISTINCT` inside an aggregate uses the same equality**: `count(distinct e)` counts distinct keys.

**Groups come out in key order** when there is no `ORDER BY`. SQL leaves that order unspecified, so a program must not rely on it; giving one anyway costs a sort of the groups and makes every answer reproducible, which the tests need and a person reading output wants.

## 4. What the select list may say

In a grouped query, a column may be read **only inside an aggregate, or as part of an expression that is one of the `GROUP BY` expressions**. Two expressions are the same when their canonical print-back is (the parser's `sql_print_expr`, after binding). So `group by lower(name)` allows `lower(name)` in the select list and refuses `name`, naming the rule and the column. This is PostgreSQL's rule without its one relaxation (a column functionally dependent on a grouped primary key), which can come later and needs nothing undone.

The same rule binds `HAVING` and `ORDER BY`. `ORDER BY` may also name an output by position or alias, as it already can.

**Refused:**

- an aggregate in `WHERE` (that is what `HAVING` is for, and the message says so);
- an aggregate inside an aggregate;
- an aggregate in `GROUP BY`;
- `HAVING` that is not a boolean.

## 5. How it runs

Aggregation is a stage between the access path and the output:

1. The access path yields the rows `WHERE` keeps, exactly as today. So an index still narrows a grouped query, and `EXPLAIN` still says how.
2. Each row's `GROUP BY` values are encoded as a key and looked up in a hash table of groups. A new group keeps **the first row it saw**, so the group's values can be read back from it. Every row of a group has the same group values, by §3, so the first is as good as any.
3. Each aggregate in the statement folds the row into its group's accumulator.
4. When the input ends, the groups are sorted by key, and each becomes one row of output: its first row is made the current row, and each aggregate is replaced by its group's final value. The select list, `HAVING` and `ORDER BY` are then evaluated by the existing code, which reads columns from the current row as it always has.

**It is in memory**, like an unindexed `ORDER BY` today: every group is held until the input ends. Spilling is Phase 2's later item (§5.1).

**`EXPLAIN`** adds a line naming the grouping, and `HAVING` as its own filter:

```
Index Seek on orders using orders_by_placed (placed >= :since)
  Aggregate: group by region
  Having: count(*) > 10
  Order: revenue DESC (sorted)
```

## 6. How it is tested

`tests/run_gsql_sql.sh`, the SQL layer's own suite:

- **parse:** the new clauses printed back in canonical form, and the refusals with their positions.
- **semantics:** hand-written answers. These cover each aggregate over empty, all-`NULL` and mixed input, `sum` of money in one currency and refused across two, exact integers past 2^53, `avg` producing a fraction, `NULL`s forming one group, `3` and `3.0` forming one group, `DISTINCT`, `HAVING`, ordering by an aggregate, and each refusal beside its legal neighbour.
- **differential (oracle):** random rows and random `GROUP BY` queries, each answered both by the engine and by an aggregation **written separately in the test**. That oracle groups by sorting and compares through the same host, so a hash table that merged two groups, or split one, is caught by an implementation that shares none of its code. The queries are run with access paths and forced to a scan, as the existing tier does.

**Thirteen perturbations proven red, and which tier caught each is worth knowing.** The oracle caught seven: groups merged, groups split, NULLs counted, `avg` without its division, `HAVING` ignored, `min` and `max` swapped, and a sum that subtracts. The other six were caught by the hand-written cases alone, because the oracle does not reach them: `DISTINCT` ignored, no group for an empty input, the column rule switched off, an aggregate allowed in `WHERE`, a group keeping its last row instead of its first (visible only as `3` against `3.0`), and groups left in arrival order. Neither tier alone would do.

## 7. Not done yet

- `stddev`, `variance` and the other statistical aggregates: `gsql_design.md` §7 puts them with the incremental-aggregate contract, which is Phase 3.
- ~~`SELECT DISTINCT`~~: built 2026-10-31 (`gsql_distinct_design.md`). It is applied to the output rows, not rewritten into `GROUP BY` as this line first said, because over a grouped query the outputs are what must be distinct.
- A column functionally dependent on a grouped primary key.
- Spilling a large grouping to disk.
