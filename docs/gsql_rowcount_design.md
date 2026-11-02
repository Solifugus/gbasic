# G-SQL: row counts in the planner, and the join order they choose

**Status:** Proposal, designed 2026-11-01 and being built: work-order item 7 of `gsql_scale_measurements.md` §4: *"row counts in the planner, for join order and the side a hash join builds."*

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

**A greedy order:**

1. **First:** the table with the fewest estimated rows. A table pinned to one row by an equality on its whole primary key (`where c.id = 7`) counts as one.
2. **Each next:** among the rest, a table reachable by an **index seek** on columns of the tables already placed, the smallest such first. Failing that, one joined by an **equality** to them (a hash join, built on it), the smallest first. Failing both, the smallest (a cross product).
3. **Ties keep `FROM`'s order,** so equal tables run as written.

The existing access-path choice then runs over that order, unchanged in what it asks: *"which conditions read only tables placed before this one."* The hash join's built side follows from the order, since the hash is always the inner table.

**`ON` conditions of inner joins become ordinary conditions,** placed at the first table in the new order at which everything they read is in hand. That is what they mean for an inner join.

## 4. What changes for a program

- **No answer changes.** The same rows come back.
- **The order of rows without `ORDER BY` may change**, and may change as tables grow. SQL promises no order without `ORDER BY`, and `gsql_join_design.md` §2 said so when joins shipped.
- **`EXPLAIN` shows the order chosen,** each table with its estimate, so a slow join can be read.

## 5. Tests

- **The differential tier:** every generated join runs reordered and in `FROM` order (`force_scan` and a new `force_order`), and the two must return the same rows as multisets.
- **The order chosen:** a small table and a large one, written in each order, both planned with the small one outer when the large one has an index on the join column, and with the large one outer, probing a hash of the small one, when it has none. `EXPLAIN` asserts both.
- **The estimate:** within a factor of two of the true count, in the memtable alone, after a flush, across several files, and for a table sharing files with others. Overwrites and deletes are measured, not asserted.
- **A `LEFT` join and a derived table** keep `FROM`'s order.
- **The cost:** at 10⁵ × 10³ written large-first, the reordered join is at least 10× faster than `FROM` order, as a ratio.
