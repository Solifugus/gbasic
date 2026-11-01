# G-SQL: spilling to disk, and the external-sort index build

**Status:** The sorter and its first user shipped 2026-11-01 (`src/gsql/xsort.c`; `gsql_index_create` in `src/gsql/table.c`; `run_gsql_table.sh`'s sort tier and its model's spilling seeds). The query operators are still to come. What was measured is §6; what the measuring found in the engine is §7. It is item 6 of the work order in `gsql_scale_measurements.md` §4: *"spill-to-disk, with the external-sort index build as its first user"*. This document designs the sorter and its first user. The query operators that hold rows in memory (§3 of the measurements) come after, each its own increment, on the same sorter.

## 1. The gap

`CREATE INDEX` over existing rows is superlinear: 1.75 s at 10⁶ rows, 33.7 s at 10⁷, 1 180 s at 10⁸, about 30× for each 10× (`gsql_scale_measurements.md` §2). Timed in parts at 10⁷, the cursor stepping over the rows took 5.1 s, decoding 2.8 s, writing the entries 14.5 s and the commit 11.4 s. The build writes index entries in **row order**, which is random in index-key order, inside the one transaction that is also reading the rows. So:

- the transaction spills its entries as sorted runs, and every run overlaps every other;
- the cursor reading the rows merges over every run spilled so far, including runs of the index tree it does not read, and rebuilds itself after each spill;
- the commit installs every run in level 0, where compaction has to merge them all.

A billion rows would take half a day or more.

## 2. The rule

**Entries are sorted before they are written.** The build reads every row first, handing each entry to an **external sorter**, which holds a bounded amount in memory and writes sorted runs to temporary files when it is full. Then the sorter merges the runs once and the build writes the entries **in key order** into the new index tree.

- **Reading and writing no longer interleave.** The cursor over the rows runs before the transaction has written anything, so it merges over nothing.
- **The entries arrive in order,** so the runs the transaction spills do not overlap each other or anything below them, and compaction moves them down a level unrewritten (`lsm.c`'s trivial move, which exists for a sorted load).
- **A unique index's duplicates are adjacent** in sorted order, so the check is a comparison with the previous entry, not a lookup.

## 3. The sorter

`src/gsql/xsort.c`, over bytes, so the query operators can use it later:

- **Keys compare by `memcmp`.** G-SQL's index key encoding is built so `memcmp` order is gBASIC's order (`gsql_storage_design.md` §3.2), so the sorter needs no comparison of its own.
- **Memory is bounded** at 64 MB by default: entries accumulate in an arena; when it is full they are sorted and written as one run.
- **A run is a temporary file:** each entry a 4-byte key length, a 4-byte value length and the bytes. The merge reads each run through a 64 KB buffer and keeps a heap of the runs' heads, so memory during the merge is the buffers, whatever the number of runs.
- **If everything fits, nothing is written:** the sorted arena is read directly.
- **Ties keep insertion order** (the sort is stable, and runs are merged in the order written), which the query operators will need for a stable `ORDER BY`.

**Where the files go.** Matthew asked for a local temporary directory, since a database on a NAS should not pay the NAS for scratch space. The sorter uses `$TMPDIR` if it is set, and **`/var/tmp` otherwise, not `/tmp`**, which is RAM (tmpfs) on many systems, including this one, and would turn a spill back into memory. Each sort makes a private directory there (mode 0700) and removes it when it ends, including when it fails.

## 4. Decisions

- **The build is still one transaction.** A failed build, or a duplicate in a unique index, leaves no index, as now.
- **Temporary files are not durable and not checksummed.** They live for one statement; a crash loses them, and the statement with them, which is what a crash does to the statement today.
- **The budget is not an option yet.** 64 MB is a quarter of what the build used at 10⁸ (276 MB); making it configurable waits for a reason.

## 5. Tests

- **The sorter alone**, against an in-memory sort of the same entries: random keys with duplicates, empty keys and values, keys longer than a run, a budget small enough to force hundreds of runs, and none at all. It must also remove every file it made, and leave the directory as it found it on failure.
- **The index build**, through `run_gsql_table.sh`'s model workload, which already builds indexes over existing rows and checks them, with the sorter's budget lowered so every build spills.
- **The scale point:** `CREATE INDEX` at 10⁶ and 10⁷ rows, timed before and after, on ext4 under `/var/tmp`. The claim is that the growth per 10× falls well below 30×.

## 6. Measured

`research/gsql/scale.sh` on NVMe ext4 (`CREATE INDEX t_k ON t (k)` over existing rows, one transaction, its commit timed apart):

| rows | before: build (commit) | sorted (§3) | sorted, and the engine fix (§7) |
|---|---|---|---|
| 10⁶ | 1.91 s (0.12) | 1.44 s (0.06) | 1.45 s (0.07) |
| 10⁷ | 37.5 s (12.9) | 26.2 s (10.4) | **15.8 s (0.34)** |
| 10⁸ | 1 180 s (478) | — | **173 s (0.3)**, with §8's lookup fix |

**Growth per 10× fell from 19.6× to 10.9×**, which is linear plus the index's own depth. Memory added by the build is 88–94 MB, the sort's 64 MB arena among it.

## 7. What measuring it found: the commit was compaction

Sorting alone took the statement from 24.7 s to 15.8 s at 10⁷ and left the commit at 10.4 s. Timing the commit's parts showed the time was not the flush, the last spill or the manifest: **10.38 s of it was compaction, run inside the commit**, merging 411 MB of the index's sorted runs with all 304 MB of the table's level-1 files. Two engine rules made sorted runs impossible to move:

- **Level 0 was compacted as one block.** Its span ran from the lowest key of any level-0 file to the highest. The build's runs held tree 5 (the index), and its smallest runs held tree 0 (the catalog) and tree 3 (the schema), so the span covered tree 4, the table, and every level-1 file was an input. **A level-0 file that overlaps no other level-0 file and nothing in level 1 now moves down on its own**, before anything is merged.
- **A spilled run could hold two trees.** The schema entry the build writes last shared a run with the index's tail, so that run spanned the table by itself. **A run is now cut wherever the tree changes.** Different trees' keys never meet, so how a transaction's entries are cut into runs changes nothing a reader sees.

The first instrumented build meant to find this failed its own commit, because the trace read the transaction after the commit had freed it. That is why the night's write-up called the commit unexplained.

**Held by `run_gsql_store.sh`** (`sorted_load` in `tests/gsql/store_test.c`): a sorted load into a new tree beside a large one, with one entry in a lower tree written last, must commit writing under a quarter of what the load spilled. Measured 3.05 MB against 1.08 MB on the engine before, and **each half of the fix alone fails it**. The first version of the check passed without the per-tree cut, because nothing in its load reached across the rows; the lower tree written last is what CREATE INDEX's schema write does. A first version also failed on the fixed engine, because its first tree was loaded in random order and left a compaction backlog the commit legitimately paid down. Every crash mode of the gate passes with both rules.

**Perturbations proven red:** a unique index's duplicate check removed, the merge's key order reversed, the merge's ties unstable, the per-tree cut removed, the lone-file move removed.

## 8. What the new layout exposed: a point read walked its levels

At 10⁸ the build was fast and **counts through the index were about 56% slower**, the same both run alone and side by side with the engine before (1 953 against 1 249 µs). At 3 × 10⁷ it reproduced at 7–10%, and crossing the two engines over the two databases showed it was the **layout**, not the code. Both read **the same blocks** (30.8 a query, 5.5 from the cache); callgrind showed 312 000 more instructions a query, all in key comparisons, from `lookup`, the point read each counted row costs: 113 comparisons a read against 74. **`lookup` walked every file of a level from the left** until one could hold the key. Moved runs leave a level with more, smaller files (150 to 170 in level 2 against 50), so the walk grew. A level at or below 1 is sorted and disjoint, so it is a binary search now.

- **At 3 × 10⁷:** a count on the new layout went 332 → 307 µs, level with the old layout's 300.
- **At 10⁸,** measured alone on the final build: the load 1 112 s → **651 s** (90 000 → 153 500 rows a second), since every insert's uniqueness check is a point read; `CREATE INDEX` 1 180 s → **173 s**, its commit 478 s → 0.3 s; point reads 25.9 / 504 µs → 28.5 / 551 (median / p99); a count by `k` 918 → 1 133 µs. **The two baselines are an older commit measured on an earlier day,** so those last two are not a comparison the side-by-side runs support; under identical conditions at 3 × 10⁷ the layouts are level.
- **This matters beyond the index build:** at 10⁹ rows a level holds hundreds of files, and the walk was linear in them.

**Perturbation proven red:** the search's bound off by one (the store suite).
