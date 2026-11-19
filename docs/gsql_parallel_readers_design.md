# G-SQL: parallel readers

**Status:** Ruled 2026-11-18. Matthew accepted §7 as proposed ("yeah..
great"): option B, a reader forked per long read (Q1); the reads it takes
(Q2); the core count minus two, at least one, set per database (Q3); reads
inside a write transaction kept sliced for now (Q4); and the replica (D) as
its own item after B (Q5). Increment 1 is done (§5); the build is for
0.13.0. Matthew, 2026-11-18: parallel readers are "important". He said the
same of read concurrency on 2026-11-01 (`gsql_slicing_design.md`).

## 1. What slicing did, and what it did not

The owner is one thread (`gsql_owner_design.md` §6.1). Slicing
(`gsql_slicing_design.md`, shipped 2026-11-01) made reads **fair**. A sort, a
`GROUP BY` and the aggregates run 1 024 input rows at a time, and the owner
answers everyone else between slices. A point read then waits a few
milliseconds beside a long report instead of the whole report.

It did not make reads **parallel**:

- **One core.** Ten users running ten reports share one core. Each report
  takes about ten times as long as it would alone. A 22-core machine (this
  one) runs the database on 1 of them.
- **Some steps are still not sliced.** Building a hash join's inner side, a
  derived table, a `WITH` query and a set operation's sides each run as one
  piece. During that piece every other client of the database waits
  (`gsql_scale_measurements.md` §3).

Parallel readers would fix both: a long read runs on a core of its own, and
the owner stays free.

## 2. What holds the owner to one thread

- **The interpreter evaluates SQL's operators.** G-SQL's expressions are
  gBASIC templates evaluated by gBASIC itself (`gsql_sql_design.md`). A
  `Value`'s reference count is not atomic, and the evaluator has global state
  (the error model's pending error, warnings, the call depth). Making that
  thread-safe would touch all of `eval.c`. It is not a G-SQL change.
- **The storage engine is single-threaded by design.** The memtable, the
  block cache, versions and compaction have no locks. Flushes and compaction
  run as slices inside commits (`src/gsql/lsm.c`, head comment).

So more threads in the owner is the expensive route. More **processes** is
the cheap one, and the owner is already a process apart from every program.

## 3. The options

**A. Threads in the owner.** Ruled out by §2: the cost is a thread-safe
interpreter, and that is a project of its own.

**B. A reader forked per long read: the snapshot is the fork.** *(Recommended.)*
When a read turns out to be long, the owner forks. The child is a copy of the
owner at that instant: the memtable, the versions, the open cursor and the
statement's state so far. So it holds an exact snapshot without any snapshot
machinery. The sorted files on disk never change once written, and the child
reads them directly. It runs the statement to the end on its own core. The
owner goes back to serving everyone else.

**C. Reader processes that open the database from disk.** They cannot see the
memtable (commits not yet flushed), so they would read stale data unless they
replayed the log. That needs a cross-process protocol for what is flushed,
what is pinned and what is deleted, which is option B's bookkeeping made
harder.

**D. A replica for reports.** This is the read-only open that
`gsql_scale_measurements.md` §4 item 2 named after backup, and it was never
built. `gsql.backup` already refreshes a copy cheaply. A read-only open of
that copy would let reports run on another machine, a moment behind.
**It complements B rather than replacing it.** B uses the cores of the
database's own machine; D moves reports off it. §7 Q5 asks whether to do D
too.

## 4. Option B in detail

**Which reads are forked.**

- A read that is still **working after its first slice**. The slicing
  machinery already identifies a long read: today the owner answers it
  `working` and comes back to it each turn. Instead, the owner forks, and the
  child resumes the cursor where it stood. A short read never pays for a fork.
- **A plan with a step that cannot be sliced** (a hash join's inner side, a
  derived table, `WITH`, a set operation), when the planner's row estimate
  (`gsql_rowcount_design.md`) says the step is large. That read is forked
  before the step runs, so the unsliced steps stop blocking anyone.
- **Not, in the first increments:** a read inside a write transaction. It
  must see the transaction's own uncommitted rows, and the fork would carry
  them, but its spilled runs are files the owner can still change. That case
  keeps slicing until it has its own increment.

**How the child answers.** Through the owner. The child writes each finished
batch to a pipe, and the owner relays it on the client's connection, exactly
as if it had computed the batch itself. The wire protocol, the client and
`gsql.query` do not change. Relaying a batch is a copy, which is cheap beside
evaluating it. The connection is otherwise served as now; a request that
arrives while its read runs in a child waits, as it waits behind a working
read today.

**What the child may not do.** It is a reader and must never write:

- not the log, the manifest, the lease, or a sorted file;
- not compaction or a flush.

Its I/O layer is switched to read-only at the fork, and any write is a
refusal naming the reader (a tripwire, since a write there is a defect). The
child closes every client socket and the listening socket it inherited. The
database lock is the owner's; the child's copy of that descriptor is closed
without unlocking.

**What the owner keeps for it.** The child reads the sorted files of the
version current at the fork, so the owner holds a reference on that version
until the child exits. Compaction can still run, but a file the child might
read is deleted only once the child has exited. Versions already count their
holders (`refs` and `obsolete` in `src/gsql/lsm.c`), so this is one more
holder, released on `SIGCHLD`.

**Cancel, failure, death.**

- `CANCEL` kills the child.
- A child that crashes is a `LOST` read for that one client. The owner and
  every other connection go on.
- An owner that dies takes its children with it (`PR_SET_PDEATHSIG`, as
  every process gBASIC starts already does, `run_process_lifetime.sh`). Their
  clients see `LOST`, as for any lost owner.

**Spill.** A child spills into temporary files of its own. If it is killed,
anything left is garbage the next open removes: a file the manifest does not
name is deleted at open (`src/gsql/lsm.c`, head comment).

**How many at once.** Capped, with a default from the core count (§7 Q3). A
read past the cap is sliced in the owner, as today, so the cap degrades
service instead of refusing it.

## 5. What it costs, measured

A probe on this machine (22 cores, 62 GB), 2026-11-18. A process with a given
amount of memory touched forks a child, best of 7:

| resident | `fork` | 32 MB of parent writes while the child lives |
|---|---|---|
| 64 MB | 0.5 ms | 9.9 ms |
| 256 MB | 3.3 ms | 10.4 ms |
| 1 GB | 17 ms | 9.6 ms |
| 4 GB | 47 ms | 9.4 ms |

- **The fork** costs about 12 to 17 ms per resident GB (page tables, not
  data). An owner at its defaults holds a 64 MB cache and a 32 MB memtable,
  so its fork should be a few milliseconds. That is about one slice, paid
  only by a read that has already run longer than one slice.
- **Copy-on-write** costs the owner about 0.3 ms per MB it writes while a
  child is alive, as pages are copied on first write. A heavy writer beside
  many readers pays this.
- **Each child's memory** is its own once it writes: its cursor, its spill
  buffers and its cache misses. N readers can use up to N times a read's
  budget. This is the cap's real reason.

Those figures are from a synthetic process. **Through the real engine
(2026-11-18)**, `research/gsql/fork_readers.c` opens a store in process, loads
the rows, and runs `SELECT b, count(*), sum(c), min(d) FROM t GROUP BY b`.
That read touches every row, and the SQL layer folds them with gBASIC
evaluating each operator. It runs N times, one after another (one owner
thread), against N children forked at once, each running it on the state it
inherited:

| rows | resident | one read | N = 2 | N = 4 | N = 8 | fork |
|---|---|---|---|---|---|---|
| 10⁵ | 44 MB | 0.053 s | 1.74x | 3.20x | 5.34x | 0.8 to 1.1 ms |
| 10⁶ | 104 MB | 0.59 s | 1.73x | 2.31x | 3.38x | 3.6 to 4.0 ms |

(The N columns are throughput against serial: serial time over forked time.)

- **Every child answered exactly the parent's pre-fork result**, while the
  parent inserted 1 000 rows during each round. So the fork is a snapshot,
  as §3 claims: 0 wrong answers in 15 forked reads at each size.
- **The fork cost what §5's table predicted** for the resident size: about
  4 ms at 104 MB.
- **N = 1 is slightly slower forked** (0.93x): the fork and the pipe, paid
  only by a read that has already run longer than a slice.
- **The speedup flattens on this machine for a reason of its own.** It is an
  Intel Core Ultra 7 155H: 6 performance cores (12 threads, cpus 0-11) and
  10 efficiency cores (cpus 12-21), in a laptop whose clock drops as more
  cores are busy. Past 6 readers, the extra ones run on the slow cores. The
  machine was otherwise idle (load 1.7). A server with uniform cores should
  scale further; that is not measured here.

Not yet measured: the real owner, with batches relayed through it and point
reads beside the forked readers. That is increment 2's tier.

## 6. Increments

1. **A probe through the real engine.** *Done 2026-11-18, §5: forked readers
   pay for themselves (3.4x at 8 readers at 10⁶ rows on this laptop).* The
   owner-level measurement, extending `research/gsql/stall.c`, folds into
   increment 2. Fork the owner mid-statement and measure:
   - the fork at the owner's real resident size;
   - N concurrent `GROUP BY`s over 10⁶ rows, wall time against the same N
     sliced, for N = 1, 2, 4, 8;
   - the point-read tail beside them.

   This settles whether B pays for itself before any of it ships.
2. **Hand a working read to a reader.** After its first slice, with batches
   relayed through the owner, the version held until `SIGCHLD`, a read-only
   I/O layer, and cancel and death as §4 says.

   Tests:
   - differential: the same statements forked and sliced answer the same
     rows;
   - a reader's snapshot ignores commits made after it started;
   - a planted write in a reader is refused;
   - a killed reader leaves its client `LOST` and everyone else served;
   - an owner killed with readers leaves no process behind;
   - a compaction during a read deletes nothing the reader holds.
3. **Fork the unsliced steps up front** by the planner's estimate. Tested by
   `stall.c`'s point-read tail during a hash-join build: today that tail is
   the whole build.
4. **The cap and its setting**, and a status answer (how many readers are
   running and how many reads were sliced because of the cap), so a slow
   report can be explained.
5. **(If Q5 says yes) a read-only open** of a backup copy: a replica for
   reports.

## 7. Questions for Matthew

- **Q1. The mechanism:** B, a reader forked per long read? My recommendation
  (§3): it is the only option that uses more cores without making the
  interpreter thread-safe.
- **Q2. Which reads:** a read still working after its first slice, plus an
  unsliced step the planner expects to be large (§4)? Or should a program also
  be able to ask for a reader explicitly?
- **Q3. How many at once, by default:** I'd say the core count minus two (the
  owner and the programs need cores too), at least 1, set per database when
  it is opened.
- **Q4. Reads inside a write transaction:** keep slicing them, as §4
  proposes, and give them a later increment of their own?
- **Q5. A replica for reports (D):** after B, as its own item, or not at all?

**Not affected by this design:**

- Writes stay in the owner, one at a time, with group commit.
- Windows has no G-SQL today, so nothing changes there.
