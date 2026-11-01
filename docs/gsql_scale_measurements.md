# G-SQL at scale: measurements, 2026-11-01

**Status:** Record. Both programs ran on code at commit `0701188`. The 10⁸ line of `scale.jsonl` says `694205e`, a later commit that changed only documents, because `scale.sh` stamps the hash when a size finishes, not when its binary was built. On one machine:

- 22 cores and 62 GB of RAM;
- ext4 on NVMe, at `/var/tmp/gsql-bench`;
- disk rules: no file past 20 GB, nothing run with under 50 GB free, every directory removed afterwards.

The raw lines are in `research/gsql/results/2026-11-01/`, and the programs that took them are `research/gsql/scale.c` and `syncbench.c`. They are repeatable: run `research/gsql/scale.sh RESULTS 1000000 10000000 100000000`, and build and run `syncbench DIR` as its header says.

**Why measured now.** Matthew, the same day: some tables will reach "into the low billions of rows", most a few hundred thousand, and large databases will sit on a fast RAID5 NAS. The question was what holds at that scale and what does not, before more is built on it.

## 1. The whole stack at 10⁵ to 10⁸ rows

`scale.c` drives SQL text through the parser, planner, executor, table layer and engine, in one process: no owner, no socket.

**The workload:**

- **The table:** `t (id INTEGER PRIMARY KEY, k INTEGER, v TEXT, amt NUMBER)`.
- **The load:** in multi-row `INSERT`s, 10 000 rows to a synced transaction.
- **Then, on the loaded table:**
  - 20 000 point reads;
  - 2 000 range counts of 100 rows;
  - a `count(*)`;
  - a filtered `count(*)`;
  - `CREATE INDEX` on `k`, then 20 000 counts by `k`;
  - a crash: a child appends rows and ends without closing, and the next open is timed.

| | 10⁶ | 10⁷ | 10⁸ |
|---|---|---|---|
| load, rows/s | 197 000 | 172 000 | **90 000** |
| point read, median / p99 | 13.8 / 19 µs | 16.7 / 22 µs | 25.9 / **504** µs |
| range of 100, median | 122 µs | 142 µs | 154 µs |
| `count(*)`, rows/s | 6.8 M | 5.2 M | 3.7 M |
| filtered scan | 0.69 s | 8.0 s | 106 s (0.94 M rows/s) |
| `CREATE INDEX` | 1.75 s | 33.7 s | **1 180 s** (478 s of it the commit) |
| count by `k`, median | 28 µs | 87 µs | 918 µs |
| disk, bytes per row | 139 | 123 | 122 (12.2 GB) |
| memory, during load | 102 MB | 103 MB | 103 MB |
| memory added by the index build | 51 MB | 77 MB | 276 MB |
| open after a crash | 0.09 s | 0.20 s | 0.20 s |

Counts by `k` grow because each value of `k` matches about 1, 10 and 100 rows at the three sizes: about 9 µs a matching row. That is expected.

**What holds:**

- **Point and range reads** grow slowly with size, as an index should. Their medians stay in microseconds at 10⁸.
- **Memory during load is flat** at about 100 MB, whatever the size.
- **Crash recovery is bounded:** the log replayed is the log since the last flush, not the database.
- **Disk:** about 120 bytes a row for this table. A billion such rows is about 120 GB.

**What does not hold, in order of importance:**

1. **`CREATE INDEX` over existing rows is superlinear:** 1.75 s, 33.7 s, 1 180 s, about 30× for each 10×. A billion rows would take on the order of half a day or more.
   - **The cause** is one transaction that spills many sorted runs. Timed in parts at 10⁷:

     | part | time |
     |---|---|
     | the cursor stepping over the rows | 5.1 s |
     | decoding them | 2.8 s |
     | writing the entries | 14.5 s |
     | the commit | 11.4 s |

     The cursor merges over every run the transaction has spilled, including runs holding only another tree's entries, and rebuilds itself after each spill. The commit installs all the runs in level 0 at once. They overlap completely, since index keys arrive in random order, so compaction has to merge them together.
   - **The same path is taken by any very large transaction:** an `INSERT ... SELECT` of a large table, or an `UPDATE` of most of one.
   - **One cause was removed, a day's fix:** the build passed `GS_NO_OVERWRITE` for every entry. That made the engine look each key up across all those runs, though a non-unique entry ends in its row key and cannot collide. Without it 10⁷ fell from 75.9 s to 33.7 s.
   - **The rest wants an external sort:** entries sorted in bounded memory, runs merged once, the index loaded as sorted data. That is the machinery spill-to-disk needs (§3), so the index build is its first user.
2. **Load rate halves between 10⁷ and 10⁸.** Memory stays flat and the data fits in the page cache, so this is the engine's own write work, compaction, growing with the depth of the tree. Unmeasured beyond that. It is the next thing to profile, and a billion-row load at this rate is about three hours.
3. **Point-read p99 jumps** from 22 to 504 µs at 10⁸, while the median moves only from 17 to 26 µs. With everything in the page cache this is not disk. Unexplained so far: file handles kept open (`GsOptions.tune_d`) is a guess, not a finding.

**What these numbers are not:**

- **Not a NAS.** §2 models a slower sync; a real NAS's throughput and latency under load are not modelled.
- **Not data larger than memory.** 12 GB fitted in 62 GB of RAM. `research/gsql/bench.sh`'s `BENCH_MEM` runs a process under a memory limit for that, and this benchmark did not.
- **Not concurrent:** one client.

## 2. What a slower sync costs a writer

`syncbench.c` replaces the I/O layer's sync with "sleep, then the real sync" (research code only, through `io_internal.h`). It then commits 1, 10, 100 and 1 000 rows to a synced transaction at five added delays.

| added per sync | 1 row/commit | 10 | 100 | 1 000 |
|---|---|---|---|---|
| 0 (this disk) | 3 616 rows/s | 33 154 | 177 525 | 294 501 |
| 0.5 ms | 1 122 | 10 676 | 85 079 | 268 002 |
| 2 ms | 417 | 4 063 | 35 791 | 190 490 |
| 5 ms | 185 | 1 833 | 16 734 | 109 008 |
| 10 ms | 93 | 926 | 9 063 | 67 739 |

- **Exactly one sync per commit** at every size below a spill (1.00 to 1.07), so nothing else is syncing.
- **On slow storage, commits per second are the sync's inverse,** and **rows per commit decide row throughput.** At 10 ms, one row per commit is 93 rows a second and 1 000 rows per commit is 67 739.
- **The owner runs one writer at a time.** So many programs each committing one row share that one budget of about 93 commits a second. **That is what group commit is for:** concurrent commits sharing one sync. It is now measured as the item that matters most on a NAS (`gsql_storage_design.md` §5.4).

## 3. Where a query holds its rows

Measured by reading the executor, not by running it. Six operations hold their working rows in memory today:

- a sort no index gives;
- `GROUP BY` and the aggregates;
- `DISTINCT` and the set operations;
- a derived table or `WITH` query;
- a hash join's inner side;
- `INSERT ... SELECT`.

**At a billion rows each is a process killed partway** (the program sees `LOST`, and nothing committed is lost), unless spill-to-disk bounds it.

**The owner is single-threaded** (`gsql_owner_design.md` §6.1). Since 2026-11-01 a sort, a `GROUP BY` and the aggregates run in slices, so a point read waits at most a few milliseconds beside them (`gsql_slicing_design.md` §5). Building a hash join's inner side, a derived table, a `WITH` query and a set operation's sides still run as one piece, during which **every other client of that database waits**.

## 4. What follows from it

The work list after this, in order, with Matthew's additions the same day:

1. The practices guide for the books (`gsql_practices.md`).
2. **Online backup** through `gsql.backup`. Then a repeated backup into an existing copy, copying only what is new: the sorted files never change once written. Then a read-only open, which together make a replica for reports.
3. **A SQL dump:**
   - schema-only and full;
   - serial counters and `RECORD` values kept;
   - held by a dump, restore and dump again that must be byte-identical.
4. **One owner per database, enforced on any storage:** a lease kept in the database itself. *Shipped 2026-11-01* (`gsql_lease_design.md`).
5. **Group commit sized from the measured sync,** and a storage probe. *Shipped 2026-11-01* (`gsql_group_commit_design.md`).
6. **Spill-to-disk,** with the external-sort index build as its first user.
7. **Row counts in the planner,** for join order and the side a hash join builds.
8. Then profile load at 10⁸ (§1, item 2) and the point-read tail (item 3).
