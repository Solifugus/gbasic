# G-SQL joins (Phase 2, increment 2)

**Status:** designed 2026-10-31. The second increment of `gsql_design.md` §11 Phase 2, after aggregates (`gsql_aggregate_design.md`).

## 1. What it is

```sql
select c.name, o.placed, o.total
from customer c
join orders o on o.customer = c.id
left join refund r on r.order_id = o.id
where c.region = :region and r.order_id is null
order by o.placed
```

- **`[INNER] JOIN … ON`**, **`LEFT [OUTER] JOIN … ON`**, **`CROSS JOIN`**, and the comma form `FROM a, b`, which is a cross join.
- Any number of tables. A table may appear more than once under different aliases (`from emp e join emp m on e.manager = m.id`).
- **Names:** `t.col` names a column of one table. A bare `col` is accepted only where exactly one table has it; otherwise it is refused, naming the tables that do. `*` is every column of every table, in `FROM` order; `t.*` is one table's.
- Everything the single-table `SELECT` has still works over a join: `WHERE`, `GROUP BY`, `HAVING`, aggregates, `ORDER BY`, `LIMIT`, `OFFSET` and `EXPLAIN`.

**It is not** `RIGHT` or `FULL` joins, `USING` or `NATURAL`, joins in `UPDATE` or `DELETE`, or reordering. A `RIGHT JOIN` is refused, with the remedy of swapping the tables into a `LEFT JOIN`. `FULL JOIN` is refused as not in this release.

## 2. Semantics, which are SQL's

- **An inner join** keeps each combination of rows whose `ON` condition is **true**. `NULL` is not equal to `NULL`, so rows with an unknown join key never match, by gBASIC's three-valued `=` (`unknown_nan_design.md`).
- **A left join** keeps every row of its left side. A left row that matches no right row appears once, with every column of the right table `NULL`.
- **`ON` and `WHERE` differ only for a left join,** and that difference is the reason both exist. `ON` decides which right rows match. `WHERE` filters the joined rows afterwards, so `left join r on … where r.x is null` finds the left rows with no match. Moving a condition from one to the other changes the answer, and the tests assert both placements.
- **No implicit casts** (§9): `on o.customer = c.name`, a number against text, is refused as `=` refuses it anywhere.

## 3. How it runs: nested loops, in the order written

The tables are joined **left to right, in `FROM` order**, by nested loops. For each row of the tables so far, the next table is read and each of its rows is tried.

**The inner table is read through an access path, and that is what makes the loop affordable.** When table *k* is read, every column of tables before it already has a value, so a condition like `o.customer = c.id` is, from table *k*'s point of view, `column = constant`. The planner therefore chooses table *k*'s path exactly as it chooses one for a single table (`gsql_sql_design.md` §5). It treats columns of earlier tables as constants, and evaluates the bound afresh for each outer row. An index on `orders(customer)` turns the loop into one seek per customer instead of a scan of every order per customer. The single-table rule carries over too: **the seek only narrows, and every condition is still checked on every row.**

**Which conditions may narrow table *k*:** its own `ON` conditions, and, when its join is inner, the `WHERE` conjuncts whose last table is *k*. For a left join, `WHERE` conditions on the right table must not narrow it, because narrowing would remove the row that null-extension then puts back.

**Where each `WHERE` conjunct is checked:** at the first table by which every table it reads has a row. That is as early as possible for an inner join. For a left join it means after the null-extended row exists, which is what §2 requires.

**The order is not chosen.** Reordering joins needs statistics and a cost model, which are later Phase 2 items (`gsql_design.md` §11). Until then `EXPLAIN` shows the order a query will use, and the author can change it by rewriting the `FROM`. A planner that reorders later changes no answer, only the cost.

**`EXPLAIN`** shows one access line per table, nested:

```
Nested Loop
  Seek on customer AS c using the primary key ...
    Filter: (c.region = :region)
  Index Seek on orders AS o using orders_by_customer (customer = c.id)
  Left: Index Seek on refund AS r using refund_by_order (order_id = o.id)
  Filter: (r.order_id IS NULL)
```

## 4. Building it

The planner and executor were written for one table, about 120 places. They are refactored first, with no change in behaviour: each table becomes a **scan** (its table, alias, access path, and the conditions it checks), and the single-table case is one scan. Every existing tier must stay green, and the differential tier's answers must not move. Joins are then a list of scans.

## 5. How it is tested

`tests/run_gsql_sql.sh`:

- **parse:** each join form printed back canonically, and the refusals (`RIGHT`, `FULL`, `USING`) with their positions.
- **semantics:** hand-written answers. They cover an inner join, a left join with unmatched rows, the `ON`-against-`WHERE` pair, a self-join, a three-table join, a cross join, `NULL` join keys matching nothing, ambiguous and unknown names, `*` and `t.*` over a join, and aggregates over a join.
- **differential:** random two-table joins, both inner and left, on random keys, with random predicates on each side. Each query runs two ways: with access paths (so the inner side seeks per outer row) and forced to scans. The answers must agree. A census floor requires that the inner side really sought.
- **oracle:** the same joins computed by the test from each table fetched alone, matched pairwise with the host's `=` and null-extended by hand. It shares no join code with the engine.
