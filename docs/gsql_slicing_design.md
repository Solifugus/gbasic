# G-SQL: a long read no longer stalls everyone else

**Status:** designed 2026-11-01. Matthew, the same day: concurrency for reads "will be very important, actually. Many simultaneous production users might otherwise quickly slow it down to a near halt for them."

## 1. The stall, measured

The owner is single-threaded (`gsql_owner_design.md` §6.1). `research/gsql/stall.c` measures what that costs, through the real owner:

- client A loads 10⁶ rows, then runs three long queries;
- client B, its own process, does point reads the whole time, recording each one's latency.

| long query, 10⁶ rows | its own time | worst point read issued during it | point read when quiet |
|---|---|---|---|
| `count(*)` with a condition | 0.64 s | **643 ms** | 20 to 23 µs median |
| `GROUP BY` | 0.90 s | **903 ms** | |
| `ORDER BY` no index gives, fetched to the end | 2.32 s | **2 044 ms** | median 62 µs during its fetches |

**A point read waits for the long query's whole blocking step.** Once a sorted result starts coming back, B interleaves: 1 129 point reads went through during the sort's fetches. The step before that is one piece. At 10⁸ rows the same waits become minutes (`gsql_scale_measurements.md` §1).

## 2. The mechanism: a statement that is still working

A read that has to fold or collect its input before answering **runs in slices.** Each slice consumes a bounded number of input rows, and the owner serves every other connection between slices.

- **Clients see no difference.** A request is answered when its first batch is ready, as now; only what the owner does in the meantime changes.
- **A slice ends at a row boundary.** All a fold's or a collection's state already lives in its cursor (the groups, the collected rows, the join's iterators), so stopping after N rows and resuming is resuming a loop, not reconstructing anything.
- **The service:**
  - `run` may answer **working**: `GsqlResult.working`, with the cursor holding the statement;
  - a new `resume(cursor, budget)` advances it by one slice and answers working again, or the first batch, or a refusal;
  - only a caller that asks, through `GsqlStatement.sliced`, is answered working. An in-process caller (the semantics tests, the benchmarks) is answered as before, the slices run back to back.
- **The owner:**
  - holds a working request on its connection, as it holds a parked one;
  - gives each working request one slice per turn of its loop, after answering whatever else is ready, round-robin;
  - never sleeps in `poll` while one is working;
  - can `CANCEL` a working request, which closes its cursor and ends its snapshot.
- **The budget is a row count:** 4 096 input rows a slice, about 4 ms at the measured million rows a second. A short query then waits at most one slice of each long read running beside it, rather than the whole read.

**What is sliced in this increment:**

- the fold of `GROUP BY` and the aggregates;
- collecting the rows of a sort no index gives, `DISTINCT` among them.

**What is not, and why:**

- **The sort itself,** over every collected row. It is one piece for now, and its share of the stall is measured once collection is sliced (§4). Slicing it means a merge sort that stops between passes.
- **Building a hash join's inner side and materializing a derived table or a `WITH` query.** These happen inside the join's start, a level below. They are a later step, measured first.
- **Writes.** A statement that writes holds the database's single writer whatever is done. Slicing it would let readers in, but not other writers, and needs the change list to be resumable. Not in this increment.
- **Set operations' sides** (`UNION` and the rest). Each side is collected whole. A later step.

## 3. What does not change

- A statement's answer: every result is exactly what it was, since the same loop runs to the same end.
- Its snapshot: a read sees the database as of its start, however many slices it takes.
- The protocol: no new message.

## 4. How it is tested

- **The semantics corpus and the differential tier** run unchanged. In process, every statement runs its slices back to back, so any difference in an answer is a defect.
- **The same corpus with a budget of one row**: every statement resumes at every row. This is the tier that would catch a fold or a collection that loses or repeats a row across a resume, where a budget of 4 096 would cross a boundary only in the larger cases.
- **The stall, through the owner, as a ratio** (`run_gsql_sql.sh`): B's worst point read during A's `GROUP BY` and during A's sort must be a small fraction of the long query's own time. A ratio is unchanged when the machine is uniformly slower. The tier also requires B to have read during A's query at all, since a B that never read would pass.
- **`CANCEL` of a working request:** the next request on that connection is answered, and the snapshot is released.
