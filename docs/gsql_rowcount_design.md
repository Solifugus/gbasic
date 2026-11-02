# G-SQL: row counts in the planner, and the join order they choose

**Status:** Shipped 2026-11-01 (`estimate` in `src/gsql/lsm.c`, `order_joins` in `src/gsql/sql_plan.c`, the join levels in `src/gsql/sql_exec.c`; tiers in `tests/run_gsql_sql.sh` and `tests/gsql/store_test.c`). Work-order item 7 of `gsql_scale_measurements.md` §4: *"row counts in the planner, for join order and the side a hash join builds."*

## 1. The gap

A join runs its tables **in the order the `FROM` names them** (`gsql_join_design.md` §2), the first as the outer loop. That design said *"the order is not chosen ... a planner that reorders later changes no answer, only the cost"*, and left reordering until the planner had statistics.

Order is most of a join's cost:

- **An indexed inner table** costs one seek per outer row, so the outer table should be the smaller.
- **A hash join** reads its inner table whole and keeps it in memory (`gsql_hashjoin_design.md`), so the inner, hashed side should be the smaller.

Written the wrong way round, `from orders o join customers c on o.customer = c.id` with ten million orders and a thousand customers seeks a customer ten million times. Turned around, it reads a thousand customers and seeks their orders, if `orders.customer` is indexed. And when nothing is indexed, it hashes the ten-million-row table into memory where a thousand-row hash would do.

## 2. Where a row count comes from: the engine, estimated

**An estimate, not a count kept on every write.** Two ways were weighed:

| | exact count per table | estimate from the engine |
|---|---|---|
| cost on a write | one more put per row written | none |
| accuracy | exact | overcounts an overwritten or deleted key until compaction folds it |
| also gives | `count(*)` in O(1) | nothing more |

Load rate is already the item to profile at 10⁸ (§1 item 2 there), and a planner needs the order of magnitude, not the digit. So the engine answers an estimate, `estimate(txn, tree, &entries)`:

- **Sorted files:** each file records its entry count and its smallest and largest key. A file holding only this tree counts whole; a file spanning several trees counts the share of its index blocks that end inside this tree.
- **The memtable:** keeps a count per tree, incremented where an entry is inserted (commit and log replay, the only two places). The frozen memtable being flushed counts too.
- **Not the transaction's own unwritten changes:** a plan made inside a transaction sees what was committed.

An overwritten key counts once per version still held, and a deleted one until compaction removes it. Both overstate a table that has churned, which matters only if two tables' estimates are within that error, and then either order is close to the other in cost.

## 3. What the planner does with it

**Only when every join is `INNER` or a comma/`CROSS` join, and every source is a table.** A `LEFT` join's order is part of its meaning, and a derived table's size is not known until it runs. Those statements keep `FROM`'s order, as now.

**A cost model, and every order costed:**

- **The cost of an order** is the rows it reads. A table reached by an index seek costs the log of its rows for each row read before it. One reached by an equality to earlier tables is hashed, costing one probe per earlier row and **four times its own rows to build**, since a hash holds its side in memory. Anything else costs its rows for every earlier row.
- **Every order is costed for up to six tables** (720 orders at most), the cheapest kept, ties in `FROM`'s order. Beyond six, a greedy order: the smallest first (or one pinned to a row by a constant on a unique key), then each next one reachable by a seek, else by a hash, else the smallest.
- **The order is changed only when it costs under two thirds of `FROM`'s.** A join written well, or a small one, runs as written.

The build weight is what makes the hashed side the smaller. Weighed at its rows alone, smallest-first hashes the larger of two unindexed tables, and found by the order tier.

The existing access-path choice then runs over that order, unchanged in what it asks: *"which conditions read only tables placed before this one."* The hash join's built side follows from the order, since the hash is always the inner table.

**`ON` conditions of inner joins become ordinary conditions,** placed at the first table in the new order at which everything they read is in hand. That is what they mean for an inner join.

## 4. What changes for a program

- **No answer changes.** The same rows come back.
- **The order of rows without `ORDER BY` may change**, and may change as tables grow. SQL promises no order without `ORDER BY`, and `gsql_join_design.md` §2 said so when joins shipped.
- **`EXPLAIN` shows the order chosen,** each table with its estimate, so a slow join can be read.

## 5. Tests

- **The differential tier** (`run_gsql_sql.sh`): a forced scan also keeps `FROM`'s order, so every generated join is compared reordered against as written. A floor requires reordered plans, 194 to 238 a seed.
- **The join oracle** compares a join with `r` read alone, and must allow one case. **A plan reading `r` by another path may meet a row whose condition raises** (a cross-currency comparison) that `r`'s own path skipped, or skip one it met. SQL fixes no evaluation order. It is allowed only when `r` read whole really refuses, so the refusal is in the data, and is counted: 0 or 1 a seed. Before that rule, three seeds failed on exactly this.
- **The order chosen** (`tests/gsql/order_test.bas`, through `EXPLAIN`):
  - an indexed large table written first is read second, sought by its index;
  - of two unindexed tables, the smaller is hashed, whichever is written first;
  - a well-written join keeps its order;
  - a `LEFT` join and a derived table keep `FROM`'s order;
  - the estimates equal the true counts in the memtable.
- **The estimate** (`store_test.c`): within a factor of two for three interleaved trees of 40, 400 and 2 000 entries, in the memtable alone, across sorted files that each hold all three, after a flush, and after a reopen. The three keep their order. The 40-entry tree read 84 before a block that only partly holds a tree counted as half.
- **The cost** (`order cost`): 10⁴ orders by 10² customers written orders-first, the chosen order 68× faster than `FROM`'s, the answers equal and the plan really reordered.
- **Seven perturbations red:**
  - the memtable counts ignored;
  - the shared files ignored;
  - no reordering (the differential floor, the cost tier and the order tier);
  - the build weighted at its rows;
  - the conditions left where `FROM`'s order put them (wrong answers, caught by the differential).
