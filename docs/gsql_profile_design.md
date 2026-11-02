# G-SQL: where load time and the point-read tail go at 10⁸ rows

**Status:** Shipped 2026-11-01, work-order item 8 of `gsql_scale_measurements.md` §4: *"profile load at 10⁸ (§1, item 2) and the point-read tail (item 3)."* The counters are in `GsStats` and printed by `research/gsql/scale.c`; what they found is §5, and the one change it led to is §6.

## 1. The two questions

From `gsql_scale_measurements.md` §1, through the whole stack in one process (`research/gsql/scale.c`):

- **Load rate halves between 10⁷ and 10⁸:** 172 000 rows/s against 90 000, with memory flat and the data in the page cache, so it is the engine's own write work.
- **Point-read p99 jumps** from 22 to 504 µs at 10⁸ while the median moves from 17 to 26 µs, also with everything cached.

## 2. Why a profiler is not the tool here

`perf` is unavailable (`perf_event_paranoid` is 4 on this machine, and is not to be changed), and valgrind's callgrind runs some 50 times slower, so a 10⁸ load would take more than a day. The engine is single-threaded and its expensive work happens in a few known places, so it can say where its time goes itself.

## 3. What is counted

`GsStats` gains counters, each kept where the work happens. They cost a clock read per compaction slice and per sync, not per row:

| counter | where | answers |
|---|---|---|
| bytes written by flushes; by compactions, per output level | `flush_step`, `comp_step` | **write amplification:** bytes written to disk per byte of commits, and which level writes them |
| bytes read by compactions | `comp_step` | how much merging is rewriting what is already sorted |
| compactions begun, and trivial moves | `comp_start` | whether a sorted load is moved down without being copied, as it could be |
| time in flushes, in compactions, in log syncs | the same, with the monotonic clock | the commit's time split three ways, the rest being SQL and the table layer |
| commits that stalled on level 0 | `maybe_compact` | whether the writer waits for compaction |
| per point read: files consulted, filter rejections, blocks read | the get path | why a read in the tail is slow |

`scale.c` prints them per size, and the load in ten slices, each with its rate and its counters, so a change in rate is seen against what changed with it.

## 4. What would follow

- **Write amplification near one level's worth per level** means the tree is doing what a leveled tree does, and the cost is its depth. The remedies are known (a larger fanout, a larger level 1, tiering the bottom levels) and each is a trade against read cost.
- **No trivial moves in a sorted load** means files are being rewritten that need not be. The likeliest cause is a level-0 file spanning more than its rows, as the schema's entry once made every level 0 file overlap everything (`gsql_spill_design.md` §7).
- **Reads in the tail consulting many files, or reading blocks the filter should have rejected,** point at level 0's depth during the load or at the filter.

Nothing is changed until the counters say which of these it is.

## 5. What they found

At 10⁶, 10⁷ and 10⁸ rows on ext4 (`/var/tmp/gsql-bench`), the data in the page cache, one process:

**The load does not slow down any more.** At 10⁸ each tenth of the load ran at 166 000 to 162 000 rows/s, the same as 10⁶ and 10⁷:

| per tenth of the load at 10⁸ | |
|---|---|
| bytes written per row | about 147: the log, and one flush of about 78 |
| bytes compactions wrote or merged | none: all 72 compactions were trivial moves |
| level-0 stalls | none |
| time in flushes and syncs | about 4.2 s of 61 |
| lookups per row | 2, all answered without touching a file |

So for a load in key order the tree does what it should: the files go down a level as they are. The halving §1 recorded (90 000 rows/s) predates the level-0 fix made for the index build (`gsql_spill_design.md` §7) and has not recurred. What a load spends its time on is the SQL and table layers, about 6 µs a row, which is the next place to look for load speed.

**The point-read tail is a file's first read after an open.** The first pass of reads after a reopen, each with its own counters:

| at 10⁸, first pass | mean | files opened | indexes read | pages read |
|---|---|---|---|---|
| the slowest 1% | 558 µs | 1.0 | 1.0 | 5.0 |
| the other 99% | 24 µs | 0.008 | 0.008 | 1.0 |

A sorted file's index and filter are read in at its first use (`file_open`), and the slowest 1% are exactly those reads. A second pass has no tail: its slowest 1% average 25 µs. A long-running owner pays this once per file. Loading every file's index at open would move the cost to the open instead (not measured); not done, since nothing here needs it.

**And a defect in the level walk, found by the range counter:** each SQL point read made 5 engine lookups, mostly the catalog's, whose keys sort below every row. On levels 1 and below, a binary search found the one file that could hold the key, and the loop then went on through every later file in the level, rejecting each on its range: 3 rejections a read at 10⁶, 87 at 10⁷, 937 at 10⁸.

## 6. What changed

- **The level walk stops at the one file the search found** (`lookup` in `src/gsql/lsm.c`). At 10⁸ a point read's range rejections went from 937 to 19.5, the remainder level 0's files, which overlap and must each be asked. The point-read median went from 27.5 to 18.8 µs. `store_test`'s sorted load counts the rejections for a key below every row: 2 with the fix, 42 without, bound at 8.
- **An unset status found by adding the counters:** `maybe_compact` returned `rc` unassigned when given no budget, the call a large transaction's commit makes. It answered whatever the stack held; with the stack under it changed, `CREATE INDEX` over a million rows failed its commit as `IO`, from no failing system call. Initialised. Valgrind, ASan and UBSan, gcc's `-fanalyzer` and `-Wall -Werror` all miss it on the old code, so it has no tier.

