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
  - **How it works now:** a commit adds one record, its freed pages under its commit id. A write transaction takes reusable records out of the tree at its start, and again whenever an allocation finds none left (LMDB's lazy refill). Pages it took and did not use go back under key 0. While the tree itself is being written, allocations come from the end of the file, so writing it never consumes what it records.
  - **Building it found four defects**, all as leaks reported by `check`, or as a settle loop that never ended:
    1. a record long enough to overflow freed its own chain on each rewrite, and the freed pages joined the list being written. Records are now chunked so they always fit inline;
    2. deleting and re-inserting the chunks merged and split nodes every round. Chunks are now replaced in place;
    3. a transaction took only part of key 0, and the commit's rewrite then overwrote the chunks it had not taken;
    4. a nested take re-loaded records the outer one had not yet deleted.
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
- **2.B's creation writes the header last.** The state page and the catalog are written and flushed, and only then the header. The other way round, a power loss kept the header and lost what followed, and recovery found a database it could not read. The crash harness found this twice: first a missing state page, then, once that was checked, a missing catalog. The fix is the write order, not more checks.
- **A commit in 2.B answers IO if the checkpoint after it fails a flush**, even though the commit itself is durable. Answering OK hid that the store had become unusable, and the harness found it. IO is the honest answer, and it is safe, since a commit reported as failed may be either kept or lost.
- **2.B keeps commits that had not returned durable far more often than 2.A**: in 12,893 of 18,672 power-loss crashes in the first run, against a few hundred for 2.A. Its log frames can land and stay valid without a flush, while 2.A recovers only what a meta page names.
- **The crash harness gained a continuation stage** (§5.3: "a further workload runs, crashes and recovers correctly"). After recovery it runs a continuation of mixed synced and unsynced commits, crashes it at a seeded point, and judges the result against the recovered state plus a prefix of the continuation.
  - The single durable commit it had before could not see an engine that accepts **stale log frames**: the frames a lost commit leaves behind, partly overwritten later. Removing 2.B's checksum chain passed 44,000 crashes. With the continuation it is caught (4 power and 1 torn violations in 45,000 crashes, and 5 and 9 in 350,000 at rare checkpoints), with the correct engines clean.
  - It is a rare case, because each log reset changes the salt and invalidates stale frames anyway.

- **2.C must flush the directory before renaming a manifest into place, as well as after.** A compaction creates new sorted files, then renames a manifest naming them. Pending namespace operations survive power loss as any subsequence, so the rename could survive while the creations did not, and recovery found a manifest naming files that were not there. The crash harness found it, as both a recovery failure and a continuation coming back as no prefix. It costs one extra directory flush per manifest write. A journalling filesystem would usually preserve the order, but rule 2 does not promise it.
- **2.C's readers pin the version they began with**: the files and the memtable. A flush makes a new version with an empty memtable, so an older reader keeps the old memtable and the old files, which between them hold the same data.
  - The first draft cleared the memtable in place, freeing nodes an open reader was pointing at.
  - Because of the pinning, compaction's "keep every version a snapshot needs" rule is not load-bearing. Perturbing it, and reclaiming retired trees under open snapshots, both pass every test. It would matter if readers used the current version filtered by sequence, as LevelDB's snapshots do. The cost of pinning is space while a reader is held (W6), the same shape as 2.A's pinned pages.
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
| 2.C: compaction ignores snapshots; retired trees reclaimed under snapshots | **green, legitimately**: readers pin their version (above) |

## First measurements (2026-10-24)

`results/2026-10-24/` holds the raw lines (`python3 bench_table.py FILE`
prints them). 100,000 records of 1,023 bytes, 20,000 ops, Zipfian keys,
every commit synced, ext4 on NVMe, one run each and no repeats -- treat
differences under about 20% as noise.

| ops/s | cow 2.A | wal 2.B | lsm 2.C | sqlite | lmdb | rocksdb |
|---|---|---|---|---|---|---|
| YCSB-A 50/50 | 1,066 | 3,505 | **4,970** | 2,668 | 2,187 | 3,311 |
| YCSB-B 95/5 | 14,822 | 46,572 | **60,644** | 33,664 | 24,804 | 18,447 |
| YCSB-C reads | 571,148 | 480,360 | 510,145 | 103,685 | **1,679,319** | 350,691 |
| YCSB-E scans | 5,507 | 8,127 | **25,261** | 10,193 | 18,334 | 17,188 |
| W2 writes | 590 | 2,029 | **2,943** | 1,999 | 1,177 | 1,702 |
| W6 held snapshot | 3,373 | 4,276 | **9,696** | 2,669 | 3,904 | 7,897 |
| W8 100 KiB values | 1,196 | 2,298 | 786 | 1,011 | **2,419** | 2,146 |
| group-64 | 2,538 | 16,369 | **49,675** | (synced) | (synced) | (synced) |
| write amp, YCSB-A | 54.6 | 21.9 | 3.5 | 19.3 | 31.0 | (1.0, see below) |
| space amp | 4.2-7.5 | 4.1-7.3 | 1.1-1.3 | 4.7 | 2.2 | 0.5 (compressed) |
| reopen after crash | 0.7 ms | 3.4 ms | 6.3 ms | 1.6 ms | 0.2 ms | 78 ms |

What the table does not say by itself:

- **RocksDB's write amplification of ~1.0 is not earned.** 20,000 updates
  of 1 KiB is 20 MB, under its 64 MB memtable, so it never flushed or
  compacted inside the measured window. 2.C did compact (3.5 on A). A run
  long enough to force RocksDB's compaction is needed before this axis
  compares anything.
- **The references ignore group commit.** The adapters' sync-off flag is
  unused for SQLite (synchronous=FULL), LMDB and RocksDB (sync write
  options), so their group-N rows are synced commits again and are not
  shown. Only the three candidates honour it.
- **2.C has no value separation, and W8 shows it.** At 100 KiB values it
  is the slowest engine (786 ops/s, write p99 19 ms, write amp 11): the
  values themselves are rewritten by compaction. The B+trees put large
  values in overflow pages and stay near 1.5-2.2. A WiscKey-style value
  log is the known remedy and would be a design addition, not a tuning.
- **2.A pays two flushes per commit** (pages, then the meta page) and path
  copying, so it is the slowest writer everywhere and barely benefits from
  group commit (it still copies the path every commit). It is
  competitive only on reads.
- **The B+trees' space amplification of ~4 is the 1 KiB record
  overflowing a quarter of a 4 KiB page** (noted above); an 8 KiB page
  variant is the test of whether that is a page-size artefact.
- **2.A's file grows 1.8x in the recovery run**: the child's unsynced
  commits allocate pages that the crash then orphans, and reuse waits for
  a durable freeing commit. Bounded, but worth a reclaim-on-open check.
- Recovery here is small (20,000 unsynced updates); every candidate
  reopens in under 7 ms. A recovery figure that matters needs a much
  longer log.

Not yet run: TPC-C-like, SSB flight 1, the 8 KiB page variant, and a long
enough write run to make RocksDB compact.
