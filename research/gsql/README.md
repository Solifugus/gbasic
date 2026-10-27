# G-SQL Phase 0a spikes

The storage-engine candidates of `docs/gsql_storage_design.md`, built small enough to measure. **Spike code**:
- nothing here is linked into `gbasic`;
- nothing here is run by `tests/run_all.sh` (§1 of the design says so);
- a written-off candidate is deleted, with its numbers recorded in the design.

The shared pieces they stand on, the I/O layer and the crash harness, are in the tree proper (`src/gsql/`) and gated by `tests/run_gsql_io.sh`.

```sh
research/gsql/run.sh              # build, test and crash-gate every candidate (~35 s per engine)
GATE_SCALE=quick research/gsql/run.sh
```

## Files

| file | what |
|---|---|
| `store.h` | the §1 storage interface as a vtable: one functional test, one crash adapter and one workload harness drive every candidate |
| `common.c` | little-endian fields, CRC32C, varints, the file header and the page header (§3.1) |
| `btree.c` | **one** B+tree over slotted pages and a small pager interface, shared by §2.A and §2.B |
| `btengine.c` | above the pager, shared by §2.A and §2.B: the catalog of named trees, a transaction's view of it, records, cursors and cursor invalidation |
| `cow.c` | **§2.A**, the copy-on-write B+tree |
| `wal.c` | **§2.B**, the B+tree with a write-ahead log |
| `util.c` | page lists and sets, and a page cache keyed by (page, version) |
| `lsm.c` | **§2.C**, the log-structured merge tree |
| `test_btree.c` | the B+tree against a model, over an in-memory pager in both modes |
| `test_store.c` | the interface against a model, for any candidate |
| `crash_store.c` | a candidate under the crash harness |
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
  - **Relocation.** Compactions rewrite values out of the oldest quarter of blob files, so their space comes back.

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
| W3 rows/s, NVMe | 20,667 | **242,199** | RocksDB 92,645 |
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

**What it cost:**
- **Recovery time grows with the memtable.** A 32 MB memtable means up to 32 MB of log to replay, which takes 1.3 s on the flash stick.
- **Space at 100 k records rises to 1.62**, mostly the log's 64 MB of zero prefill counted in the file size. At 10 M records it is 1.04, as before.
- **Point reads are 10 to 20% lower** on YCSB-C, W1 and YCSB-F, and 22% on W1 larger than memory. These are single runs, so part may be noise.
- **W8's blob space is not coming back fast enough.** Relocation only touches the oldest quarter of the blob files, and only when a compaction passes through, so overwritten large values keep their space. It needs a trigger based on how much of a file is dead. The crash gate reached relocation in only 5 cases.
- **A flush still runs inside the commit that fills the memtable.** That is most of W3's remaining p99 larger than memory (150 ms against RocksDB's 35).

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
| 2.C: recovery leaves the log tail in place | **green**: a stale record survives only if a new commit is exactly the size of a lost one |
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
| 2.C: relocation aborted when reached | crash tiers (5): reached, though rarely |
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

- **W9**, which needs the version store, which does not exist.
- **The hard requirements**: measured in Test 3 (above), except nonce uniqueness, which waits for encryption. Online backup is not met as built.
- **The G-Dash profile** (§5.1.1) and the owner round trip (§5.5).
- **2.C's remaining items** (above): blob reclamation by dead space, a frozen memtable flushed in slices, and recovery time against memtable size.
- **2.B's group commit on slow flash**, unexplained.
