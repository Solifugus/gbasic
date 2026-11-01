# G-SQL: spilling to disk, and the external-sort index build

**Status:** Designed 2026-11-01. It is item 6 of the work order in `gsql_scale_measurements.md` §4: *"spill-to-disk, with the external-sort index build as its first user"*. This document designs the sorter and its first user. The query operators that hold rows in memory (§3 of the measurements) come after, each its own increment, on the same sorter.

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
