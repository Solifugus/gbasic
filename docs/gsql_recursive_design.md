# G-SQL: `WITH RECURSIVE`

**Status:** Shipped 2026-11-02, built the day Matthew said "go ahead". The second of the three features the gBASIC books' volume 3 needs (`gbasic-books/docs/volume-3-enterprise-integration-and-automation.md` §5.5): the recall chapter's bill-of-materials traversal, which "needs a guard against cycles".

## 1. What is written

```sql
WITH RECURSIVE parts (part, qty, depth) AS (
    SELECT component, qty, 1 FROM bom WHERE assembly = 'robot-arm'
    UNION ALL
    SELECT b.component, p.qty * b.qty, p.depth + 1
    FROM parts p JOIN bom b ON b.assembly = p.part
)
SELECT part, sum(qty) FROM parts GROUP BY part
```

`WITH RECURSIVE` lets each query of its list read itself. One that does is **recursive**; one that does not is an ordinary `WITH` query. A recursive one is `anchor UNION [ALL] step`:

- the **anchor**, everything before the last `UNION`, does not read the query. It may itself be a `UNION` of several `SELECT`s;
- the **step**, the last `SELECT`, reads the query once, in its `FROM`.

`RECURSIVE` is not reserved; it is a keyword only after `WITH`.

## 2. What it answers

The rows are worked out in **rounds**, as SQL defines them:

1. the anchor's rows are the first round's;
2. each further round runs the step with the query reading **only the round before's rows**;
3. a round that adds no rows ends it.

The answer is every round's rows, the anchor's first, then each round's in order. A query reading it with no `ORDER BY` sees them in that order, which is breadth first.

With `UNION ALL` every row is kept. With `UNION` a row the same as one already kept, keyed as `DISTINCT` keys it, is dropped and does not go on to the next round. So a `UNION` over a cyclic graph ends, as long as the rows hold only what repeats (the node, not a running depth).

## 3. Cycles: the guard

A cyclic graph read with `UNION ALL`, or with a column that changes every round such as a depth or a running quantity, never stops adding rows. Two things stop it.

**`CYCLE`**, SQL's own clause, written after the query's `)`:

```sql
WITH RECURSIVE parts (part, qty) AS ( ... )
    CYCLE part SET looped USING route
SELECT part, qty, looped, route FROM parts
```

It adds two columns to the query: `route`, an array of the `CYCLE` columns' values from the anchor row down to this one, and `looped`, `TRUE` on a row whose `CYCLE` values are already in its parent's route. A looped row is answered, so the cycle can be seen and reported, but it starts no further rows. One `CYCLE` column gives a route of its values, as `[1, 4, 7]`; several give a route of arrays. Values are matched as `DISTINCT` matches them. In this release the mark is `TRUE` or `FALSE` (`TO ... DEFAULT ...` is refused), and `SEARCH` is not offered.

**A limit on rounds**, which needs nothing written: a query still adding rows after **100 000 rounds** is refused with `EXPRESSION`, naming the query and the remedies (`CYCLE`, `UNION` in place of `UNION ALL`, or a bound such as `WHERE depth < 50` in the step). That is far more than any hierarchy needs, and enough for a ten-year calendar a day at a time (3 653 rounds), so it refuses only a query that would not end. The limit is a constant in this release.

**A limit on memory**, also needing nothing written. Rows that double each round reach the end of memory long before any round limit: measured, a graph with two cycles read with `UNION ALL` ended the process at 2.7 GB after 27 seconds, which in an owner would be `LOST` for every client. So a recursive query whose rows hold more than **1 GiB** of memory is refused the same way, naming the same remedies; that one was refused at 1 GB after 8 seconds. Both limits stand until the spill work (`gsql_operator_spill_design.md`) puts these rows on disk; the round limit stays after it.

## 4. Refused, each saying what to write instead

| written | refusal |
|---|---|
| `INTERSECT` or `EXCEPT` joining anchor and step | a recursive query is `anchor UNION [ALL] step` |
| the anchor reading the query | the anchor reads only other tables and queries |
| the step reading it twice, inside a subquery or derived table, or on the null-filled side of a `LEFT JOIN` | read it once, in the step's `FROM` |
| an aggregate, `GROUP BY`, `HAVING` or a window function in the step | each round's rows are separate rows, not a group: aggregate in the query that reads the recursive one |
| `ORDER BY`, `LIMIT` or `OFFSET` in the recursive query's body | order or limit the query that reads it |
| a step with a different number of columns from the anchor | as `UNION` refuses it |

## 5. How it runs

A recursive query is materialized where it is read, as a `WITH` query is (`gsql_derived_design.md` §4): the anchor runs once; then each round runs the step through a cursor of its own, its scan of the query reading the previous round's rows. With `UNION`, the keys of every row kept so far are held, to drop repeats. With `CYCLE`, each row carries its route, and a row is marked by comparing its `CYCLE` key with each key on its parent's route.

Like `WITH` today it runs in memory and as one piece (`gsql_practices.md` §2): fine at the sizes a hierarchy has, and the spill work (`gsql_operator_spill_design.md`) moves it to disk. A recursive query read twice runs twice.

`EXPLAIN` shows it as `CTE name` over a `Recursive Union`, with the anchor's plan and then the step's, whose read of the query is a `CTE Scan`.

## 6. Tests

- **parse:** each form printed back canonically, `CYCLE` included, and each refusal at its position.
- **semantics:** hand-written answers for a count, a tree's depths, a bill of materials with quantities multiplied down, a cyclic graph ending under `UNION`, `CYCLE` marking the loop and stopping, and every refusal. The round limit is lowered for the test through a test-only setter, as `force_scan` is, so the refusal is reached in a few rounds and the test does not take the time 100 000 would.
- **differential:** random directed graphs (cycles included) and random recursive queries (reachability under `UNION`; paths with a depth bound under `UNION ALL`; with and without a join to another table), every answer computed by SQLite's `WITH RECURSIVE` on the same rows and compared ordered. A census floor makes cyclic graphs, `UNION` and `UNION ALL` all appear.
