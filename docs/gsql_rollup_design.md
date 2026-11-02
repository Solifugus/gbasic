# G-SQL: `ROLLUP`, `CUBE`, `GROUPING SETS` and `GROUPING()`

**Status:** Shipped 2026-11-02. The third of the features the gBASIC books' volume 3 needs before its pinned release (`gbasic-books/docs/volume-3-enterprise-integration-and-automation.md` §5.5): report subtotals for chapter 17. It was ruled in with the other two, so it is built after them.

## 1. What is written

```sql
SELECT region, product, sum(amount) AS total, GROUPING(region, product) AS level
FROM sale
GROUP BY ROLLUP(region, product)
```

answers each (region, product) total, then each region's subtotal with `product` NULL, then the grand total with both NULL. A `GROUP BY` element may be:

| element | grouping sets it stands for |
|---|---|
| `e` | `(e)` in every set |
| `ROLLUP(a, b, c)` | `(a, b, c)`, `(a, b)`, `(a)`, `()` |
| `CUBE(a, b)` | every subset: `(a, b)`, `(a)`, `(b)`, `()`; at most 10 columns, 1 024 sets |
| `GROUPING SETS ((a, b), (a), ())` | the sets listed; an element of the list may be a bare expression |

Several elements combine by their product, as SQL defines it: `GROUP BY year, ROLLUP(region, product)` is `(year, region, product)`, `(year, region)`, `(year)`. `ROLLUP`, `CUBE`, `GROUPING` and `SETS` are not reserved; each is a keyword only where it stands.

## 2. What it answers

Each set is grouped as `GROUP BY` groups, over the same rows: one fold, each row folded into one group per set. In a group of a set that leaves an expression out, that expression reads NULL wherever the query reads it, in the select list, `HAVING` and `ORDER BY`. An aggregate's argument still reads the rows themselves.

`GROUPING(e, ...)` tells a NULL that a set left out from a NULL in the data: a whole number with one bit per argument, the last argument the lowest, set where the argument is left out of the row's set. `GROUPING(region, product)` is 0 on a detail row, 1 on a region subtotal and 3 on the grand total. Each argument must be one of the `GROUP BY` expressions, compared as the grouped-query rule compares them.

**The order.** With no `ORDER BY` the groups come as a report reads them: for `ROLLUP(region, product)`, each region's rows, then that region's subtotal, and the grand total last. A left-out expression sorts after every value of it, NULL included, so a NULL in the data and a subtotal never interleave. `ORDER BY region, product` gives the same order whenever the data holds no NULL in those columns, since NULL sorts last; with NULLs in the data, `ORDER BY GROUPING(region), region` separates them.

## 3. Refused

| written | refusal |
|---|---|
| `GROUPING(x)` where `x` is not a `GROUP BY` expression | names the expression, as the grouped-query rule does |
| `GROUPING(...)` outside a grouped query, or in `WHERE` | as an aggregate there is refused |
| `CUBE` of more than 10 columns, or more than 4 096 sets in all | names the count |
| `ROLLUP` and the others in a recursive query's step | as any grouping is |

## 4. How it runs

The planner expands the elements into the sets, each a list of the `GROUP BY` expressions it holds, the expressions numbered once by their canonical print, so `a, ROLLUP(a, b)` names `a` once. Where a select-list, `HAVING` or `ORDER BY` expression is a `GROUP BY` expression that some set leaves out, it is wrapped in a node that reads NULL in a group whose set leaves it out.

The fold keys each group by its set: for each expression in `GROUP BY` order, a byte saying present or left out, then the present value's key. A left-out byte sorts after a present one, and groups are already put in key order, so the order of §2 is the fold's own and costs nothing. A plain `GROUP BY` is one set holding every expression, and folds as before.

The work grows with the sets: each row is folded once per set. It is in memory and sliced, as `GROUP BY` is.

## 5. Tests

- **the harness:** `sql_test` read each line into 8 KiB and cut a longer one short in silence; a generated answer here ran to 11 053 bytes. It reads 1 MiB now, and refuses a longer line rather than compare part of it.
- **parse:** each form printed back canonically, so a view keeps it, and the refusals at their positions.
- **semantics:** hand-written answers for `ROLLUP`, `CUBE`, `GROUPING SETS`, a plain column beside `ROLLUP`, `GROUPING` with one and two arguments, NULLs in the data beside subtotals, `HAVING` on a subtotal, the order with no `ORDER BY`, money totals, and every refusal.
- **differential:** random rows and random grouping sets, every answer computed by SQLite from the `UNION ALL` of one plain `GROUP BY` per set, which is what the sets mean. SQLite has no `ROLLUP`, so this is the definition written out, not another implementation of the shortcut. Compared ordered by every output column.
