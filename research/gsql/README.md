# G-SQL Phase 0a spikes

The storage-engine candidates of `docs/gsql_storage_design.md`, built small enough to measure. **Spike code**:
- nothing here is linked into `gbasic`;
- nothing here is run by `tests/run_all.sh` (§1 of the design says so);
- a written-off candidate is deleted, with its numbers recorded in the design.

The shared pieces they stand on, the I/O layer and the crash harness, are in the tree proper (`src/gsql/`) and gated by `tests/run_gsql_io.sh`.

**Since step 5 (2026-10-29) the chosen engine is in the tree proper too.** §2.C moved to `src/gsql/lsm.c`, the interface to `include/gsql_store.h`, the shared field helpers to `src/gsql/store_common.c` and `store_util.c`, and the functional test and the crash adapter to `tests/gsql/`. It is gated by `tests/run_gsql_store.sh`. What stays here is 2.A (retired), 2.B (the fallback) and the bench, and `run.sh` still builds 2.C from its new place beside them, so the fallback is held to the same tests as the choice.

```sh
research/gsql/run.sh              # build, test and crash-gate every candidate (~35 s per engine)
GATE_SCALE=quick research/gsql/run.sh
```

## Files

| file | what |
|---|---|
| `../../include/gsql_store.h` | the §1 storage interface as a vtable: one functional test, one crash adapter and one workload harness drive every engine. `store.h` here adds the research engines' names |
| `../../src/gsql/store_common.c` | little-endian fields, CRC32C, varints, the file header and the page header (§3.1) |
| `btree.c` | **one** B+tree over slotted pages and a small pager interface, shared by §2.A and §2.B |
| `btengine.c` | above the pager, shared by §2.A and §2.B: the catalog of named trees, a transaction's view of it, records, cursors and cursor invalidation |
| `cow.c` | **§2.A**, the copy-on-write B+tree (retired) |
| `wal.c` | **§2.B**, the B+tree with a write-ahead log (the fallback) |
| `../../src/gsql/store_util.c` | page lists and sets, and a page cache keyed by (page, version) |
| `../../src/gsql/lsm.c` | **§2.C**, the log-structured merge tree (chosen; moved in step 5) |
| `test_btree.c` | the B+tree against a model, over an in-memory pager in both modes |
| `../../tests/gsql/store_test.c` | the interface against a model, for any engine |
| `../../tests/gsql/crash_store.c` | an engine under the crash harness |
| `hardreq.c`, `hardreq.sh` | Test 3: the hard requirements, every claim checked by the program |

## Why §2.A and §2.B share one tree

The comparison is meant to measure the engines, not two independently written trees (§3).

- **The pager decides what "write this page" means.** Under copy-on-write, a page not yet written in this transaction moves to a new number and its old one is freed, so every ancestor must follow. Under a write-ahead log, the page keeps its number, so the climb stops as soon as nothing above needs to move.
- **The tree code is otherwise identical.** That includes splits, merges, overflow chains and cursors, and the leaf links that §2.B has and §2.A cannot.

## What building it found, so far

- **A 4 KiB page cannot hold §1.2's 1,024-byte key.** §3.1.3 requires every page to hold four cells. A quarter of a 4 KiB page body, less the slot and the worst leaf cell's overhead (a value in overflow pages), is 984 bytes, so the key limit is the smaller of 1,024 and that. It is 1,024 at 8 KiB.
- **2.A's meta slots are pages 1 and 2, not slots in page 0's root area.** At a 512-byte page, page 0 is one sector, and two slots inside it could not be written independently (rule 3).
- **The header checksum covers only page 0's fixed 76 bytes.** It sits at bytes 76 to 80, the end of the unused key-check block. Each candidate's root area changes after creation, so a checksum over the whole of page 0 would go stale on every commit.
- **2.A's free list is a tree keyed by the commit that freed each page**, as LMDB has it and as §3.1.4 proposed.
  - **The first version was a blob, BoltDB's shape**, rewritten at every durable commit to pages taken from the end of the file. Its cost grew with the list, and the list grew with its own old copies. A 20,000-commit YCSB-A run **wrote 419 GB and filled this machine's disk** before it was stopped. `bench.sh` now caps every file at 20 GB and will not start a run with under 50 GB free.
  - **How it works now:** a commit adds one record, its freed pages under its commit id. A write transaction takes reusable records out of the tree at its start, and again whenever an allocation finds none left (LMDB's lazy refill). Pages it took and did not use go back under key 0. While the tree itself is being written it is never refilled from (a nested take would edit it mid-edit), but it does use pages already taken out of it.
  - **Building it found four defects**, all as leaks reported by `check`, or as a settle loop that never ended:
    1. a record long enough to overflow freed its own chain on each rewrite, and the freed pages joined the list being written. Records are now chunked so they always fit inline;
    2. deleting and re-inserting the chunks merged and split nodes every round. Chunks are now replaced in place;
    3. a transaction took only part of key 0, and the commit's rewrite then overwrote the chunks it had not taken;
    4. a nested take re-loaded records the outer one had not yet deleted.
  - **A fifth defect survived every test until the benchmarks ran long enough: the file grew without bound.** Writing the free tree only at the end of the file meant each commit grew the file by the tree's own path copy (two or three pages) and handed as many pages back to a list that only grew. The file reached 9.4× the data after 40,000 updates of 20,000 records, synced or not, and passed the 20 GB cap on a million. `check` saw nothing, since every page was accounted for, just on a list nobody drew from. The first write-up's 2.A space figures (5.9 on YCSB-A, 1.8× growth in the recovery run) were this, not a page-fit cost or orphaned allocations.
  - **The fix needed the settle loop to change twice, and each wrong version was caught:**
    - Comparing the lists' LENGTHS let a round take page X for the tree's own write while a page freed in passing came back. The length was the same, so the loop stopped with key 0 listing X, now live, as free. A later reuse overwrote it, and `test_store` failed 19 checks while the crash gate reported 27 violations in two modes.
    - Comparing MOVEMENT instead never terminated. Key 0 at 28 pages needs a second chunk, writing it takes two pages, at 26 the surplus chunk is deleted, the merge frees the two, and it is 28 again. That is LMDB's freelist oscillation, met as a commit answering CORRUPT after 32 rounds.
    - Only round 0 may take from the reusable pages now. Later rounds allocate from the end, so the list can only grow and the loop is monotone. The file now holds at 1.00× over a million updates.
  - **Both reuse conditions are proven by perturbation.** "Durable" is caught by the crash tiers (115 power and 96 torn violations); "no older snapshot" by `test_store` (222 failures).
- **At 4 KiB pages, a YCSB record overflows.** A 1,000-byte value plus a 23-byte key makes a cell of about 1,029 bytes, over the quarter-page inline limit of 1,004, so each record takes its own overflow page, about 25% full. Load space amplification is 4.2. The design names this threshold as a measurement (§3.1.3); SQLite's inline limit at 4 KiB is nearly the same.
- **A `sync = false` commit in 2.A writes its pages but no meta page.** The next sync, or the next synced commit, writes one meta covering every commit since (group commit). A page freed by a commit is reusable only once that commit is durable and no open snapshot predates it.
  - Dropping the "is durable" half was caught at 90 violations in 504 crashes. It is what keeps a fall-back to the older meta safe.
- **2.A cannot keep a commit that had not returned durable**, because only a meta page makes a commit recoverable. A crash keeps one only when it hits that meta write itself.
- **Two meta slots matter only when the meta page's live bytes span sectors.** In a light workload the header, the roots and the inline free list fit in the first 512 bytes, the rest of the page is zero in both versions, and any tear yields one valid page or the other. So writing every meta to one slot passed 6,000 torn crashes at 512 B and at 4 KiB.
  - The perturbation is caught only by a delete-heavy workload, whose free list crosses a sector boundary. That is why `run.sh` has a heavy torn tier.
- **The crash harness gained flush sampling**, in the tree (`gsql_io_sim_flush_points`). Sampled uniformly, a run of thousands of operations rarely lands on the one flush that makes a meta page durable. Half the crash points are now drawn from flushes.
  - Measured on 2.A: unsynced commits kept by a crash rose from 150 to 636 in the same run, so far more of the oracle's range is exercised.
  - The single-slot perturbation went from 1 violation to 5.

- **2.B's checkpoint copies only durable frames**, and none past the oldest open reader. Copying a frame of a commit that is not yet durable puts a page into the main file that a crash can leave without its log; that perturbation was caught at 588 violations in 9,153 crashes.
  - The log is reset (a new salt and epoch, then truncated) only when every frame is in the main file. The reset's own test that no reader needs the log is implied by the checkpoint's limit, so a perturbation removing it is legitimately green; it stays as a guard.
- **2.B's log never reset under group commit**, so it grew to 166× the data over a million group-64 updates. A checkpoint ran only at a commit, and the reset needs every frame durable, which the commit that triggers a checkpoint almost never is under group commit. A sync now checkpoints once the log is a checkpoint long. Measured both ways:
  - testing instead for "a checkpoint's worth is un-backfilled" left the growth exactly as it was (8.4× at 40,000 updates), because the commit-time checkpoints keep that count under the threshold;
  - fixed, the file holds at 1.00× and group-64 rises from 16,600 to 21,000 updates/s.
- **2.B's creation writes the header last.** The state page and the catalog are written and flushed, and only then the header. The other way round, a power loss kept the header and lost what followed, and recovery found a database it could not read. The crash harness found this twice: first a missing state page, then, once that was checked, a missing catalog. The fix is the write order, not more checks.
- **A commit in 2.B answers IO if the checkpoint after it fails a flush**, even though the commit itself is durable. Answering OK hid that the store had become unusable, and the harness found it. IO is the honest answer, and it is safe, since a commit reported as failed may be either kept or lost.
- **2.B keeps commits that had not returned durable far more often than 2.A**: in 12,893 of 18,672 power-loss crashes in the first run, against a few hundred for 2.A. Its log frames can land and stay valid without a flush, while 2.A recovers only what a meta page names.
- **The crash harness gained a continuation stage** (§5.3: "a further workload runs, crashes and recovers correctly"). After recovery it runs a continuation of mixed synced and unsynced commits, crashes it at a seeded point, and judges the result against the recovered state plus a prefix of the continuation.
  - The single durable commit it had before could not see an engine that accepts **stale log frames**: the frames a lost commit leaves behind, partly overwritten later. Removing 2.B's checksum chain passed 44,000 crashes. With the continuation it is caught (4 power and 1 torn violations in 45,000 crashes, and 5 and 9 in 350,000 at rare checkpoints), with the correct engines clean.
  - It is a rare case, because each log reset changes the salt and invalidates stale frames anyway.

- **2.C must flush the directory before renaming a manifest into place, as well as after.** A compaction creates new sorted files, then renames a manifest naming them. Pending namespace operations survive power loss as any subsequence, so the rename could survive while the creations did not, and recovery found a manifest naming files that were not there. The crash harness found it, as both a recovery failure and a continuation coming back as no prefix. It costs one extra directory flush per manifest write. A journalling filesystem would usually preserve the order, but rule 2 does not promise it.
- **2.C's readers pin the version they began with**: the files and the memtable. A flush makes a new version with an empty memtable, so an older reader keeps the old memtable and the old files, which between them hold the same data.
  - The first draft cleared the memtable in place, freeing nodes an open reader was pointing at.
  - Because of the pinning, compaction's "keep every version a snapshot needs" rule was not load-bearing: perturbing it passed every test. Test 3 found it was pure cost (a held reader kept every version written after it) and replaced it with the last durable commit as the horizon. What pinning does NOT cover is a reader that has not begun: it starts at the last durable commit, so a retired tree must survive until its retirement is durable (Test 3, below). The cost of pinning is the data at the snapshot while a reader is held, the same shape as 2.A's pinned pages.
- **A flush in 2.C makes unsynced commits durable**, since their data is now in a flushed file. So a fresh snapshot may see more than the last synced commit. The functional test's model accepts the durable or the committed state, and treats the latter as now durable.
- **2.C meets G5 by spilling to sorted runs.** Past half of `cache_bytes`, a write transaction's buffer becomes a sorted run file. It is written and flushed, but no manifest names it yet. Its entries carry a placeholder sequence, and the file carries an override in a private range that only the writer sees.
  - A transaction that spilled commits by flushing the memtable, spilling the rest of its buffer as a last run, and naming every run in one manifest with its final sequence. The manifest's rename is the commit, so no part of the transaction can survive without the rest. That is why the remainder is not written to the log beside runs named earlier: that would have allowed half a transaction to survive a crash.
  - Such a commit is always durable, whatever `sync` said. A spill rebuilds every open cursor's sources, which is transparent, since a spill changes no tree.
  - In one crash workload the path ran 1,658 spills and 865 run commits under every crash point.
- **2.C has a block cache**: decoded blocks keyed by (file, block), each one allocation, with a block an iterator stands on pinned against eviction. It cut the functional test's block reads from about 33,000 to about 3,300.
  - Building it found an uninitialised block, freed on a read error, which is the error a crashed disk produces.
- **2.C needs several files**: a manifest, logs, sorted files and a lock file. That is not "one file plus at most a log" (§7 question 6). Folding them into one needs an extent allocator the other two do not have.
- **2.C takes the full 1,024-byte key at any block size.** It has no page-fill constraint, where 2.A and 2.B cap at 984 bytes at 4 KiB.

- **On this machine a flush that also commits a change of file size costs 4×.** Appending 4 KiB and calling `fdatasync` takes about 1 ms (p50); overwriting in place takes 250 µs. All three candidates first appended to growing files, and SQLite commits ran about 4× faster for that reason, not for any reason of design. So:
  - 2.A grows its file in zero-filled chunks;
  - 2.B no longer truncates its log at a reset;
  - 2.C zero-fills each new log.
- **Those changes reopened three crash holes, each found by the harness:**
  - **"Creation never finished" is decided by commit id, not file size.** A grown file made an unfinished 2.A store 65 pages long, so the old test stopped firing. Now no valid meta above transaction 1 means nothing was ever committed.
  - **2.B's reset salt must not repeat.** Computed from the old salt, a reset whose header a crash lost was repeated with the same salt, and that epoch's untruncated frames read as valid again.
  - **Recovery must remove every stale record past the committed end, not just the first.** A crash can leave a lost commit's frames behind a torn one. A transaction run again writes byte-identical frames at the same positions and continues their checksum chain, and the lost commit after it comes back. A first fix stopped at the torn frame and missed the ones behind it. Both 2.B and 2.C now truncate at recovery (2.C then refills). **The pushed 2.B already had this hole latently**: it never truncated at recovery.
- **Randomness is input from the environment, so it comes through the I/O layer** (`gsql_io_random`). The real layer reads `/dev/urandom`; the simulated one is seeded, differently in each crash image, so a crash run replays exactly.

## The 2.C rebuild (2026-10-25 to 26)

§6's confirming tests left 2.C four items: value separation (W8), larger levels and a trivial move (W4), and compaction off the commit path (W3's p99). All four are in `lsm.c` now.

- **Larger levels.** The default memtable is 32 MB, up from 4 MB, and level sizes follow it (level 1 is ten times the memtable). At 4 MB a 10 GB store was five or six levels deep, and a random load rewrote every row at each.
- **A trivial move.** When nothing below overlaps, files go down a level as they are, unrewritten.
- **Compaction in slices.** A compaction's state lives in the store and runs a little in each commit, a budget of 32 times what the commit wrote. If level 0 backs up past three times its trigger, the merge in progress is finished outright, which is RocksDB's write stall. A pausing compaction's outputs are listed as garbage in every manifest written meanwhile.
- **Value separation.**
  - **Blob files.** Values of 4 KiB or more go to a blob file written beside the sorted file they are flushed with. The entry holds a 24-byte pointer carrying the value's checksum, and compaction copies the pointer, never the value.
  - **Deletion.** A blob file goes when the last record pointing into it is freed as obsolete, never at close; an early draft deleted every blob at close. The manifest lists live blob files, so an open can delete one whose deletion a crash undid.
  - **Relocation.** Compactions rewrite values out of the oldest quarter of blob files, so their space comes back. *(Replaced 2026-10-27 by relocation from any blob file at least half dead, and a trigger: "Blob space by dead fraction", below.)*

**What the rebuild found:**
- **A durable commit could be lost, and the defect was there from the start.**
  - **The mechanism.** An open replays every log from the manifest's onward into one memtable. The manifest then recorded the last of those logs as where replay starts, so a manifest written before the next flush dropped the earlier logs' commits from every later recovery.
  - **Why it stayed hidden.** Nothing wrote a manifest in that window until compaction began installing between flushes.
  - **The fix has two parts.** The manifest records the oldest log the memtable needs, and a flush deletes every log the memtable held, not just the last one.
  - **What the gate catches.** On the workload that found it, each part alone is enough: reverting either leaves the gate green, and reverting both reproduces the loss. So the gate guards the pair, and neither part on its own.
- **Three file leaks, all already in 2.C, found by a new check.**
  - **The check.** The crash adapter now checks every recovered store, and 2.C's check requires every numbered file on disk to be named by something. Before this, recovery was only read back, which cannot see a file nothing names.
  - **What it found:**
    1. a large transaction's spilled runs, orphaned by a crash between the flush that precedes its commit and the commit's own manifest;
    2. a file whose writing failed on a full disk, closed and kept;
    3. a flush whose manifest failed, its finished file named by nothing.
  - **Each grew the store without bound.** A fourth "fix", for deletions lost to a crash, was written on a theory and taken out when reverting it alone stayed green: the manifest that first omits a deleted file makes its deletion durable.
- **Sorted and blob files share one number space**, and a number may be taken again after a crash. A stale blob file therefore survived beside the sorted file that took its number. Taking a number now clears the other kind's file. The check had been blind to this too: it treated a number as accounted for when either kind was named, and now it judges each kind on its own.

**Measured after the rebuild** (`results/2026-10-26/`, 2.C only; the other engines' figures are the earlier rounds', on the same machine and harness). One run each.

| | before | after | best reference |
|---|---|---|---|
| W3 rows/s, NVMe | 20,667 | ~~**242,199**~~ 40–52 k at the same size (corrected 2026-10-29, below) | RocksDB 92,645 |
| W3 p99, NVMe | 724 ms | **4.3 ms** | |
| W3 rows/s, larger than memory | 9,030 | 30,945 | RocksDB 107,340 |
| W3 p99, larger than memory | 1,851 ms | 150 ms | RocksDB 35 ms |
| YCSB load | 72,983 | **177,641** | RocksDB 172,929 |
| W4 sorted, 10 M rows | 52,702 | 130,858 | LMDB 185,089 |
| W4 random, 10 M rows | 14,336 | 27,977 | 2.A 36,326 |
| group-64 | 46,943 | **85,459** | RocksDB 55,217 |
| W8, 100 KiB values | 743 | **2,081** | LMDB 2,043 |
| W8 space amp | 1.21 | **2.91** | LMDB 1.04 |
| recovery, NVMe | 8.4 ms | 53 ms | LMDB 0.23 ms |
| recovery, flash stick | 81 ms | **1,279 ms** | LMDB 0.21 ms |

- **Corrected 2026-10-29: the W3 row compared two sizes.** 242,199 rows/s was a 10⁵-record run (20,000 rows inside one memtable, write amp 1.01), while 20,667 and RocksDB's 92,645 were 10⁶. At 10⁶ the rebuild's build (`83f5b75`) gives 42–52 k rows/s, run again for step 4, against 99 k for RocksDB on the same day: a 2 to 2.5× gain, not 12×, and RocksDB still leads W3 by 2×. The p99 at 10⁶ is 100–120 ms, not 4.3 ms. Every 2.C build since gives the same, so nothing regressed; the figure was wrong when written.

**What it cost:**
- **Recovery time grows with the memtable.** A 32 MB memtable means up to 32 MB of log to replay, which takes 1.3 s on the flash stick.
- **Space at 100 k records rises to 1.62**, mostly the log's 64 MB of zero prefill counted in the file size. At 10 M records it is 1.04, as before.
- **Point reads are 10 to 20% lower** on YCSB-C, W1 and YCSB-F, and 22% on W1 larger than memory. These are single runs, so part may be noise.
- **W8's blob space is not coming back fast enough.** Relocation only touches the oldest quarter of the blob files, and only when a compaction passes through, so overwritten large values keep their space. It needs a trigger based on how much of a file is dead. The crash gate reached relocation in only 5 cases. *(Done 2026-10-27: "Blob space by dead fraction", below. W8 is 1.96.)*
- **A flush still runs inside the commit that fills the memtable.** That is most of W3's remaining p99 larger than memory (150 ms against RocksDB's 35). *(Wrong about the share, measured 2026-10-27: a flush is about half of a p99 commit, and a compaction slice of the same size lands in about a third of all commits. The flush is off the commit path now: "The flush off the commit path", below.)*

## Test 3: the hard requirements (2026-10-26)

§6 point 2's hard requirements, checked against 2.C with 2.B alongside and 2.A as a reference. `hardreq.c` drives every candidate through `store.h`; `hardreq.sh` runs the set and writes `results/2026-10-26/results-hardreq.jsonl`. **Every claim a line makes is checked by the program, not inferred.** A G1 line saying the old snapshot kept the old data has scanned that snapshot. A backup line saying the copy matches has reopened the copy and compared it. A recovery line has read back the last durable commit. One run each, NVMe, 100-byte values.

**The verdicts, after the fixes below:**

| requirement | 2.C | 2.B |
|---|---|---|
| G4: read-only enforced by the engine | **met** | **met** |
| G1: atomic replacement under a live reader | **met**, the swap is O(1) | met, the swap is O(rows) |
| bounded recovery on a long log | **met**: bounded by the memtable | met only with no reader held |
| held-snapshot space | **met**: the data at the snapshot, and no more | the log grows with every write while a reader is held |
| online backup without stopping writers | consistent; **not met as built** (one blocking call) | consistent; not met as built |

**Measured** (after the fixes; 2.A as a reference only):

| | 2.A cow | 2.B wal | 2.C lsm |
|---|---|---|---|
| G1 at 10^6 rows: the swap call | 141 ms | 102 ms | **0.004 ms** |
| G1 at 10^6 rows: the replacing commit | 172 ms | 160 ms | 405 ms |
| G1: space with the old reader held / after release | 253 / 253 MB | 125 / 250 MB | 276 / **138 MB** |
| backup of 10^6 rows (writers wait for all of it) | 154 ms | 157 ms | 326 ms |
| 20 k updates, held snapshot: peak space | 590 MB | 167 MB of log | **0.2 KB** (all in the memtable) |
| 2 M updates of 1 k keys, held snapshot: peak / after release | | | **2.5 / 1.4 MB** |
| 200 k updates, held snapshot: 2.B's log | | 1.66 GB | |
| recovery after 10^6 commits, no reader | | **3.1 ms** (4.2 MB log) | 103 ms |
| recovery after 10^5 commits, a reader held | | 656 ms (827 MB log) | **122 ms** |
| recovery after 10^6 commits, a reader held | | not run (would be ~8 GB) | **99 ms** |

- **2.A's file never shrinks**, and a held snapshot costs it 30 KB a commit; nothing new, 2.A being retired.
- **2.B's space after G1 is two datasets** because the B+tree reuses freed pages rather than shrinking the file; the old pages are free, not lost.
- **With no reader held, 2.B recovers fastest** now that its log resets. 2.C's recovery is bounded by the memtable (up to 32 MB of log) at about 100 ms, whatever the history and whatever is held.

**What it found, in order of importance:**

1. **A long reader cost 2.C space in proportion to writes, not keys, and the rule behind it also lost data.**
   - **The rule.** Compaction kept every version newer than the oldest open snapshot, and dropped a retired tree's records once the retirement was older than it (or than the last commit, with no snapshot open).
   - **Too much kept.** Every reader pins the version it began with, the files and the memtable (above), so a snapshot never reads a compaction's outputs. The kept versions were readable by nobody. 2 M updates of 1,000 keys under one held snapshot left 301 MB after the release; the held snapshot needed 1.4 MB.
   - **Too little kept.** A new reader begins at the last *durable* commit (§1.3), not the last commit. The dead-file sweep this test added (item 3) was written with the same rule, and dropped the files of a tree whose drop was not yet durable: a new reader saw 78 of 300 of its records. Compaction's copy of the rule was not reached by any workload tried, and is fixed by the same argument.
   - **The fix is one function, `horizon`: the last durable commit.** A new version serves only readers that have not begun yet, and they begin there. The README's earlier note, that the snapshot rule was "not load-bearing", was right, and stopped short: not load-bearing meant pure cost.
2. **2.B's log never reset under group commit written as "every Nth commit syncs".** The log resets only when every frame is durable. Test 1 fixed this for `sync()`, but a synced *commit* is the same moment and did not get the same rule. Over 10^6 commits the log reached 8.3 GB and took 6.1 s to recover. A synced commit now checkpoints by `sync()`'s rule.
3. **A replaced dataset kept its space in 2.C until a compaction happened to reach it.** A 10^6-row dataset's 138 MB survived its replacement, the reader's release and 2,000 further commits. A sorted file whose every record belongs to a retired tree is now dropped whole (`sweep_dead`).
4. **A flush in 2.C wrote every version in the memtable.** For 1,000 hot keys, level 0 held about 100 MB of shadowed versions. A flush now writes only each key's newest version. By the horizon argument nothing can read the others.

**Two blind spots in the suites, both closed.**
- **No workload had ever put two versions of one key in a memtable when it flushed.** The crash workloads write 40 to 60 transactions over 200 to 3,000 keys, and `test_store`'s store flushes a 40-byte memtable on every commit. So a flush keeping the *oldest* version of each key passed both, while a direct read went stale after 25 commits. Two additions catch it: `test_store`'s `hot_keys` (five keys, 3,000 commits, a snapshot held throughout, every read checked) and two hot-key crash specs in `run.sh` (300 transactions over 6 keys).
- **The crash gate never dropped or replaced a tree**, so `tree_replace` had never been crashed in any engine. The adapter now commits one transaction in four by a whole-tree refresh: copy the tree, apply the puts there, `tree_replace`. The model is unchanged, and the choice comes from the transaction itself, so every replay makes it the same way. The sweep fires in the gate (about 1,500 files dropped in one seed range). It never fires there with a retirement still unsynced, so the visibility rule is held by `test_store`'s `dropped_not_durable`, not by the gate.

**What is left:**
- **Online backup.**
  - **As built.** Both candidates' backups are consistent: a backup taken while commits continue holds exactly the snapshot. But each is one blocking call, and the owner is single-threaded, so writers wait for the whole of it, about 2.3 ms per MB for 2.C (a logical rewrite) and 1.3 ms per MB for 2.B (a page copy).
  - **Meeting the requirement needs a backup taken in slices between commits**, which is an interface change (`backup_begin`, `backup_step`, `backup_end`).
  - **The two designs pay for slicing differently, and that is the structural result.** A sliced 2.C backup pins its snapshot's immutable files, a cost bounded by the data at the snapshot. A sliced 2.B backup is a long reader, so its log grows with every write made during the backup, and its recovery time grows with the log.
  - **A hard-link backup**, which §6 expected for 2.C, needs a link operation the shared I/O layer does not have.
- **2.B's long-reader cost is structural**: an open reader stops the log reset, so recovery time follows write volume for as long as the reader lives. SQLite has the same property ("checkpoint starvation").

## Test 4: the sliced backup, and open question 6 (2026-10-26)

Test 3 left online backup unmet by both candidates as built, because each was one blocking call in a single-threaded owner. Test 4 builds the backup in slices and measures what open question 6 turns on: how many files a database is. `SET=test4 hardreq.sh` writes `results/2026-10-26/results-test4.jsonl`.

**The interface.** `backup_begin(snapshot, dest)`, `backup_step(job, budget_bytes, &done)` and `backup_end(job)`. Ending a job that is not done discards the partial copy. The one-call `backup` is now begin, steps and end, so one path serves both.
- **2.C's backup is physical now.** It copies the snapshot's pinned files as they are, in chunks, then writes the snapshot's memtable view as one more level-0 file, then the manifest. The files cannot change or go while the copy runs, because they are immutable and pinned.
- **2.B copies pages through the snapshot**, a slice at a time, into a temporary file renamed into place.
- **2.A, retired, does the whole copy in one step.**

**Measured, 10^6 and 10^7 rows, one group-committed write between every two slices** (each copy reopened and compared with the snapshot):

| | 2.B, 10^6 | 2.C, 10^6 | 2.B, 10^7 | 2.C, 10^7 |
|---|---|---|---|---|
| one step (the old monolithic backup) | 161 ms | **73 ms** | | |
| 1 MB slices: median / p99 / longest slice | 2.0 / 3.1 / 3.2 ms | 1.3 / 7.3 / 7.4 ms | 1.9 / 3.1 / 3.7 ms | 1.2 / 2.1 / 6.2 ms |
| 1 MB slices: longest commit during the backup | 1.1 ms | 0.4 ms | 1.4 ms | 0.4 ms |
| 1 MB slices: whole backup | 261 ms | 252 ms | 2.5 s | 1.7 s |
| space growth while the backup ran | +0.5 MB, the log (120 commits) | **none** | +9.5 MB, the log (1,192 commits) | **none** |

- **The requirement is met by both**, at a few milliseconds a slice. What differs is the cost of the open snapshot. 2.C's pins files it already had. 2.B's is a reader, so its log grows about 8 KB with every commit made during the backup, and the backup's length is set by the data size. A 100 GB 2.B backup taken under a steady write load holds the log open for its whole duration.
- **2.C's physical copy is 4.5× faster than the logical rewrite it replaced** (73 ms against 326 ms in one step).

**Found by building it:**
- **2.C's logical backup copied every retired tree into the copy, including trees retired after the snapshot.** In the copy, such a tree is live in its catalog and marked retired in its manifest, so the copy's own compactions would drop its data once its sequences passed the retirement. `test_store`'s `sliced_backup` reproduces it on the old rule: 173 of 200 records left after 1,500 commits in the copy. The copy now takes only retirements at or before the snapshot.
- **One slice ran far longer than its budget, three ways.**
  - A slice copied a whole 1 MB chunk whatever its budget, so 256 KB and 1 MB budgets made the same 131 steps.
  - 2.C flushed a copied file only when it was finished, about 6 ms for 64 MB.
  - 2.B flushed the whole copy at the end: one 228 ms step for 1.25 GB.
  - Each slice now copies at most its budget and flushes what it wrote.
- **An interrupted backup could leave files that a later one cannot find.** The I/O layer has no directory listing, and file numbers are kept from the source, because a blob pointer names its blob file. `begin` therefore writes a durable `dest.pending` record of the highest number the copy may use, before anything else. Clearing a destination reads it and any old manifest, and removes the manifest first, so the place never holds a mix of two backups that looks like one.
- **Three perturbations are caught**: copying every retirement, not clearing the destination, and copying memtable entries newer than the snapshot. The last was green until the test compared every key's version against a model kept by the test, not record counts. The crash gate does not exercise backup.

**Open question 6: how many files a database is.** Random-order loads, 100-byte values:

| rows | 2.A files | 2.B files | 2.C files (sorted) | 2.C descriptors after 2,000 reads | 2.C reopen |
|---|---|---|---|---|---|
| 10^4 | 1 | 2 | 3 (0) | 2 | 30 ms |
| 10^6 | 1 | 2 | 6 (3) | 5 | 41 ms |
| 10^7 | 1 | 2 | 22 (19) | 21 | 66 ms |

- **2.C is a manifest, a lock file, a log and one sorted file per ~64 MB**, plus blob files for long values. A 1 TB store would be about 16,000 sorted files.
- **Sorted files stay open once read**, so descriptors follow the file count. At 1 TB that exceeds the default soft limit of 1,024 per process. RocksDB caps open files with a handle cache (`max_open_files`), and 2.C needs one. That is engineering, not structure.
- **The layout as built scatters siblings beside the path** (`db`, `db.lock`, `db.log.13`, `db.42.sst`). Two databases in one directory interleave their files, deleting one needs a glob, and copying `db` alone copies only the manifest. Opening such a copy fails, loudly, because the files it names are missing.
- **2.B has the quieter form of the same hazard.** Copying the main file without its `-wal` opens cleanly and has silently lost every commit not yet checkpointed, the SQLite trap.
- **One file for 2.C needs an extent allocator inside the file.** That is a second space manager, with hole punching or a second compaction to give space back, which neither B+tree candidate needed and 2.C was built without.
- **Recommendation: a database is a directory**, holding its manifest, lock, logs and data files, as RocksDB, LevelDB and PostgreSQL do. Copy, move and delete each become one operation: `cp -r`, an atomic rename, and the `remove_tree` gBASIC already has. Nothing in a directory belongs to another database. For 2.C it is a naming change, not a new mechanism.

## The directory layout (2026-10-26)

Open question 6 is decided: **a database is a directory**, and owns everything in it (`docs/gsql_storage_design.md` §7).

- **The I/O layer gained `mkdir` and `list`** (`include/gsql_io.h`), in the real layer and the simulated one.
  - A directory `mkdir` made is a name in its parent, so it is durable only after the parent's `sync_dir`. A crash that loses it loses everything beneath it, however well that was synced. `tests/run_gsql_io.sh` holds that as a rule-2 difference, and turning it off is caught.
  - A listing is in byte order through one walk shared by both layers, and the parity tier compares it.
- **Every candidate's `open` takes a directory** (`gs_db_dir` in `common.c`), and keeps its old file names inside it (`db`, `db.lock`, `db.log.3`, `db.17.sst`).
  - A directory opens only if every name in it is one an engine could write: `db`, or `db` followed by `.` or `-`. Anything else is refused with `FORMAT`.
  - **That refusal is what makes the rest safe, and the test shows why.** With it turned off, the open's sweep deleted the stranger's file.
  - With `create` an absent directory is made, and its parent synced.
- **2.C deletes at open whatever it does not name** (`sweep_dir`), and its `check` lists the directory instead of probing every number below `next_file`.
- **Four mechanisms were measured redundant under the sweep, and removed.**
  - **The removed mechanisms:**
    1. the manifest's garbage list;
    2. its list of live blob files;
    3. clearing a reused number's file of the other kind;
    4. the backup's `dest.pending` record.
  - **How each was measured.** Each was disabled with the sweep kept, and the crash gate stayed at 0 violations.
  - **Why the measurement means something.** With the sweep also disabled, almost every recovery checked failed (process 794 of 794, power 1,206 of 1,206).
  - **What it buys.** 2.C is the same size (3,294 lines before, 3,293 after), so the gain is in the mechanism, not the line count. Four separate answers to "what can a crash strand?", each added when a leak was found, became one rule that covers files nobody has thought of yet.
- **A backup's destination is a database directory too**, so clearing it is listing and removing.
- **Directed tests** (`test_store`'s `directory_rules`) cover these cases, each proven red:
  - a stranger's file is refused, and left alone;
  - a plain file passed as the path is refused;
  - four store-shaped strays are reported by `check` and deleted at the next open.

## A cap on open files (2026-10-27)

Test 4 left one item that would break at scale: 2.C kept every sorted file and blob file open from its first read, or from the flush that wrote it, until the file was deleted. A store needed one descriptor per file: 22 at 10⁷ rows, about 16,000 at 1 TB, against a default limit of 1,024 per process.

- **It was worse than Test 4 reported.** Test 4 said files stay open once *read*. A flush also hands its output's descriptor to the new file and never closes it, so a store with no readers at all still holds one per file. Under a limit 40 descriptors above what the process held, `test_store`'s new `many_files` case failed its **commits** with `IO` after about 37 flushes.
- **The fix is a handle list with a cap** (`hnd_get` in `lsm.c`), the shape of RocksDB's table cache.
  - A handle remembers its file's name, so it can be closed at any moment and reopened at the next read. The least recently used is closed to make room.
  - The index and the Bloom filter stay in memory when the handle closes. They are what a read consults first, so a closed handle costs a reopen only when a block is actually read.
  - The default cap is 512, half the usual limit, leaving the rest to the application. `GsOptions.tune_d` sets it. The crash gate runs at a cap of 4, so closing and reopening handles is crashed as well.
- **A second defect came with it: running out of descriptors was reported as a damaged store.** `file_open` turned every failure to open a sorted file into `CORRUPT`, and four cursor paths did the same for any failure from `file_open`. Now a missing file is `CORRUPT` and anything else is the I/O layer's own answer. `many_files` checks this by reopening with the cap lifted past the limit: a scan must answer `IO`.
- **What eviction costs, measured.** YCSB C on 10⁶ rows (1.4 GB, about 22 sorted files), 200,000 reads, two runs each on NVMe. With the cap far below the file count, nearly every block read from disk reopens a file:

  | cap | ops/s | p99 |
  |---|---|---|
  | 512 (default; nothing is closed) | 290 k, 284 k | 8.8, 8.9 µs |
  | 4 | 259 k, 263 k | 10.3, 10.2 µs |
  | 2 | 263 k, 262 k | 10.3, 10.5 µs |

  So the worst case is about 9% of throughput. It is paid only by a store with more files than the cap, which at the default means more than about 30 GB.
- **`many_files`** builds 200 overlapping level-0 files and reads every key twice, scans, runs `check` (which reads every file), reopens and reads again. In the real layer the process limit is lowered first. A get stops at the newest file holding its key, which is never far down here, so the descriptor-exhaustion check uses a scan, which merges every file.

## Blob space by dead fraction (2026-10-27)

The 2.C rebuild left W8 at 2.91 times its live data (2.74 on this round's runs). Two things were wrong with the old relocation rule. It moved values out of the **oldest** quarter of the blob files, and age is not deadness. And it acted only when a compaction happened to pass through, which stops once writing moves elsewhere.

- **Each sorted file records the bytes its pointers cover in every blob file it points into.** A blob file's live bytes are then a sum over the current version, and its dead fraction is 1 − live / its size. It is derived from the version, not kept as a separate garbage counter that a crash could leave wrong. The counts go in the manifest, and `check` recounts each one from the file's pointers.
- **A compaction relocates out of every blob file at least half dead** (`GC_DEAD_PCT`), as judged when it begins.
- **When the tree's shape wants no compaction, `gc_start` begins one for blob space.**
  - **Which file.** It picks the file with the most bytes in the deadest blob file. A file below level 0 is rewritten at its own level, with nothing merged; a level-0 file gets an ordinary level-0 compaction.
  - **Why it cannot loop.** Each rewrite ends at least one file's pointers into a collectable blob file.
  - **When it looks.** The scan reruns only after a new version is installed, since nothing else changes its answer.
- **It has its own pace.** A GC compaction gets 4 times what the commit wrote, not the 32 the tree's shape gets. Above half dead, a byte relocated frees at least a byte. If the shape comes to want a compaction while a GC one is running, the GC one is finished at full pace, since it is in the way.
- **`lsm_stats` now counts blob files.** It reported "every file the store owns" and summed only the sorted files and the log. The benchmark measures the directory, so W8's figures were right all along; a test bounding space through `stats` would not have been.

**W8, two runs of each build alternated on NVMe** (2,000 records of 100 KiB, 20,000 operations):

| | before | after |
|---|---|---|
| space amp | 2.74, 2.74 | **1.96, 1.96** |
| write amp | 4.09, 4.09 | 4.28, 4.28 |
| ops/s | 2,084, 2,089 | 2,059, 2,037 |
| write p99 | 935, 955 µs | 992, 1,029 µs |
| write p999 | 53.6, 53.6 ms | 55.2, 54.2 ms |

- **The first version tripled the p99** (3,375 and 3,322 µs). It spent GC work at the shaping credit, so the credit a shaped tree leaves unspent became every commit's work. The separate credit brought it back.
- **The threshold is a trade, measured at one end.** Relocating out of **every** blob file gives space 1.64, but write amp 5.57, 1,777 ops/s and a p99 of 3,535 µs.
- **YCSB-A is unchanged; two of its runs looked otherwise.** Its values are under the 4 KiB separation threshold, and its flush and page counters were identical across builds. Its timings on this disk are **bimodal whatever the build**: four runs gave about 7,300 ops/s with a 430 µs p99, or about 5,000 with 2,300 µs. The first pair happened to land one build in each mode and read as a 32% regression, so one pair of timings here proves nothing.
- **What reaches it now.** In two short crash runs (process and power, 512-byte pages), relocation ran 1,564 and 2,392 times and `gc_start` began 501 and 648 compactions. The rebuild's gate reached relocation in 5 cases. The level-0 path was never taken (0 of 1,149); it is the ordinary level-0 compaction, which the gate already covers.
- **`test_store`'s `blob_space` is the only check on space.** Nothing in the crash gate measures it, so a trigger that never fired would pass every tier.
  - **The setup.** It loads 400 values of 1,000 bytes and overwrites 70% of them, with a memtable sized so the values die inside one level-0 merge. That merge's relocation set was fixed before they died, so after it only `gc_start` can reach them. It then writes small values to another tree, whose key range overlaps nothing.
  - **What it asserts.** The `.blob` files on disk, listed through the I/O layer, must be at most 1.25 times the live values. They measure 1.00, and 1.58 without the trigger. A reader held from the load must still read the load, and `stats` must count at least the blob bytes.
- **Seen on the way, and not changed: a commit hands back only `IO` from the compaction it runs.** Any other failure is dropped, because the commit itself succeeded. So a compaction that fails on every relocation shows up only in `check`, and as space that does not come back (see the table below).

## The flush off the commit path (2026-10-27)

**What a W3 commit waited for, measured first** (5 M records under a 1 GiB memory cap, timed inside the engine).

| part of the slow commit | cost |
|---|---|
| the flush, which lands in about 3% of commits | about 75 ms |
| — creating the next log, and writing and syncing its 64 MB of zeros | 26 ms |
| — writing the level-0 file | about 40 ms |
| — the manifest | 5 ms |
| — deleting the old logs | 3 ms |
| a compaction slice (32 times what the commit wrote), in about a third of commits | 60 to 90 ms |

So the p99 commits (141 to 156 ms) were a flush and a slice together. The flush was half of a p99 commit, not most of it.

**What changed:**
- **A full memtable is frozen and flushed in slices.**
  - **Freezing.** The memtable stays in the version as `imm`, read between the live memtable and level 0 by point reads, cursors and the backup. Commits go on into a new memtable.
  - **Slicing.** Each commit writes twice what it wrote (`FLUSH_CREDIT`) of the frozen memtable to its level-0 file.
  - **Installing.** When it is all written, one manifest names the file and moves the first log to replay. The old logs go only after that.
  - **The fallback.** If the new memtable fills first, the flush is finished outright, as compaction does at `COMP_STALL`.
- **A freeze syncs the old log.** `durable_seq` is one high-water mark. A later sync of the *new* log would otherwise also vouch for frozen commits that sit unsynced in the old one.
- **The next log is the frozen memtable's own, recycled.** Once the install's manifest is durable, the newest frozen log is renamed to be the spare. It is already full size and synced. Its new name is made durable before any commit lands in it.
  - **Each log record now carries its log's number**, so replay stops at a recycled file's old records, which still have valid checksums.
  - **Zeros are written only for the first freeze after an open**, in 4 MB chunks.
  - **Why recycle.** Zeroing every spare in slices cost W8's p99 write: 1.0 ms became 2.3 ms with a sync per slice, and 3.6 ms with 4 MB chunks.
- **A read moves a pending flush on once writing has stopped:** 64 reads with no commit between them, 256 KB a time.
  - **Why.** A flush otherwise advanced only with commits, so a store that stopped writing kept its frozen memtable for good, and with it the memory and a second memtable to search.
  - **Why so many reads.** Done at every read, it doubled W8's p99 read. With "no commit since the last read" the p99 read was still 210 µs, since two reads in a row are common in a 50/50 mix.

**W3, 5 M records under a 1 GiB cap, three runs each alternated with the pushed build:**

| | rows/s | p99 | p999 | write amp | peak RSS |
|---|---|---|---|---|---|
| before | 36,614 / 37,325 / *26,604* | 121 / 121 / *161* ms | 138 / 136 / *213* ms | 14.38 | 140 MB |
| after | *28,952* / 38,789 / 38,067 | *130* / 86 / 87 ms | *140* / 90 / 94 ms | 12.51 | 157 MB |

- **The machine runs in two modes**, whatever the build. Each build had one run in the slow mode (italic).
- **In the fast mode** the p99 falls from 121 ms to 86–87 and the p999 from 136–138 to 90–94. What is left of the p99 is compaction slices.
- **The cost:** the p50 rises from 4.4 ms to 5.6, now that the flush's work is shared by ordinary commits. Peak memory rises 17 MB, since a frozen memtable and a live one coexist.

**The rest, 100 k records on NVMe, two runs each:**

| | before | after |
|---|---|---|
| W8 ops/s | 2,028 / 2,047 | **2,234 / 2,219** |
| W8 write p99 | 1,019 / 1,000 µs | 1,007 / 1,122 µs |
| W8 write p999 | 57 / 55 ms | **15 / 14 ms** |
| W8 read p99 | 100 / 97 µs | 94 / 96 µs |
| W8 write amp | 4.28 | **2.32** |
| YCSB load, rows/s | 177 k / 176 k | **195 k / 200 k** |
| YCSB load, write amp | 3.94 | 2.47 |
| YCSB load, space amp | 1.62 | **2.12** |
| YCSB C, 2 M reads | 718 k | 715 k |
| recovery | 53.1, 52.9, 52.4 ms | 58.4, 56.9, 55.8 ms |

- **Write amplification falls because the zeros are gone.** Before, every flush wrote 64 MB of them.
- **Space at 100 k records rises.** The spare is one more 64 MB log, which matters only while a store is small.
- **Recovery is 4 ms slower.** An open replays the spare as well, if only to find it empty or recycled.
- **YCSB C** finishes its frozen memtable in the read-only phase, and is level over 2 M reads.
- **The first comparison at 20,000 reads showed a 40% loss, and it was the window, not the read path.** The pages read were identical (6,312 against 6,316). The four syncs of the flush's install, about 30 ms, fell inside a 41 ms phase.
- **YCSB-A and W2 are bimodal** on this disk in both builds and are not recorded.
- **A smaller compaction credit is a trap.** At 8 instead of 32, W3's p99 halves (45 ms), but compaction falls behind until level 0 stalls, and the stall finishes the merge outright: a p999 of 2.6 **seconds**, and write amp 18.9. The p99 left needs pacing that answers to debt, not a smaller constant.

**Found on the way: a manifest recorded the death of a tree whose commit was not yet durable.** This defect was already in the pushed code.
- **What it did.** A tree replacement retires the old tree, and every manifest wrote the death list whole. Suppose a crash then lost the commit. Recovery brought back the state before it, in which the "dead" tree is the live one, and the first compaction after recovery dropped every row of it.
- **Why it surfaced now.** The sliced flush installs a couple of commits after each freeze, while the new memtable holds unsynced commits, so manifests in that window became common. Compaction installs could always write one.
- **The fix.** A manifest records only deaths at or below `durable_seq`; replay restores the rest from the log. A large transaction's commit, whose manifest is what makes it durable, raises `durable_seq` first.
- **Measured on the code from before this change**, over the hot-key power spec: 1 violation in 403,033 crashes, at seed 35. The same code with only this fix: 0. The gate ran seeds 1–16, so its hot specs run 1–64 now.
- **A first fix went in on a wrong theory and came out again.** It was a directory sync at open, for a spare log a crash might have left with a pending name. Reverting it alone stayed green over 188,299 crashes. A crash image *is* the disk after the crash, so a file recovery can see is on it.

## Recovery, and compaction paced by debt (2026-10-28)

**Recovery against memtable size, measured first** (`hardreq recovery`, NVMe, the open timed inside the engine; `HARDREQ_MEMTABLE` sets the memtable).

| memtable | log replayed | replay | zeroing the log's tail |
|---|---|---|---|
| 8 MB | 7 MB | 36 ms | 8 ms |
| 32 MB (the default) | 15 MB | 79 ms | 29 ms |
| 128 MB | 44 MB | 279 ms | 104 ms |

- **Replay** runs at 150–180 MB/s of log, inserting into the skiplist. It grows with the memtable, and is the price of a larger one.
- **Zeroing** cost about 0.4 ms per MB of log file at every open, whether or not anything had crashed, and whatever was replayed. It also zeroed the spare, which is already clean. It existed because a commit a crash lost can leave a later record on disk behind it, and a new commit landing in the gap could end exactly where that record begins.

**What changed: the log's records are chained, and nothing is zeroed at open.**
- **The chain.** Each record carries the checksum of the record before it in the same file. The first record of a file carries a seed made from the file's number. Replay stops at a record that does not chain.
  - A stale record chains to the lost record it followed, not to the new one in its place.
  - A recycled log's old records chain to their old number's seed. So the chain also does the job of the log number, whose 8 bytes it replaces: the header is still 20 bytes.
- **A value drawn at every open** goes in every record. A new commit can be byte for byte the commit a crash lost: the same sequence, the same contents, which is what a program that retries writes. Then it has the lost one's checksum, and the record behind it would chain to it. The value is the only difference between them.
  - It comes from `/dev/urandom` and the clock, not the I/O layer. It is no data, and only has to differ from the previous open's, which nothing recorded.
  - It is 32 bits, as are the chain and the record checksum. A stale record is accepted only if a new record ends exactly where it begins **and** the two 32-bit values collide.

**Recovery, before and after, two runs each:**

| memtable | 1,000 commits | 10⁶ commits |
|---|---|---|
| 8 MB | 11.9, 12.2 → **3.3, 3.2** ms | 42.5, 42.8 → 35.5, 36.1 ms |
| 32 MB | 30.9, 30.9 → **3.5, 4.9** ms | 103.7, 103.5 → 78.2, 79.4 ms |
| 128 MB | 119.2, 122.4 → **3.1, 3.2** ms | 388.0, 388.4 → 285.8, 289.8 ms |

- **A short log now opens in about 3 ms whatever the memtable.** Before, it paid for the whole file.
- **What is left is replay**, in proportion to the memtable. Making it faster is the next lever, and is not done.
- **The bench, old format against new, two runs each (NVMe):**
  - **Recovery:** 57.4 and 56.6 ms become **28.8 and 29.1**.
  - **W8:** 2,246 / 2,218 against 2,231 / 2,233 ops/s, and a write p999 of 14.4 / 15.0 against 14.2 / 14.6 ms. Level.
  - **YCSB load:** 0.499 / 0.498 s against 0.494 / 0.490. Level.
  - **W3 was not rerun.** The header is the same size, and a commit computes no more than before.

**Two perturbations, and why there is a test for the second:**
- **Without the chain check** (and with no zeroing), every crash tier goes red: process 204, power 144, torn 144, failed-sync 32, hot-key process 54, hot-key power 110 (quick scale).
- **With the per-open value a constant, every crash tier stays green.** The gate never builds the case it guards: its continuations do not rewrite a lost commit byte for byte.
  - So `test_store`'s `lost_twin` builds it on the log's bytes: three commits, then the middle record zeroed (the third kept). Reopen, commit the middle one's twin, and reopen again. The third must not come back.
  - With the value constant it comes back. With it drawn at open it does not, in the simulated layer and the real one.

**Compaction paced by debt: tried, and left at a constant.** The credit is a function of the pressure now: the worst of level 0's file count over its trigger and each level's bytes over its limit. It rises from `COMP_CREDIT_MIN` to `COMP_CREDIT_MAX` as the pressure nears `COMP_STALL`. The defaults are 32 and 32, so nothing changed until a run asks for a ramp.

**W3, 5 M records under a 1 GiB cap, two runs each, alternated:**

| credit | rows/s | mean | p50 | p99 | p999 | write amp |
|---|---|---|---|---|---|---|
| 32 (as pushed) | 34,570 / 38,964 | 28.7 / 25.5 ms | 7.5 / 5.7 ms | 104 / 82 ms | 126 / 88 ms | 12.51 |
| 8 → 64 | 27,454 / 27,234 | 36.2 / 36.5 ms | 36.0 / 36.1 ms | 66 / 70 ms | 68 / 76 ms | 17.67 |
| 12 → 96 | 36,574 / 36,308 | 27.1 / 27.3 ms | 26.1 / 26.7 ms | 67 / 76 ms | 74 / 80 ms | 13.22 |
| 16 → 64 | 20,413 / 27,798 | 48.8 / 35.8 ms | 44.7 / 38.4 ms | 113 / 72 ms | 117 / 75 ms | 17.43 |

- **The best ramp (12 → 96) does not change the mean.** It cuts the p99 by about 20 ms and the p999 by about 10 ms, and raises the p50 from about 6 ms to 26.
- **This is the arithmetic of the design, not a tuning miss.** Compaction runs inside commits, so its total work sets the mean commit, and pacing can only choose how that work is shared out.
  - At a constant credit, two thirds of commits pay nothing and a third pay a whole slice.
  - Spread out by debt, every commit pays about the mean.
- **Two ramps did worse on every count.** 8 → 64 and 16 → 64 raised write amp to 17.5, because a slower compaction lets the levels overrun and the merges grow.
- **What would lower W3's tail is less compaction work, or work off the commit path.** Less work means a lower write amp than 12.5. Off the commit path means a background thread, which RocksDB's 35 ms p99 has. Neither is a pacing question.

## The version store, and W9 (2026-10-28)

**Built once, over the bench's interface, so all six engines pay for the same layout** (`bench.c`, "the version store"). A table is a leading byte of the key, as the application workloads already do:

| key | value |
|---|---|
| the row's key | a 20-byte header (start commit id, commit second, author), then the row |
| `h` key column start-id | the old header and the old temporal column |
| `c` second | the last commit id made in that second |

- **The current value lives in the row**, so an ordinary read never touches history. An update moves the old temporal column, with the header saying which commit made it, to history under the id that version **started** at. The row keeps no pointer (§4).
- **`AS OF` commit A** is the row if its start is at most A. Otherwise it is the greatest history key at or below (row, column, A). That version started by A and the next one after it, so it is the one that was current. **`AS OF` a second** maps through the commit-time index. Ids and seconds both only grow, so the floor of a second is the last commit made by then.
- **One index entry per second, not per commit.** The clock is second-resolution, so that is all the index can say. Every commit in a second overwrites the entry.
- **The commit id is the version store's own here.** The reference engines have none. A candidate's id is known only after commit, and the version has to be written inside the transaction. With one writer, the next id is known at begin, which is what the engine will hand the version store when it moves in (step 5). The counter is recovered at open from the commit-time index's last entry, so it needs no key of its own.
- **W9 is W2 with field 0 of the YCSB row temporal.** The rest of the row is not versioned. Each update reads the old row (130 bytes of it) and writes three keys: the row, a 130-byte version and the 17-byte index entry.

**The model check (`bench ENGINE w9-check`), green on all six engines, 17,381 checks each.**
- It runs 2,000 commits of 1 to 3 distinct rows on a simulated clock. Several commits share a second, and there are empty seconds between, so the index is both overwritten and floored across gaps. The store is closed and reopened halfway, which tests the counter's recovery.
- Every row is then read `AS OF` each of its versions' ids, one either side of each, the load and the last commit. Every second is mapped. Rows that never existed must read as absent: that is where the floor lands in **another row's** history.
- **It runs in `run.sh` for every candidate, under ASan.** Six perturbations were proven red on two engines each:

| perturbation | mismatches |
|---|---|
| no check that the floor landed in this row's history | 50 (every never-existed row) |
| history keyed by the superseding id, not the start id | 12,193 |
| no archiving | 12,193 |
| the index written by one commit in five | 521 |
| the counter not recovered at open | 1 (its own check) |
| the row's own start compared with `<`, not `<=` | 260 |

**W9 against W2: the cost of history on update throughput** (NVMe, 100,000 records, 20,000 durable single-row updates, one run each):

| engine | W2 ops/s | W9 ops/s | change | write amp W2 → W9 |
|---|---|---|---|---|
| 2.A cow | 835 | 810 | −3% | 36.2 → 39.2 |
| 2.B wal | 2,339 | 2,084 | −11% | 23.9 → 31.3 |
| 2.C lsm | 2,973 | 2,879 | −3% | 1.84 → 2.00 |
| SQLite | 1,954 | 1,620 | −17% | 23.2 → 33.6 |
| LMDB | 1,391 | 1,156 | −17% | 31.1 → 43.5 |
| RocksDB | 1,544 | 1,624 | +5% | 1.02 → 1.06 |

- **History costs the B+trees in pages and the LSMs almost nothing.** A version lands on a different leaf from its row, so a B+tree writes or logs a second page per commit. In an LSM it is a 150-byte append to the same log record.
- 2.A loses only 3% because its path copy (36× write amp) already dwarfs one more leaf. RocksDB's +5% is within one run's noise.
- **Write amp here counts the row as the logical write**, so history shows up as overhead, which is the cost being measured.

**`AS OF` reads against depth of history** (the first 1,000 rows given 10, 100 or 1,000 versions each; reads at a commit chosen uniformly over the whole history; mean µs, one run each):

| engine | depth 10 | depth 100 | depth 1,000 |
|---|---|---|---|
| 2.A cow | 1.2 | 2.5 | 4.3 |
| 2.B wal | 1.2 | 2.5 | 4.0 |
| 2.C lsm | 1.9 | 4.1 | 7.6 |
| SQLite | 11.8 | 11.2 | 14.7 |
| LMDB | 0.5 | 0.9 | 1.2 |
| RocksDB | 5.0 | 9.2 | 16.2 |

- **An `AS OF` read is one point read plus at most one floor seek, however deep the history.** The growth in the table tracks the history's total size (10⁴ to 10⁶ versions), which deepens the tree or adds levels. The depth of one row's chain plays no part, and this run does not separate the two.
- **No read missed or returned a version newer than asked** (0 errors, 0 not found, all six).

**Found on the way: in 2.A, a run of large unsynced commits holds every page they free.** At depth 1,000, 2.A's file was 13.7 GB against 2.B's 0.7 GB. The build commits 1,000 rows per transaction without sync, and 2.A frees a page only once its commit is durable (above, "A `sync = false` commit in 2.A"). So none of the pages freed during the build could be reused.
- **Measured both ways at depth 200:** 2.6 GB when those commits are unsynced, 0.5 GB when each is synced.
- **This is a cost of the stated rule, not a new defect.** Nothing had measured it before: group commit's earlier runs used single-row commits, which free a page or two. Under group commit, 2.A's file grows by each unsynced commit's rewritten pages until the next sync. That is a space amplification §6 should weigh alongside W6's.
- **The `AS OF` reads above are unaffected**, since they touch only live pages. The depth-1,000 file size is an artefact of the build, not of history.

## The G-Dash profile, the handoff and space under group commit (2026-10-28)

**`SET=gdash hardreq.sh`** (`hardreq.c`: `g2`, `g3`, `g5`, `handoff`, `gspace`). NVMe, the three candidates, one run each unless stated. Every line checks its answer: a query's sums are compared with sums computed from the row index, and a handoff reads back the last durable commit. Raw lines: `results/2026-10-28/results-gdash.jsonl` and `results-gspace.jsonl`.

### G2: the dominant query

`sum(amount) group by` one of 20 groups over a whole dataset, and the same with an equality filter on a second column. The rows are G-Dash's shape: 100 bytes, written once in key order. Median of five, ms:

| rows | 2.A cow | 2.B wal | 2.C lsm |
|---|---|---|---|
| 10² | 0.010 | 0.012 | 0.004 |
| 10⁴ | 0.67 | 0.64 | 0.40 |
| 10⁶ | 119 | 141 | 160 |

- **A 10⁶-row dataset answers in 120 to 160 ms**, a scan of 6 to 8 million rows a second. At that rate one query fits inside G-Dash's measured 57 ms page load only up to about half a million rows. The scan path is the one already known to be about 10× slower than LMDB's in all three candidates (§6, "Common to every outcome"), so this is where that fix pays.
- **The filter costs nothing and saves nothing.** With no index the query still reads every row, and the filter only skips the addition.
- **A refresh half written alongside changes nothing.** With half the replacement dataset written and not committed, the same query takes the same time (within 8%, all three), and answers from the live dataset.
- **2.C is fastest while the dataset sits in its memtable (10⁴) and slowest once it is in sorted files (10⁶).**

### G3 and G6: the per-request cost, and touching only what a query names

The request is a snapshot, one row read from one dataset, and a release, 20,000 times (µs):

| datasets × rows | 2.A mean / p99 | 2.B mean / p99 | 2.C mean / p99 |
|---|---|---|---|
| 1 × 1,000 | 0.36 / 0.42 | 0.41 / 0.79 | 0.31 / 0.80 |
| 20 × 1,000 | 0.45 / 0.92 | 0.58 / 3.83 | 0.63 / 1.41 |
| 1,000 × 20 | 0.50 / 1.29 | 0.72 / 4.13 | 0.83 / 1.53 |
| 1,000 × 1,000 | 2.55 / 6.36 | 3.51 / 7.65 | 5.28 / 6.62 |

- **The number of datasets costs almost nothing.** The same 20,000 rows held as 20 datasets or as 1,000 give requests within 0.2 µs of each other. The first run held 1,000 datasets of 1,000 rows, 120 MB against a 64 MB cache, and its slower requests are random reads outside the cache, not the dataset count. The control row (1,000 × 20) is what separates the two.
- **A request costs under a microsecond in process.** G-Dash's 10 to 100 requests a second are nothing to the engine; the owner's round trip (§5.5), not yet built, will be the cost.
- **G6: a query naming 2 datasets of 20 costs a tenth of one naming all 20** (0.10 to 0.15 ms against 0.96 to 1.37 ms at 1,000 rows each). Nothing is opened or read for a dataset a query does not name, in any candidate.

### G5: one large write, peak memory against memory at open

| rows in one transaction | 2.A cow | 2.B wal | 2.C lsm |
|---|---|---|---|
| 10⁵ sequential | 12.7 MB | 13.3 MB | 41.5 MB |
| 10⁶ sequential | 67.6 MB | 37.1 MB | 44.0 MB |
| 10⁷ sequential | 79.1 MB | **146.3 MB** | 79.8 MB |
| 10⁶ random | 68.7 MB | 61.0 MB | 77.4 MB |

- **2.A and 2.C are bounded by their buffers**: a 64 MB cache, and in 2.C a 32 MB memtable and half a cache of transaction buffer before a spill. Ten times the rows adds 12 MB in 2.A and 36 MB in 2.C, and both stop near 80 MB.
- **2.B's write memory grows with the transaction**, about 400 bytes per spilled page (110 MB across the 9 × 10⁶ rows between the last two sizes). Each spilled page gets a hash entry and its own vector of frame numbers in the index of spilled frames. SQLite's equivalent, the WAL index, spends a few bytes per frame. It is engineering, not structure, and 2.B is the fallback; at 10⁸ rows it would be about 1.4 GB.
- **One cost of 2.C's spill shows up here: a 10⁷-row transaction takes 27.9 s, against 5.6 s in 2.A and 9.5 s in 2.B.** Each spill is a sorted run written and flushed, and the commit flushes the memtable and names every run. With random keys the order reverses (2.C 1.6 s, 2.A 3.5 s, 2.B 8.0 s at 10⁶).

### The handoff (§5.5)

An owner makes COMMITS single-row commits, synced every 64 and at the last, then either closes and exits, or keeps writing unsynced and is killed with SIGKILL 20 ms later. The time runs from the owner's death to the new owner's first read. Taking the lock is inside the open, since the lock is the store's own.

| commits | 2.A clean / kill | 2.B clean / kill | 2.C clean / kill |
|---|---|---|---|
| 100 | 0.05 / 0.12 ms | 0.99 / 9.01 ms | 2.91 / 10.76 ms |
| 10⁴ | 0.13 / 0.13 ms | 1.83 / 14.98 ms | 11.43 / 21.03 ms |
| 10⁵ | 0.12 / 0.10 ms | 2.09 / 9.63 ms | 94.60 / 106.70 ms |

- **2.A's handoff is constant**, as §6 predicted, at about 0.1 ms.
- **2.B's is bounded by its checkpoint**, about 10 to 15 ms after a kill, whatever the count: its log never holds more than about four checkpoints' frames.
- **2.C's grows with its memtable**, up to about 100 ms when the memtable is full, the replay §6 already records. A clean close does not flush the memtable, so a clean handoff replays too. Flushing at close would make a planned handoff cheap; whether it is worth the close's own time is a choice for the owner.
- **A kill keeps unsynced commits in 2.B and 2.C** (1,100 to 15,000 of the unsynced writes came through), because a process kill is not a power loss and their log frames survive in the page cache. 2.A recovers only what a meta page names. Correctness is the same either way: a commit not returned durable may be kept or lost.
- **Perturbed:** with the child's final synced commit removed, 2.A's line reports the last durable commit lost, and 2.B's and 2.C's do not, for the reason above.
- **Not measured: many owners on one machine, and cold start per tenant** (§5.5's third item). Both need the owner, which is part of step 5.

### Space under group commit (`gspace`)

2,000 transactions of K random updates over a 10⁵-row table, synced every G commits. Peak store size against the size after the load (2.C's includes its preallocated log, about 100 MB):

| K rows per txn | sync every | 2.A peak (end) | 2.B peak (end) | 2.C peak (end) |
|---|---|---|---|---|
| 1 | 64 | 1.12 (1.12) | 1.00 (1.00) | 1.00 (1.00) |
| 100 | 1 | 1.12 (1.12) | 1.00 (1.00) | 1.26 (1.26) |
| 100 | 64 | 3.65 (3.65) | 1.51 (0.76) | 1.26 (1.26) |
| 1,000 | 1 | 1.44 (1.44) | 1.00 (1.00) | 1.59 (1.55) |
| 1,000 | 8 | 3.74 (3.74) | 1.34 (0.50) | 1.59 (1.55) |
| 1,000 | 64 | **21.9 (21.9)** | 9.18 (0.50) | 1.59 (1.55) |

- **2.A's file grows with K × G and never shrinks.** It frees a page only once the commit that freed it is durable, so every unsynced commit's rewritten path is held until the next sync. The growth is the file's high-water mark, so the space stays allocated after the sync.
- **2.B's log grows the same way and is given back.** Frames past the last durable commit cannot be checkpointed, so the log holds them until the sync, then resets and is truncated (the end figure falls below the start, because the start included the load's log).
- **2.C does not depend on G at all.** An unsynced commit is a log append, and the memtable's flush, not the sync, decides when it reaches a sorted file.
- **What it means for the owner:** a group interval sized for single-row commits is safe for all three. With large transactions, 2.A's space amplification is unbounded by anything but the group size, which the owner would have to cap by pages freed rather than by commits or time. 2.A is retired, so this is recorded rather than fixed.

### 2.B's group commit on the flash stick: one explanation ruled out

The stick runs (2026-10-25) predate `b6b21f7` (2026-10-26), which made 2.B reset its log under group commit. A growing log would make every flush commit a change of file size. Rebuilt from both sides of that commit and run twice each on NVMe, at 10⁵ records and 20,000 commits (ops/s, mean of two):

| build | 2.B W2 | 2.B group-8 | 2.B group-64 | SQLite group-8 | SQLite group-64 |
|---|---|---|---|---|---|
| before `b6b21f7` | 2,080 | 8,870 | 17,076 | 8,469 | 14,882 |
| after | 2,105 | 8,816 | 16,235 | 8,236 | 15,320 |

- **The fix changes nothing here**, and the bench's `group-N` path (commits with `sync = false`, then `sync()`) is not the one it fixed (synced commits under a grouping caller). So it does not explain the stick.
- **On NVMe 2.B and SQLite are equal at every group size**, with the same bytes per commit (about 24 KB) and the same flush counts. On the stick, SQLite reached 1,733 commits a second at group-8 (38 MB/s) where 2.B made 23 (0.5 MB/s). The stick's bandwidth is therefore not the limit: something in 2.B's write pattern is, and NVMe does not show it.
- **The next candidate:** 2.B's checkpoint writes pages in its frame index's hash-bucket order, where SQLite's goes in ascending page order. Scattered writes inside one flush are what a flash stick's translation layer punishes. This needs the stick attached, and it was not, on 2026-10-28.

## Step 4: the candidates scored under the fixed weights (2026-10-28 to 29)

**`score.py`**, committed before the round's numbers existed (`f280255`), applies §6's weights to the three candidates. Within a workload: the throughput ratio to the best candidate, and where a p99 is reported the geometric mean of that and the inverted p99 ratio. Space is W7's, as the weights table names it. Recovery is the bench's reopen and the three SIGKILL handoffs. Complexity is lines of code alone: the invariant count was never made.

**The round.** Every engine on one day's code, NVMe only (the stick and the SATA SSD were not attached), then the deciding parts twice more: the main set, TPC-C-like, SSB and W3 for the candidates, and the B+trees at 8 KiB pages. W4 ran once.
- **W4's B+trees are carried from 2026-10-24** (`round-w4-carried.jsonl`). At 4 KiB pages they need about 44 GB at 10⁷ rows, past the 20 GB file-size cap, and died of SIGXFSZ. Since then their code changed only in the backup, the directory layout and 2.B's checkpoint rule for synced commits, which resets its log sooner and so cannot slow a load. W3 and W4 are at 4 KiB in the 8 KiB scoring too.
- **The machine was busy:** a game on one core at the start, and two compiles from another session through the first repetition. The engines alternate within each workload, so steady load falls on all of them, and the three repetitions bound the noise.

**Points out of 100, mean of three repetitions (range of totals):**

| criterion (weight) | 2.A @4K | 2.B @4K | 2.C | 2.A @8K | 2.B @8K | 2.C |
|---|---|---|---|---|---|---|
| standard workloads (35) | 13.2 | 20.4 | 32.6 | 16.3 | 21.6 | 31.5 |
| engine-specific (20) | 6.4 | 9.3 | 17.9 | 8.7 | 10.5 | 17.1 |
| complexity (20) | 20.0 | 19.2 | 13.7 | 20.0 | 19.2 | 13.7 |
| space (15) | 7.8 | 8.3 | 15.0 | 14.1 | 15.0 | 11.5 |
| recovery and handoff (10) | 10.0 | 0.2 | 0.1 | 10.0 | 0.2 | 0.1 |
| **total** | **57.3** (56.1–59.1) | **57.4** (53.6–60.9) | **79.2** (78.9–79.5) | **69.1** (67.1–72.3) | **66.6** (64.2–70.0) | **73.8** (73.7–74.0) |

2.C's scores differ between the two halves because the best candidate in each workload changes with the B+trees' page size.

**Two details of the rule decide the margin at 8 KiB, so each was varied.** These are sensitivity checks, chosen after seeing the numbers, not the rule:
- **Space at 10⁵ records charges 2.C for its two preallocated 64 MB logs**, about 100% of a 100 MB store (W7: 2.28). `--space-at-scale` takes space from W4's 10⁷ rows instead, where 2.C is 1.04.
- **Recovery as a ratio is winner-take-all**: 2.A's 0.1 ms against 10 ms leaves the others 0.01. `--recovery-floor 20` counts anything under 20 ms as 20.

| totals, per repetition | 2.A | 2.B | 2.C |
|---|---|---|---|
| 8 KiB, the rule | 72.3 / 67.1 / 67.8 | 70.0 / 64.2 / 65.4 | **73.9 / 74.0 / 73.7** |
| 8 KiB, space at scale | 61.9 / 56.7 / 57.4 | 59.0 / 53.2 / 54.4 | **77.4 / 77.4 / 77.2** |
| 8 KiB, recovery floored | 72.3 / 67.1 / 67.8 | **79.8** / 73.9 / 75.2 | 79.6 / **79.9 / 79.5** |
| 4 KiB, any of the three | 52.1–59.1 | 49.4–70.7 | **78.9–85.3** |

- **2.C leads in 17 of 18 scorings, and ties the eighteenth** (2.B 79.8 against 79.6, in the repetition the game ran through).
- **Its lead is all performance.** It takes 49 of the 55 workload points against 25 (2.A) and 32 (2.B) at 8 KiB. It gives back 6.3 points of complexity, since the rebuild took it to 3,095 lines against 2,118 and 2,210, and at 8 KiB it gives back 2.6 to 3.5 of space at small sizes.
- **2.A comes second at 8 KiB on these weights**, through its complexity and recovery points. Its retirement rests on what the weights do not hold: the flash stick (1 durable commit a second against 2.C's 410), group commit that cannot close its path copy, and space that grows with unsynced commits (21.9×, kept).
- **The fresh round moved three things the score does not show:**
  - 2.C's point reads are about 32% lower than before the flush moved off the commit path, in a short read run right after a load (YCSB-C 210 k against 310 k, the two builds alternated). A frozen memtable is being flushed by the reads. Over 2 million reads it is 515 k.
  - RocksDB still leads W3 by 2× at 10⁶ records. The 242 k recorded after the rebuild was a 10⁵-record run (corrected in "The 2.C rebuild").
  - 2.C's W7 space is its fixed log reservation, not amplification; it matters only while a store is small.

Raw lines: `results/2026-10-28/round-*.jsonl` and `rep2-*`, `rep3-*`; scores: `score-PAGE-repN.json`.

## Step 5: the owner round trip, many owners, and the planned handoff (2026-10-29)

`roundtrip.c` measures §5.5's last two items over the chosen engine, now `src/gsql/lsm.c`. An owner process serves one store on an `AF_UNIX` stream socket with §6.5's frames (`le32` length, a type byte, a `le32` request id, the body), single-threaded and polling its connections. The bodies are storage requests, not SQL, since there is no SQL layer yet: so what is measured is what the protocol adds, and a query's own work is the same on both paths. **Every reply is checked**: a GET against the value its key was loaded with, a SCAN for 100 rows in order from the key asked for, and the request id echoed. NVMe, one run of each against the same store. Raw lines: `results/2026-10-29/`.

### The round trip

10⁶ rows of 100 bytes, 20,000 requests of each kind, three runs (µs, the middle run's figures; the other two agree within 0.4 µs on means):

| request | in process: mean / p50 / p99 | through the owner: mean / p50 / p99 | added |
|---|---|---|---|
| empty | 0.02 / 0.02 / 0.02 | 5.81 / 4.97 / 11.17 | ~5.6 |
| point read | 3.73 / 3.98 / 6.54 | 11.69 / 11.22 / 22.50 | ~8.0 |
| 100 rows | 17.19 / 18.16 / 25.22 | 28.39 / 28.52 / 52.34 | ~11.2 |

- **The protocol costs 5 to 12 µs a request.** About 5.5 µs is the floor, an empty request, and a 100-row batch adds about 6 µs more for its 13 KB.
- **Against a query's own work it is small but not nothing.** A point read becomes about 3× slower, and a 100-row batch 1.65×. G-Dash's measured page queries take 120 to 160 ms for a 10⁶-row scan (§5.1.1), against which the round trip is noise.
- **The threshold is open question 5, and it is not mine to set.** These are the first numbers the design asked for before agreeing one.

### Many owners on one machine

`roundtrip tenants ROOT N` starts N owners, one database each (100 rows), each the child of the measuring client. Cold start is from the fork to the first verified GET; the databases were flushed, so nothing is replayed.

| owners | memtable | idle RSS per owner | descriptors per owner | cold start mean / p99 | disk per empty database |
|---|---|---|---|---|---|
| 1 | default (32 MB) | 2.25 MB | 8 | 1.30 ms | **134 MB** |
| 100 | default | 2.28 MB | 8 | 0.86 / 1.78 ms | **134 MB** |
| 1 | 1 MiB | 2.26 MB | 8 | 0.89 ms | 4.3 MB |
| 1,000 | 1 MiB | 2.28 MB | 8 | 0.92 / 1.32 ms | 4.3 MB |

- **An idle owner costs about 2.3 MB and 8 descriptors**, constant from 1 to 1,000 owners, and starts in under 1 ms when it has nothing to replay. 1,000 tenants is about 2.3 GB of memory. So an owner per active tenant is affordable, and starting one on demand costs about a millisecond plus its replay (up to about 110 ms with a full memtable, the handoff below).
- **An empty database takes 134 MB on disk at the default memtable**: two logs, each preallocated at twice the memtable size and zero-filled. 1,000 tenants at the default would be 134 GB of zeros. With a 1 MiB memtable it is 4.3 MB. **This is the finding that matters for multi-tenant use**, and it is a cost of the log prefill, whose own justification did not reappear here (see the planned handoff below). Recorded, not changed.
- **The first run reported 57 and 507 descriptors per owner at 100 and 1,000.** That was the harness: its client sockets were not close-on-exec, so each new owner inherited the client's connections to the earlier ones. The engine's own files are opened `O_CLOEXEC` (`src/gsql/io.c`). Fixed in the harness, and the corrected lines are the ones above.

### The planned handoff

`hardreq handoff lsm DIR COMMITS 2` is the planned form: the old owner calls `flush` (step 5) and closes, timed from the decision to hand over; then the next owner opens and reads. Two runs each, ms:

| commits | plain close: close + first read = total | flush + close + first read = total |
|---|---|---|
| 100 | 0.4 + 3.0 = 3.4 | 8.6–12.5 + 0.1 = 8.7–12.6 |
| 10⁴ | 1.0 + 13–16 = 14–17 | 9.5–10.7 + 0.3 = 9.7–11.0 |
| 10⁵ | 13–14 + 107–116 = 120–130 | 30–33 + 0.3 = 30–33 |

- **The next owner's open drops to 0.1–0.3 ms**, since there is nothing to replay. A flush pays off once the memtable holds a few thousand commits, and is about four times cheaper with a full one.
- **The first `flush` cost about 34 ms however little it moved** (35 against 3.5 ms after 100 commits). A freeze needs a ready spare log, and making one ready is 64 MB of zeros, synced. `flush` now freezes into an **empty** log, which is correct (replay stops at its end, and the checksum chain at anything stale). The commit path keeps its paced, zero-filled spare.
- **The empty log costs the next owner nothing, and that was luck of the open, not a property of the log.** 2,000 synced single-row commits after each handoff: 260–271 µs a commit after a flushed one, 262–267 µs with a zero-filled log (three runs each, 10⁴ commits). I first read that as the prefill no longer mattering. **Measured directly it matters**: 20,000 synced commits into a log that is not prefilled take 515–519 µs at the median against 252–256 into one that is (a build with the prefill removed, three runs). The next owner escaped because its open probes the logs in order and writes into the last one present, which was the frozen log the flush had recycled at full size.
- **So an owner that went on writing after its own `flush` paid twice for every synced commit**, 515 against 252 µs at the median, until its next freeze. `flush` now makes the switch an open would: once it has frozen into the empty log and installed, it names the recycled full-size log durably and writes into that. Commits after an owner's own flush: 254 µs at the median. With the name not made durable first, `store_test`'s power losses lose 25 of 275 commits.
- **The prefill stays, then, and so does its cost**: two logs of twice the memtable on disk for every database, 134 MB at the default (many owners, above). Shrinking that is a choice between the memtable's default and the prefill's extent, for its own increment.

### The scan path

`scanbench DIR ROWS` loads ROWS rows of 100 bytes in key order, flushes them to sorted files, reopens, and times full scans that read every key and value, checking count, order and a checksum of the values. Best of five, NVMe, ms:

| build | 2 × 10⁵ rows (fits the cache) | 10⁶ rows (130 MB, twice the cache) |
|---|---|---|
| before | 10.3–10.7 | 166–169 |
| a reused key buffer, the tree bound as one 8-byte compare, one valid source advanced alone | — | 150–151 |
| the processor's CRC32C | — | 116–118 |
| one allocation per cached block (was three, with two frees and two copies) | — | 108–109 |
| read ahead past half the cache, not filling it | 11.5 | **74–75** |

- **What the scan was paying for was the cache, not the merge.** A table that fits ran at 18.7 M rows/s once warm, and one twice the cache size at 6 M, because every block was read, checksummed and decoded into a new allocation, then evicted again, on every scan. Instruction counts (callgrind) fell 20% from the first change and the time 10%, which is what said so.
- **CRC32C was about 3 instructions a byte**, slicing-by-8. It now uses SSE4.2's instruction where the processor has one (x86-64, checked at run time), and the portable version elsewhere or with `GSQL_CRC=portable`. `store_test` holds the two to the same answers at every alignment and to the standard check value, **and that check is the only one that can fail**: with the fast path broken, 2 of 11,474 checks failed, because every store's writer and reader agreed on the wrong checksum.
- **Read-ahead takes a rule, and the first rule was wrong.** Bypassing the cache after two blocks made the 10⁶-row scan 78 ms, and made the small table 17.6 ms where the cache had given it 10.3, which is G-Dash's real pattern: modest datasets scanned on every page load. A scan now goes through the cache until it has read half the cache's size, and only then reads up to 128 KB of consecutive blocks in one call into a buffer of its own and stops filling the cache.
- **G2 at 10⁶ rows** (`hardreq g2`, the G-Dash query): 160 ms before (2026-10-28), 69–86 ms now.
- **Against LMDB the same day** (`scanbench lmdb`, built with `-DHAVE_LMDB`, the same rows and checks, two runs): 10⁶ rows in 14.8–15.0 ms against 2.C's 78–80, and 2 × 10⁵ in 2.8–2.9 ms against 12.4. **The gap is about 5×, where §6 recorded about 10×.** LMDB reads in place from a memory map; what 2.C still copies is each row's key and value into the cursor, and each block once into the cache.

## Perturbations proven red

| broken | caught by |
|---|---|
| the B+tree: nine separate defects (child index, leaf link, merge link, overflow freed, root collapse, climb, APPEND, interior split, seek GT) | `test_btree`, each one |
| 2.A: no flush between pages and meta | power (36 violations), torn (24) |
| 2.A: reuse pages freed by a non-durable commit | power and torn (90 each) |
| 2.A: recovery takes the older meta | power and torn (96 each) |
| 2.A: every meta into one slot | the heavy torn tier only (5); safe and green in light workloads, for the reason above |
| 2.B: checkpoint copies non-durable frames | power (588), torn (698) |
| 2.B: recovery takes frames past the last commit marker | power (183), torn (302) |
| 2.B: commit ignores its sync flag | power (261), torn (268) |
| 2.B: no checksum chain | only the continuation stage (4 and 1); see above |
| 2.B: checkpoint ignores open readers | `test_store` (17), invisible to the crash workload, which holds no readers |
| 2.B: reset ignores open readers | **green, legitimately**: implied by the checkpoint's limit |
| 2.C: no directory flush before the manifest rename | power (63), torn (56) |
| 2.C: a sorted file not flushed before a manifest names it | power (158), torn (161) |
| 2.C: commit ignores its sync flag | power (67), torn (37) |
| 2.C: replay ignores record checksums | torn only (1): the records' own lengths reject most tears |
| 2.C: recovery leaves the log tail in place | **green**: a stale record survives only if a new commit is exactly the size of a lost one. Since 2026-10-28 the tail is left in place by design, and the chain rejects it |
| 2.C: compaction ignores snapshots; retired trees reclaimed under snapshots | **green, legitimately**: readers pin their version (above); the rule is gone since Test 3 |
| 2.C: the horizon is the last commit, not the last durable one | `test_store`'s `dropped_not_durable` (78 of 300 records); **green in the crash gate**, which never sweeps with a retirement unsynced |
| 2.C backup: every retirement copied, including later ones (the logical backup's rule) | `test_store`'s `sliced_backup` (173 of 200 records after 1,500 commits in the copy) |
| 2.C backup: the destination not cleared first | `test_store`: every backup made over another fails check |
| 2.C backup: memtable entries newer than the snapshot copied | `test_store`'s `sliced_backup` (298 of 300 keys); **green until the test compared versions, not counts** |
| 2.C: a flush keeps each key's oldest version, not its newest | `test_store`'s `hot_keys` (484 stale reads) and the hot-key crash specs; **green in every suite before Test 3** |
| 2.A: the settle loop compares lengths, not movement | `test_store` (19), failed-sync and full-disk crash tiers (27) |
| 2.A: every settle round may take reusable pages | `test_store`: the commit never settles and answers CORRUPT |
| 2.A: free-tree writes always from the end of the file | no test: the file grows 9.4× in 40,000 updates (the bench measures it) |
| 2.B: no checkpoint at a sync | no test: the log grows 8.4× in 40,000 group-64 updates (the bench measures it) |
| 2.B: no checkpoint at a synced commit | no test: the log reached 8.3 GB over 10^6 commits (`hardreq recovery` measures it) |
| 2.C: the trivial move made unconditional (overlapping files moved) | `test_store` and every crash tier |
| 2.C: a pausing compaction's outputs not listed as garbage | every crash tier (6,071), once recoveries were checked; **green before that check existed** |
| 2.C: spilled runs not listed during the pre-commit flush | every crash tier (17,528) |
| 2.C: a failed file kept, or a flush's file kept when its manifest failed | full-disk (96 each) |
| 2.C: the manifest records the last replayed log, **and** a flush deletes only the last log | failed-sync (1); **either alone is green**, see above |
| 2.C: blob files never deleted | every crash tier (25,525) |
| 2.C: a compaction's pointers into a blob not counted | every crash tier (1,374) |
| 2.C: a reused number does not clear the other kind's file | every crash tier (125); **green since the directory layout**, whose sweep covers it, and the clearing is removed |
| 2.C: the open-time sweep disabled (and the garbage and live-blob lists with it) | every crash tier (process 794 of 794, power 1,206 of 1,206), and `test_store`'s `directory_rules` |
| 2.C: the manifest's garbage and live-blob lists disabled, the sweep kept | **green, legitimately**: the sweep covers both, and they are removed |
| a database directory holding a stranger's file accepted | `test_store`'s `directory_rules`: the sweep then DELETED the stranger's file |
| the simulated layer keeps files beneath a directory a crash lost | `run_gsql_io`'s rule-2 directory difference |
| 2.C: an open does not delete live-listed blobs nothing points into | every crash tier (25,518) |
| 2.C: relocation aborted when reached | crash tiers (5): reached, though rarely. **Since dead-fraction relocation, green in the crash tiers**, which reach it over a thousand times a run: the commit that runs a compaction drops any failure but `IO`. Caught by `blob_space` (1.58x) |
| 2.C: no dead-fraction trigger (`gc_start` does nothing) | `blob_space`: 1.58x against 1.25; **green in every crash tier**, which measures no space |
| 2.C: never relocate | `blob_space` (1.58x); green in the crash tiers |
| 2.C: a passed-through pointer counted as 0 bytes | `check`'s byte recount, in `test_store` (20) and every crash recovery (901 of 901, 768 of 768) |
| 2.C: the manifest writes the byte counts as 0 | `check` after a reopen, the backup's `check`, and every crash recovery (984 of 984, 1,416 of 1,416) |
| 2.C: relocate out of every blob file, whatever its dead fraction | **green**: a cost, not a fault. W8 space 1.64, write amp 5.57 against 4.28, p99 3,535 µs; `blob_space`'s bytes written differ by only 7% |
| 2.C: GC compactions paced at the shaping credit | no test: W8's write p99 3,350 µs against 1,010 (measured above) |
| 2.C: point reads skip the frozen memtable | `test_store` (11): dropped-not-durable, hot keys, the sliced backup |
| 2.C: cursors skip the frozen memtable | `test_store`'s `frozen_reads` (6 of 24 scans) **and nothing else** |
| 2.C: the backup skips the frozen memtable | `test_store`'s backup checks (5) |
| 2.C: a freeze does not sync the old log | the hot-key power tier (716); green on the others |
| 2.C: the install moves no logs forward but deletes them | `test_store` (3), process (601), power (714) |
| 2.C: a spare used before its name is durable | the hot-key power tier (136) |
| 2.C: replay ignores a record's log number (a recycled log's records replayed) | process (214), power (154), hot-key power (490) |
| 2.C: replay ignores the record chain (2026-10-28, with no zeroing at open) | process (204), power (144), torn (144), failed-sync (32), hot-key process (54) and power (110), at quick scale |
| 2.C: the per-open value a constant | `test_store`'s `lost_twin` **and nothing else**: every crash tier is green, since none rewrites a lost commit byte for byte |
| 2.C: the log recycled before the install's manifest | process (1), power (5): the window is one rename wide |
| 2.C: the manifest records deaths of commits not yet durable | the hot-key power tier (105); the pre-change code, 1 in 403,033 at seed 35 |
| 2.C: the flush drops trees retired since the freeze | the hot-key power tier (47) |
| 2.C: a large transaction's manifest omits its own deaths | **green**: every read stays right, since the catalog no longer points at the retired tree, but its space is never reclaimed. A leak no tier measures |
| 2.C: the directory sync at open removed | **green, legitimately** (188,299 crashes): written on a wrong theory, and taken out |
| 2.C: a read never moves a pending flush on | no test: a store that stops writing keeps its frozen memtable (the frozen memtable is not visible through `store.h`); YCSB C's 4 installs in its read-only phase measure it |
| 2.C: no cap on read handles (`hnd_make_room` does nothing) | `test_store`'s `many_files` in the real layer: commits answer `IO` after ~37 flushes, 40 reads wrong, `check` fails; **green in the simulated layer**, which has no descriptor limit |
| 2.C: closing a handle does not close its descriptor | `many_files`: the store cannot even open (`IO`); LeakSanitizer (12,635 leaked handles) |
| 2.C: a closed handle keeps its stale pointer | ASan use-after-free, in both layers |
| 2.C: every failure to open a sorted file is `CORRUPT` | `many_files`: out of descriptors, a scan answered `CORRUPT` |


## Measurements (2026-10-24)

`results/2026-10-24/` holds the raw lines, one file per run (`python3 bench_table.py FILE` prints them). `superseded/` holds the first round, invalidated by the two file leaks above and by compressible values (below), and kept as the record of what was first reported.

**Setup.** 100,000 YCSB records (23-byte key, 1,000-byte value), 20,000 operations, Zipfian keys, every commit synced unless the workload is group-N. One process per run on a fresh directory, ext4 on NVMe, 64 MiB engine cache, OS page cache **not** dropped (that needs root). One run each, no repeats, so treat differences under about 20% as noise. Values are random printable bytes, as YCSB's are.

| ops/s, 4 KiB pages | 2.A cow | 2.B wal | 2.C lsm | SQLite | LMDB | RocksDB |
|---|---|---|---|---|---|---|
| YCSB load | 80,133 | 51,303 | 72,983 | 31,029 | 109,435 | **172,929** |
| YCSB-A 50/50 | 1,441 | 3,230 | **4,965** | 3,213 | 2,608 | 3,387 |
| YCSB-B 95/5 | 11,355 | 47,677 | **64,742** | 34,412 | 24,446 | 19,192 |
| YCSB-C reads | 621,564 | 482,524 | 539,856 | 111,363 | **1,876,449** | 371,443 |
| YCSB-D latest | 10,652 | 47,782 | **65,374** | 18,984 | 15,153 | 21,818 |
| YCSB-E scans | 5,365 | 8,424 | **27,238** | 10,777 | 19,196 | 12,924 |
| YCSB-F read-modify-write | 1,053 | 3,251 | **5,009** | 3,204 | 2,564 | 3,094 |
| W1 uniform reads | 320,831 | 274,373 | 333,285 | 95,171 | **1,565,788** | 278,818 |
| W2 synced writes | 769 | 2,359 | **2,480** | 1,785 | 1,402 | 1,551 |
| W5 100-row scans | 4,493 | 4,611 | 22,133 | 11,586 | **148,307** | 28,270 |
| W6 held snapshot | 3,727 | 3,732 | **9,495** | 2,705 | 3,916 | 7,903 |
| W7 1,000-key hot set | 858 | 2,063 | **2,936** | 2,434 | 1,297 | 1,734 |
| W8 100 KiB values | 1,156 | 1,990 | 743 | 1,233 | **2,043** | 1,789 |
| group-8 | 1,398 | 10,041 | **20,197** | 9,454 | 10,711 | 5,625 |
| group-64 | 2,465 | 20,447 | 46,943 | 19,841 | 24,386 | **55,217** |
| TPC-C-like, tx/s | 773 | 1,938 | **2,200** | 2,039 | 1,354 | 1,169 |
| SSB Q1.1, ms (6.0 M rows) | 1,048 | 1,074 | 1,325 | 501 | **109** | 1,582 |
| reopen after a crash | 0.78 ms | 2.58 ms | 8.42 ms | 1.31 ms | **0.23 ms** | 86.9 ms |

| amplification | 2.A | 2.B | 2.C | SQLite | LMDB | RocksDB |
|---|---|---|---|---|---|---|
| write, W2 (bytes written per logical byte) | 36.2 | 23.9 | 7.5 | 23.2 | 31.1 | **1.0** |
| write, a million group-64 updates | 32.3 | 24.0 | 8.6 | 28.3 | 31.1 | **2.2** |
| write, TPC-C-like | 38.0 | 25.5 | 10.0 | 29.4 | 33.5 | **1.5** |
| space, YCSB at 4 KiB pages | 4.41 | 4.14 | **1.12** | 4.73 | 2.16 | 1.01 |
| space, YCSB at 8 KiB pages | 1.86 | 1.75 | **1.11** | 1.58 | -- | 1.01 |
| space, W6 (one snapshot held) | 5.58 | 4.73 | **1.16** | 5.13 | 3.09 | 1.05 |
| space, SSB lineorder (100-byte rows, key order) | 2.34 | 2.11 | 1.21 | 1.22 | 1.14 | **0.68** |
| file growth, a million updates | 1.00 | 1.00 | 1.30 | 1.00 | 1.00 | 1.74 |

The **TPC-C-like** run is one warehouse, the standard mix, synced commits and no think times, so it is **not a tpmC**. New-Order p50/p99 in µs: 2.A 1,356/5,165, 2.B 570/4,587, 2.C 328/2,280, SQLite 549/3,433, LMDB 838/1,375, RocksDB 759/4,534. The **SSB** run is scale factor 1, with every engine's revenue checked against the answer key the harness computes while generating the data. All six agree.

What the tables do not say by themselves:

- **The 4× space cost of the B+trees is the page size, as suspected.** A 1,023-byte record is over a quarter of a 4 KiB page, so each takes its own overflow page. At 8 KiB the cost falls to 1.86 (2.A) and 1.75 (2.B), against SQLite's 1.58.
  - At 8 KiB, throughput is roughly level on reads, and TPC-C on 2.A rises from 773 to 1,026 tx/s.
  - 2.A's write amplification doubles at 8 KiB (64 on W2), because each path copy is twice the bytes.
- **The B+trees fill pages half-full on key-order insert.** At 100-byte rows (SSB) they take 2.1-2.3× the data where SQLite and LMDB take 1.1-1.2. Both split at the middle, and a split that favours the right edge is the known fix.
- **2.A's commit is its weakness, and group commit does not close it.** At group-64 2.A reaches 2,465 updates/s against 20,447 for 2.B and 46,943 for 2.C. Its cost is the path copy (32× write amplification), not the flushes, which group commit already removes (0.03 per update).
- **Every candidate's scan path is slow, and that is code, not design.**
  - Our two B+trees scan 5.7 M rows/s on SSB, against 55 M for LMDB and 12 M for SQLite; 2.C does 4.5 M against RocksDB's 3.8 M.
  - W5's 100-row scan is 4.5 k/s on the B+trees against 148 k/s on LMDB.
  - The B+trees share one cursor (`btree.c`), so this is one piece of work, needed whichever wins.
- **RocksDB's write amplification is low because the data set is small.**
  - Now that values are incompressible, RocksDB does flush and compact over a million updates (2.2, file growth 1.74). But 100 MB sits in its first level or two.
  - 2.C writes 8.6 on the same run, so its compaction policy is several times dearer than RocksDB's at this size.
  - Neither figure says anything about W4's 10 million rows.
- **2.C has no value separation, and W8 shows it**: the slowest engine at 100 KiB values (743 ops/s, write p99 18 ms, write amplification 11). The values are rewritten by compaction. A WiscKey-style value log is the known remedy, and it would be a design addition, not a tuning.
- **Recovery is small for everyone.** 20,000 unsynced updates replay in under 9 ms on every candidate. A recovery figure that tests "bounded" needs a much longer log.
- **Lines of code are close.** Comment and blank lines excluded:
  - 2.A is 2,045 (773 own, plus the shared B+tree's 1,272);
  - 2.B is 2,129;
  - 2.C is 2,136.
  - The invariant count, §6's other half of complexity, has not been counted.

### Against §6's predictions

| prediction | measured |
|---|---|
| 2.A wins recovery and handoff time | **holds among the candidates** (0.78 ms against 2.58 and 8.42), though LMDB is faster still; not yet measured at a long log |
| 2.A loses single-row commit latency without group commit | **holds**: W2 at 769/s against 2,359 and 2,480 |
| ...with group commit closing most of the gap | **refuted**: at group-64 it is 8× behind 2.B and 19× behind 2.C. The path copy, not the flush, is the cost |
| 2.A loses space during W6 | **holds**: 5.58 against 4.73 (2.B) and 1.16 (2.C) |
| 2.B wins single-row commit latency | **refuted, narrowly**: W2 p50 316 µs against 2.C's 257 µs |
| 2.B wins space efficiency | **refuted**: 4.14 against 2.C's 1.12 (1.75 against 1.11 at 8 KiB) |
| 2.B's recovery grows with the log | not tested: the log here is too short |
| 2.C wins W4 by a wide margin | **refuted at its defaults** (2026-10-25, below): at 10 M random keys 2.C loads 14,336/s against 2.A's 36,326 and RocksDB's 32,304. A larger memtable recovers most of it |
| 2.C loses W1 tail latency | **refuted**: W1 p99 4.4 µs, the best of the three |
| 2.C has the highest complexity | **not shown by lines** (2,136 against 2,045 and 2,129); invariants are not counted |
| overall: 2.A | **refuted on the standard workloads.** 2.C leads YCSB A, B, D, E and F, W2, TPC-C and group commit; 2.A leads none of them among the candidates |

## Confirming tests (2026-10-25)

Run after §6 took 2.C provisionally, to try to overturn it. Raw lines are `results-w3`, `-w4`, `-ltm`, `-ssd` and `-usb.jsonl` in `results/2026-10-24/` (the directory is named for the round, not the day). One run each, so treat differences under about 20% as noise.

**Devices.** Both slower devices negotiated USB 3 at 5 Gb/s, so the link is not the limit in either case.
- **ssd**: a Samsung 860 EVO SATA SSD behind an ASMedia ASM1153e USB-C bridge, ext4.
- **usb**: a generic USB 3.0 flash stick on a USB-A port, reformatted as ext4 for this, so that the device is the only difference from the other runs.

**Larger than memory.** 5 M records (about 5 GB) under a 1 GiB memory cgroup with swap off. Page cache is charged to the cgroup, so this is genuinely larger than memory and needs neither root nor cache dropping (`BENCH_MEM`). SQLite hit the one-hour limit on all six workloads and is absent.

| ops/s, larger than memory | 2.A | 2.B | 2.C | LMDB | RocksDB |
|---|---|---|---|---|---|
| W1 uniform reads | 10,498 | 5,398 | 10,472 | 2,525 | 4,588 |
| YCSB-B | 6,966 | 12,986 | 12,586 | 3,453 | 4,475 |
| YCSB-C | 13,863 | 10,526 | **15,634** | 4,508 | 7,841 |
| W2 durable commits | 648 | **1,520** | 1,329 | 609 | 871 |
| W5, 100-row scans | 135 | 132 | **1,249** | 90 | 574 |
| W3, 1,000-row batches (rows/s) | 8,307 | 8,491 | 9,030 | 2,222 | **107,340** |
| W3 p99 per batch | 257 ms | 249 ms | **1,851 ms** | 1,117 ms | 35 ms |

**W4, a 10 M-row bulk load** (about 10 GB), NVMe, rows/s:

| | 2.A | 2.B | 2.C | SQLite | LMDB | RocksDB |
|---|---|---|---|---|---|---|
| sorted keys | 77,410 | 46,782 | 52,702 | 45,637 | **185,089** | 162,226 |
| random keys | **36,326** | 28,650 | 14,336 | 13,082 | 34,835 | 32,304 |
| write amp, random | 14.6 | 16.2 | 25.5 | 29.3 | 11.1 | 8.2 |
| space amp | 4.36 | 4.07 | **1.04** | 4.59 | 2.02 | 1.02 |

**2.C's W4 loss is mostly its defaults.** At 2 M rows on NVMe:

| 2.C memtable | random load (rows/s) | write amp | sorted load (rows/s) | write amp |
|---|---|---|---|---|
| 4 MB (default) | 24,405 | 17.9 | 56,870 | 7.0 |
| 16 MB | 38,593 | 12.7 | 75,738 | 6.2 |
| 64 MB | 61,384 | 8.4 | 97,119 | 5.9 |
| RocksDB | 115,225 | 4.7 | | |

Level sizes follow the memtable, so a 4 MB memtable means a 40 MB level 1 and five or so levels for 10 GB. The sorted load stays near 6× because 2.C has no trivial move: RocksDB moves a file down a level without rewriting it when nothing below overlaps it, and writes 2.0× on sorted input. The rest of the gap to RocksDB is its background compaction threads.

**Slower devices**, 100 k records, ops/s (TPC-C-like in tx/s):

| | device | 2.A | 2.B | 2.C | SQLite | LMDB | RocksDB |
|---|---|---|---|---|---|---|---|
| W2 durable commits | ssd | 203 | 800 | 764 | 822 | 556 | 591 |
| | usb | 1 | 26 | **410** | 30 | 2 | 90 |
| group-8 | ssd | 1,213 | 3,799 | **7,575** | 4,952 | 2,644 | 4,086 |
| | usb | 1 | 23 | **3,067** | 1,733 | 3 | 738 |
| group-64 | ssd | 1,654 | 6,915 | 21,648 | 10,818 | 3,931 | **24,328** |
| | usb | 9 | 19 | **18,035** | 1,205 | 15 | 4,377 |
| YCSB-A | ssd | 846 | 2,069 | **2,482** | 2,038 | 1,110 | 1,165 |
| | usb | 2 | 51 | **1,001** | 70 | 10 | 127 |
| W3 (rows/s) | ssd | 15,865 | 11,600 | 30,879 | 8,748 | 19,982 | **107,668** |
| | usb | 100 | 93 | 2,961 | 77 | 132 | **41,105** |
| TPC-C-like | ssd | 357 | 806 | **1,069** | 794 | 452 | 557 |
| | usb | 0.4 | 18 | **339** | 33 | 1 | 69 |
| recovery (ms) | ssd | 1.87 | 5.72 | 25.33 | 1.37 | 0.16 | 130.19 |
| | usb | 545 | 27.3 | 80.6 | 1.04 | 0.21 | 74.8 |

The ssd runs used 20,000 operations; the usb runs used 1,000, because 2.A could not finish 20,000 durable commits on the stick within the hour (two timed-out lines are kept outside the results).

**What the confirming tests show.**
- **The slower devices widen 2.C's lead.** On the stick, copy-on-write collapses: 2.A and LMDB manage one or two durable commits a second with p99s of 2 to 4 s, because every commit writes scattered pages and flushes twice. 2.C appends to a log and commits 410 a second, 16× 2.B.
- **2.B's group commit does nothing on the stick**, an unexplained result. Its flushes fall as they should (134 for 1,000 commits at group-8, 24 at group-64), and its throughput does not rise (26, 23, 19/s). The cost is therefore the volume or pattern of its page writes, not the flush count. This is not yet investigated.
- **Larger than memory, 2.C leads or ties everything but W3**, and its 100-row scans are 9× faster than either B+tree's.
- **2.C has two weaknesses, both engineering rather than structure.**
  1. Its bulk load needs bigger levels and a trivial move.
  2. Its commit can stall: W3's p99 is 1.85 s against RocksDB's 35 ms, because compaction runs in the foreground, inside the commit that triggers it. §6 predicted a tail-latency weakness for 2.C, and it appears only at scale.
- **2.A's recovery advantage disappears on the stick** (545 ms, the slowest), while LMDB's and SQLite's stay near 1 ms.

### Not yet measured, and needed before §6 can decide

- **W9 is measured** (2026-10-28, "The version store, and W9"). History costs the B+trees 3–17% of update throughput and the LSMs almost nothing. An `AS OF` read is one point read and one floor seek.
- **2.A's space under group commit is measured** (2026-10-28, "The G-Dash profile, the handoff and space under group commit"): 21.9× at 1,000-row transactions synced every 64, and never given back. 2.B's log grows and is truncated back; 2.C does not depend on the group size.
- **The hard requirements**: measured in Test 3 (above), except nonce uniqueness, which waits for encryption. Online backup is not met as built.
- **The G-Dash profile is measured in process** (G2, G3, G5, G6, same section; G1 and G4 in Test 3). **The handoff is measured**, clean and by SIGKILL. **Not measured: the owner round trip and many owners on one machine** (§5.5), which need the owner (step 5). 2.B's write memory grows with a transaction, about 400 bytes per spilled page.
- **2.C's remaining items are finished** (2026-10-28):
  - **Recovery** no longer zeroes the log at open: a short log opens in about 3 ms whatever the memtable. What is left is replay, in proportion to the memtable.
  - **Compaction pacing by debt** was measured and left at a constant. It trades the p50 for the p99 at the same mean. W3's tail needs less compaction work or a background thread ("Recovery, and compaction paced by debt").
- **2.B's group commit on slow flash**, still unexplained. The log-reset fix is ruled out (no change on NVMe, and not the bench's path); the next candidate is its checkpoint's write order, which needs the stick attached.
