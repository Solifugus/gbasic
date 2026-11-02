# G-SQL: where load time and the point-read tail go at 10⁸ rows

**Status:** Proposal 2026-11-01, work-order item 8 of `gsql_scale_measurements.md` §4: *"profile load at 10⁸ (§1, item 2) and the point-read tail (item 3)."* A measurement first; what it finds decides what is changed.

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
