# G-SQL: query operators that spill to disk

**Status:** Partial 2026-11-02: §4's steps 1 to 4 are built: `ORDER BY` (§6), `GROUP BY` (§7), the row spool (§8), and `DISTINCT` and the set operations (§9). The join (step 5) and window functions remain. The rest of work-order item 6 (`gsql_scale_measurements.md` §4), *"the query operators are still to come"* (`gsql_spill_design.md`). Matthew said "go ahead" on 2026-11-02 without ruling on §5, so §5 records the defaults taken, each his to reverse.

## 1. The gap

Six operations hold their working rows in memory (`gsql_scale_measurements.md` §3): a sort no index gives, `GROUP BY` and the aggregates, `DISTINCT` and the set operations, a derived table or `WITH` query, a hash join's inner side, and `INSERT ... SELECT`. At a billion rows each is an owner killed partway, which the program sees as `LOST`.

The external sorter exists (`src/gsql/xsort.c`, 64 MB, runs under `$TMPDIR` or `/var/tmp`, stable) and so does the index build that uses it. It orders **bytes**, by `memcmp`.

## 2. The bridge: the index key encoding

A row is sorted, grouped or deduplicated by gBASIC's own order and equality. The index key encoding (`src/gsql/key.c`) turns a value into bytes whose `memcmp` order *is* gBASIC's order, and grouping, `DISTINCT` and `UNIQUE` already use its equality. So a spilled operator encodes its sort or group key with it and hands the sorter `(key, row)`.

**Where the two disagree, and how the meaning is kept:**

| case | in memory today | `memcmp` of keys | so a spilled operator |
|---|---|---|---|
| two currencies in one sort column | refused (`EXPRESSION`) | the encoder refuses too | refuses, the same |
| an array, a record | refused (`<` has no order for them) | no key exists | refuses, the same |
| a duration | **sorted**, by its total seconds | no key exists (its equality is field by field) | **stays in memory**: a sort that meets one does not spill, so it answers as it always did and only its memory bound is lost |
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

## 5. Decisions for Matthew, and the defaults taken

1. **The budget:** 64 MB per operator, as the index build has, or one budget shared by every operator in a statement, or one per owner shared by every client? Per operator is simplest; per owner is what bounds the process. **Taken: per operator**, the simplest, and the index build's.
2. **A group's output order.** This turned out not to be a question: an in-memory `GROUP BY` already answers its groups in key order (`agg_finish` sorts them by their keys), which is the order a spilled one would have. **Nothing differs.**
3. **Where spill files go:** `$TMPDIR` or `/var/tmp`, as the sorter does now, or a directory inside the database, so a database on a NAS spills to the NAS? The local disk is faster; inside the database keeps a tenant's data in one place. **Taken: as the sorter does now.**

## 6. Built: `ORDER BY` (2026-11-02)

A sort collects its rows as before, counting the memory they take (each row's bytes, its ORDER BY values and its slot). Past the budget, every row in hand goes to the external sorter, keyed by its ORDER BY values in the key encoding (`DESC` terms inverted), and so does every row after; the sorter's merge is then the sort, and `OFFSET` skips from its front. Whether or not it spills, each ORDER BY term's values must be comparable with its first, checked as `<` checks them, so a spilled sort refuses exactly where the in-memory one does, with the same words.

**Measured** (`sql_test` in process, rows of an integer key, an integer and 47 bytes of text, `ORDER BY v DESC, id`, peak resident memory of the whole process including its 64 MB of sorter run buffer):

| rows | spilled | in memory |
|---|---|---|
| 2 000 000 | 236 MB, 3.6 s (3 runs) | 614 MB, 5.2 s |
| 6 000 000 | 247 MB, 10.7 s (7 runs) | 1 706 MB, 17.4 s |

The spilled sort's memory stays flat as its input triples, and it is faster, since the sorter compares bytes where the in-memory merge sort calls back into gBASIC for each comparison.

**Tested** by running the whole semantics corpus with a budget of one byte and 64-byte runs, so that every `ORDER BY` in it goes through disk (76 sorts, 295 runs), requiring the same answers, with floors on both counts; and under valgrind the same way.

**Not yet measured:** a sort meeting a duration staying in memory, since SQL reaches a duration only through a record's field or a parameter, and no corpus case orders by one. **A limit found on the way:** the sorter merged all its runs at once, a file each, so past about a thousand runs (64 GB of sort at 64 MB a run) it would meet a common open-file limit, and each open run held a 64 KB buffer. The index build shared it. **Fixed the same day (§10).**

## 7. Built: `GROUP BY` (2026-11-02)

The fold counts the memory its groups take (each group's key, first row and accumulators). Past the budget it does not merge what it has with a sorted stream: it **drops the groups and reads its input again from the start**, in the same snapshot, every row going to the sorter under its group key (once per grouping set, for `ROLLUP` and the others), with its scans' rows as the value. The groups then come off the sorter in key order, which is the order the in-memory fold already answers in, each folded as its rows stream past and finished before the next begins, so one group is in memory at a time. `HAVING`, `DISTINCT`, the statement's `ORDER BY` (which may spill itself) and `LIMIT` then see the groups as before.

Reading the input again costs, at most, the rows read before the budget was passed, a second time. That was chosen over merging the groups already folded with the sorted rows, which would need the fold's accumulators kept and combined, a second way of folding that could disagree with the first.

**Not spilled:** a grouped query that feeds window functions, since the windows hold every group themselves (their spill is §4's later work); one with no `GROUP BY`, which is one group; and a grouped query with no `FROM`.

**Measured**, `GROUP BY id HAVING count(*) > 1` over the same table, so every row is a group and none is answered, peak memory of the whole process:

| groups | spilled | in memory |
|---|---|---|
| 2 000 000 | 185 MB, 4.2 s (4 runs) | 1 358 MB, 6.0 s |
| 6 000 000 | 198 MB, 13.8 s (11 runs) | 3 969 MB, 17.7 s |

A first measurement read the groups through a derived table, `SELECT count(*) FROM (SELECT id, count(*) ... GROUP BY id)`, and grew from 287 to 724 MB: that was the derived table holding every group it was given, which is the row spool's work (§4, step 4, now §8), not the fold's.

**Tested** by the spilled tier, the whole corpus with every `GROUP BY` and `ORDER BY` through disk (113 sorts), and one seed of the grouping sets differential spilled (about 400 sorts, 36 000 runs), each answering as in memory; under valgrind the same way. Three perturbations (a stream that never joins equal keys, no restart of the input, a grouping set's number lost) each went red in both.

## 8. Built: the row spool (2026-11-02)

Every place that held rows to read back now holds them in one **row store**: a derived table's and a `WITH` query's rows, a hash join's inner side, and `INSERT ... SELECT`'s rows (read in full before any is written, so a table copied into itself never meets its own new rows). A store keeps its rows in memory until they take the operator budget, then writes every one to a **spool**, an append-only file in a private temporary directory beside the sorter's, and keeps only each row's offset, eight bytes in place of the row. A row is read back by its offset, in any order and any number of times, which a nested loop rescanning a derived table and a hash chain both need.

**Measured**, peak memory of the whole process:

| statement | rows | spooled | in memory |
|---|---|---|---|
| `SELECT count(*), sum(n) FROM (SELECT id, count(*) AS n FROM b GROUP BY id) AS t` | 2 000 000 | 205 MB, 7.7 s | 1 334 MB, 6.4 s |
| the same | 6 000 000 | 206 MB, 23.0 s | 3 899 MB, 19.6 s |
| `INSERT INTO b2 SELECT id, v, pad FROM b` | 2 000 000 | 150 MB, 10.8 s | 456 MB, 8.2 s |
| the same | 6 000 000 | 213 MB, 34.9 s | 1 141 MB, 26.7 s |

Spooling costs about a fifth more time, for reading rows back from disk. The derived table's memory is flat; the `INSERT`'s still grows a little, which is the write transaction's own buffering, not the rows read. A hash join over this table hashed its three-row partner, as it should, so a large inner side was not measured; the spilled tier covers it for correctness.

**Not spooled:** a `WITH RECURSIVE` query's rows, which keep their own 1 GiB guard (`gsql_recursive_design.md` §3), and a hash join's keys (one per distinct key), which stay in memory.

**Tested** by the spilled tier, which now also spools every row store in the corpus (269), with a floor, and by the recursive differential spooled (about 1 600 spools a seed, through self-scans and hash joins), each answering as in memory; clean under valgrind spooled. Two perturbations (the rows before the budget lost, a read from the wrong offset) went red. A third, writing without seeking after a read, **stayed green**: no store is read before it is complete, so no read is ever followed by a write. The seek stays, because C requires it between a read and a write.

## 9. Built: `DISTINCT` and the set operations (2026-11-02)

Both keep the order the in-memory forms answer in, which is arrival order: `SELECT DISTINCT` answers each row's first occurrence where it first came, a `UNION` the left side's rows then the right's, an `INTERSECT` or `EXCEPT` the left side's. A spill that answered in key order would differ, so each spilled form sorts twice: once by row key, to decide which rows stay, and once by arrival number, to put them back.

**The set operations.** Each side's rows go into a row store (§8). Past the budget, the decision is made off the sorter: each row is an entry keyed by its row key (the key `DISTINCT` uses), then its side (the right first), then its arrival number. Within one row key the right side's count is known before the left side's rows go past, which is all `INTERSECT [ALL]` and `EXCEPT [ALL]` need; a `UNION` keeps each key's first row; `UNION ALL` is the two stores read one after the other and needs no sort. The rows kept go to a second sorter keyed by arrival number, and are read back from their stores in that order. The compound's own `ORDER BY` then collects its rows as a plain query's does (§6), so it spills too.

**`SELECT DISTINCT`.** The keys of the rows answered so far are held, to drop repeats. Past the budget, every key held goes to a sorter marked as already answered, and each later row goes in under its key with its arrival number, not answered yet. Once the input ends, the first row of each key not already answered is kept, and the kept rows come back in arrival order. A `DISTINCT` with no `ORDER BY` that passes its budget therefore answers its later rows only at the end, though in the same order; one with an `ORDER BY` collects them for its sort, as before.

**Measured**, `SELECT id FROM b WHERE id < 0 UNION SELECT id FROM b EXCEPT SELECT v FROM b ORDER BY 1 LIMIT 2`, every row of the table on two sides, peak memory of the whole process:

| rows | spilled | in memory |
|---|---|---|
| 2 000 000 | 291 MB, 13.6 s | 843 MB, 7.5 s |
| 6 000 000 | 409 MB, 46.2 s | 2 643 MB, 27.0 s |

Not flat, as §6 to §8 are: each row store keeps an eight-byte offset per row, and this statement holds several stores over the same rows, which is the growth left. And slower than in memory, by 1.7 to 1.8 times, since it sorts twice and reads its rows back from disk; it is a statement that would otherwise have ended the owner.

**Tested** by a differential (`tests/gsql/gen_setop_diff.py`): random rows with repeats and NULLs and random `DISTINCT` and nested set operations, every answer computed outside G-SQL (SQLite for each `SELECT`, the multiset definition for the compound, since SQLite has no `INTERSECT ALL` or `EXCEPT ALL`), in memory and spilled; and the same queries with no `ORDER BY`, which must come back **in the same order** spilled as in memory, the order being what the second sort keeps. The spilled corpus now spills 166 operators. Clean under valgrind spilled. Three perturbations (the rows' arrival numbers lost, `EXCEPT ALL` keeping one row too many, `DISTINCT` keeping a key already answered) each went red, the last two in the order-parity check as well.

**What remains of §4:** the join (step 5) and window functions, which hold every row of their query; and the sorter's merge in passes is done (§10).

## 10. Built: the sorter merges in passes (2026-11-02)

The sorter opened every run at once for its one merge, a file and a 64 KB buffer each. This machine allows 524 288 open files, so tests writing 35 755 runs passed, holding 2.3 GB of buffers; on a system allowing the common 1 024, a sort of about 64 GB would have failed. Now, past 128 runs, each 128 consecutive runs are merged into one longer run first, in passes, until 128 or fewer remain for the last merge: at most 128 files and 8 MB of buffers open at once, however large the sort. Consecutive runs merged in order keep a tie with the earlier entry, so the sort stays stable. Each pass reads and writes the data once more, which only a sort of more than 128 runs (8 GB at the default run size) pays.

**Tested** in `run_gsql_table.sh`'s sort tier: 300 000 entries with a 4 KB budget make over 2 000 runs, sorted with the process allowed only 256 open files, against an in-memory stable sort, every file removed after. The sorter before this change fails it (28 432 mismatches, measured); the merged runs written out of order fail it too (299 219). The index build uses the same sorter, so it gains the same.

## 11. Proposal: window functions (Matthew's ruling, 2026-11-02: option C)

A windowed query holds every row, its state and its windows' inputs (`gsql_window_design.md` §5). Three ways were weighed: refusing past a budget (cheap, but a large window query then cannot run); one partition at a time in memory (bounded by the largest partition, so a window with no `PARTITION BY` over a large table is still the whole table); and **the partition-at-a-time form with each partition in a spooling row store**, which bounds even that. The last was ruled.

**The plan.** The rows are collected as now, but into a row store (§8) as encoded entries: each row's state, its windows' arguments, and its arrival number. Past the budget, each ordering's pass goes through the sorter keyed by partition key and the window's `ORDER BY` values in the key encoding, with the same comparability check `ORDER BY` makes (§6), and the partitions are read off one at a time, each into a row store of its own, which spools past the budget; the computing reads it by position, as it reads the in-memory array now. Each answer is written beside its row's arrival number, and a last sort by arrival number puts the rows and their answers back in the order the in-memory form answers in. A row store keeps eight bytes a row in memory, so the largest partition costs that much at most.

**Kept as now:** a query whose rows fit in the budget runs exactly as today. A value with no sort key (a duration) in a window's `ORDER BY` keeps that ordering in memory, as `ORDER BY` does, and the 1 GiB refusal stays the backstop for it.

**Tests:** the spilled tier and the window differential run spilled, as for every other operator, with a floor on the windows that spilled.

