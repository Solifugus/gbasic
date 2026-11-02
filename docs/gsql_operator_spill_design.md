# G-SQL: query operators that spill to disk

**Status:** Proposal 2026-11-01, for Matthew's review: the rest of work-order item 6 (`gsql_scale_measurements.md` §4), *"the query operators are still to come"* (`gsql_spill_design.md`). Nothing here is built. §5 lists the decisions that are his.

## 1. The gap

Six operations hold their working rows in memory (`gsql_scale_measurements.md` §3): a sort no index gives, `GROUP BY` and the aggregates, `DISTINCT` and the set operations, a derived table or `WITH` query, a hash join's inner side, and `INSERT ... SELECT`. At a billion rows each is an owner killed partway, which the program sees as `LOST`.

The external sorter exists (`src/gsql/xsort.c`, 64 MB, runs under `$TMPDIR` or `/var/tmp`, stable) and so does the index build that uses it. It orders **bytes**, by `memcmp`.

## 2. The bridge: the index key encoding

A row is sorted, grouped or deduplicated by gBASIC's own order and equality. The index key encoding (`src/gsql/key.c`) turns a value into bytes whose `memcmp` order *is* gBASIC's order, and grouping, `DISTINCT` and `UNIQUE` already use its equality. So a spilled operator encodes its sort or group key with it and hands the sorter `(key, row)`.

**Where the two disagree, and how the meaning is kept:**

| case | in memory today | `memcmp` of keys | so a spilled operator |
|---|---|---|---|
| two currencies in one sort column | refused (`EXPRESSION`) | the encoder refuses too | refuses, the same |
| a record, a duration, an array | refused | no key exists | refuses, the same |
| a number against text in one column | `<` refuses | ordered by kind tag, silently | **records each column's kind** as rows arrive, and refuses at the first row of another kind, naming the column |
| `NULL` | after every value | the encoding's absence placement | the same, `DESC` included |
| ties | stable | the sorter is stable | the same |

The third row is the one that matters: without it a spilled sort would answer where the in-memory sort refuses. With it, a statement answers the same at any size.

## 3. Each operator

| operator | spilled form |
|---|---|
| `ORDER BY` without an index | keys and rows into the sorter, read back in order; `LIMIT n` with no spill keeps a top-n heap, which never needs the disk |
| `GROUP BY` and the aggregates | rows sorted by the group key, then each group folded as it streams past, one group in memory at a time |
| `DISTINCT`, `UNION`, `INTERSECT`, `EXCEPT` | rows sorted by their whole key; equal neighbours are duplicates, and a set operation tags each row with its side |
| a derived table, a `WITH` query, `INSERT ... SELECT` | no ordering is needed, only somewhere to put the rows: a **row spool**, an append-only temporary file read back from the start |
| a hash join's inner side | past the budget, both sides are sorted on the join key and merged; the hash stays the form below the budget |

Each spills only once its rows pass the budget, so a small query runs exactly as now, and each stays sliced (`gsql_slicing_design.md`), so other clients are served between slices.

## 4. Order of work

1. The kind check and `ORDER BY`: the sorter is already there, and a sort is the plainest test of §2.
2. `GROUP BY` and the aggregates, on the same sorted stream.
3. `DISTINCT` and the set operations.
4. The row spool, for derived tables, `WITH` and `INSERT ... SELECT`.
5. The join, last, since a sort-merge join is the most new code.

Each is its own increment with a design note, and each is tested the way the differential tier already tests access paths: every generated query run with a budget of one row, so it spills, against the same query in memory, with a floor on spilled runs so a budget that quietly never spilled cannot pass.

## 5. Decisions for Matthew

1. **The budget:** 64 MB per operator, as the index build has, or one budget shared by every operator in a statement, or one per owner shared by every client? Per operator is simplest; per owner is what bounds the process.
2. **A group's output order.** `GROUP BY` without `ORDER BY` has no promised order. A spilled `GROUP BY` would answer in group-key order, and the in-memory one answers in whatever order it does today. Fine to let them differ, as SQL allows?
3. **Where spill files go:** `$TMPDIR` or `/var/tmp`, as the sorter does now, or a directory inside the database, so a database on a NAS spills to the NAS? The local disk is faster; inside the database keeps a tenant's data in one place.
