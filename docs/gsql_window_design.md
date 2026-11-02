# G-SQL: window functions

**Status:** Shipped 2026-11-02, built the day Matthew said "go ahead". The first of the three features the gBASIC books' volume 3 needs before its pinned release (`gbasic-books/docs/volume-3-enterprise-integration-and-automation.md` §5.5): keeping the latest record per key, change detection with `lag`, running totals and rankings.

## 1. What is written

```sql
SELECT id, account, posted, amount,
       sum(amount) OVER (PARTITION BY account ORDER BY posted) AS balance,
       row_number() OVER (PARTITION BY account ORDER BY posted DESC) AS newest
FROM ledger
```

A call followed by `OVER (...)` is a window function. Inside the parentheses, each part optional and in this order:

- `PARTITION BY e, ...` splits the rows into partitions, keyed as `GROUP BY` keys a group (so `3` and `3.0` are one partition, and NULLs are one).
- `ORDER BY e [DESC], ...` orders each partition, in gBASIC's order with the kind check `<` makes, NULL last ascending and first descending, as the statement's own `ORDER BY` sorts.
- A frame, `ROWS` or `RANGE`, then `BETWEEN start AND end` or a start alone, which ends at `CURRENT ROW`. A bound is `UNBOUNDED PRECEDING`, `n PRECEDING`, `CURRENT ROW`, `n FOLLOWING` or `UNBOUNDED FOLLOWING`. `n` is a constant (a literal or a parameter): a whole number, zero or more.

`OVER`, `PARTITION`, `ROWS`, `RANGE`, `PRECEDING`, `FOLLOWING`, `UNBOUNDED` and `CURRENT` are not reserved: each is a keyword only where it stands, so no existing column name stops working.

## 2. The functions

| function | answers |
|---|---|
| `row_number()` | 1, 2, 3 ... in the partition's order; ties in the order the rows were read |
| `rank()` | 1 + the rows before the row's peers (gaps after ties) |
| `dense_rank()` | 1 + the peer groups before it (no gaps) |
| `percent_rank()` | (rank − 1) / (rows − 1), 0 for a one-row partition |
| `cume_dist()` | (rows up to and including its last peer) / rows |
| `ntile(n)` | which of `n` near-equal buckets it is in, the larger buckets first |
| `lag(e [, k [, d]])`, `lead(...)` | `e` on the row `k` before or after it in the partition (default 1); `d` (default NULL) where there is none |
| `first_value(e)`, `last_value(e)`, `nth_value(e, k)` | `e` on the frame's first, last or `k`-th row; NULL when the frame has none |
| `count`, `sum`, `avg`, `min`, `max` | the aggregate over the frame, folded exactly as the aggregate folds a group: the same kind checks, money stays money, NULLs skipped |

*Peers* are the rows of a partition equal on every term of the window's `ORDER BY`; with no `ORDER BY`, every row of the partition is a peer of every other.

**The frame.** With none written the frame is `RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW`: from the partition's start to the current row's last peer. So `sum(x) OVER (ORDER BY d)` is a running total in which rows of the same `d` share one answer, and with no `ORDER BY` the frame is the whole partition. `ROWS` counts rows; `RANGE` counts peer groups, and only `UNBOUNDED` and `CURRENT ROW` bounds are taken with it.

A frame reaches only the aggregates and `first_value`, `last_value` and `nth_value`. The others read the partition's order and nothing else, so a frame written on one of them is refused, naming which functions it does reach.

## 3. Where windows stand

Windows are computed after `WHERE`, `GROUP BY` and `HAVING`, and before `DISTINCT`, the statement's `ORDER BY` and `LIMIT`, as SQL places them. So:

- a window function may stand in the select list and in `ORDER BY`, and nowhere else. `WHERE`, `GROUP BY`, `HAVING`, `ON`, an aggregate's argument and another window are each refused, the `WHERE` refusal naming the way to filter on one: select it in a derived table and filter outside. That is the latest-per-key idiom:

  ```sql
  SELECT * FROM (SELECT o.*, row_number() OVER (PARTITION BY cust ORDER BY placed DESC) AS rn
                 FROM orders o) AS t
  WHERE rn = 1
  ```

- in a grouped query a window sees the groups, so `sum(sum(amount)) OVER (ORDER BY month)` is a running total of monthly totals. Its arguments and its `PARTITION BY` and `ORDER BY` follow the grouped query's rule: a column only inside an aggregate or as a grouped expression.
- a windowed query with no `ORDER BY` of its own answers its rows in the order it would have without the window.

## 4. Refused, each saying what to write instead

| written | refusal |
|---|---|
| `OVER w` / `WINDOW w AS (...)` | named windows are not in this release; write the window in each `OVER` |
| `ORDER BY 2` inside `OVER` | it sorts by the number 2, not output column 2: name the column |
| `RANGE 3 PRECEDING` | `RANGE` takes only `UNBOUNDED` and `CURRENT ROW` bounds in this release; `ROWS` counts rows |
| `GROUPS`, `EXCLUDE`, `FILTER (WHERE ...)`, `count(DISTINCT x) OVER` | not in this release |
| a frame starting after it ends, or starting at `UNBOUNDED FOLLOWING` | refused, naming the bound |
| `n PRECEDING` with `n` negative, fractional or NULL | refused when the statement runs, as `LIMIT` is |

## 5. How it runs

After the source (the join, or the groups once `HAVING` has kept them), a windowed query **collects** its rows: each row's state, which is every scan's row bytes or, grouped, its group, together with the values the windows need: each window's partition key, its `ORDER BY` values, and each function's arguments. Windows that write the same `PARTITION BY` and `ORDER BY` share one **ordering**. For each ordering, the row numbers are sorted, stably, by partition key and then the `ORDER BY` values, and each function's answer is worked out partition by partition into the row's slot.

A windowed query never reads its table in the statement's `ORDER BY` order to save the final sort, because a window with no `ORDER BY` of its own would then see its partition in that order (measured: under `ORDER BY id DESC`, `last_value` answered each partition's first row). It is sorted after its windows instead.

Then the rows are replayed in the order collected: each row's state restored, as a group's is, with its window answers where `eval` finds them, so the select list, `DISTINCT` and the statement's `ORDER BY` run over them unchanged.

**Cost.** An aggregate whose frame starts at the partition's start is a running fold, and one whose frame ends at the partition's end is folded from the end backwards, so either is one pass. A frame bounded at both ends (`ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING`) is folded afresh for each row, which costs the frame's width per row. The rows are held in memory up to the operator budget and spill to disk past it (`gsql_operator_spill_design.md` §11, shipped the same day). The collection is sliced (`gsql_slicing_design.md`); in this first increment, the computing that follows it runs in one piece.

## 6. Tests

- **parse:** every form printed back canonically, and each refusal at its position. A view's text is that canonical print, so a window survives `CREATE VIEW`.
- **semantics:** hand-written answers in `gen_sql_cases.py` for each function, frame and placement, ties and NULLs, money, a grouped window, the latest-per-key idiom, and each refusal.
- **differential:** random rows (whole numbers, with repeats and NULLs) and random windows, every answer checked against SQLite's window functions run by python3 on the same rows, with `NULLS LAST` written so that SQLite orders as G-SQL does. SQLite is the oracle because it is an independent implementation. A census floor makes each function, each frame kind and partitioning appear, so a generator that drifted to trivial windows cannot pass. Skips, saying so, without python3's sqlite3 or a SQLite older than 3.30.
