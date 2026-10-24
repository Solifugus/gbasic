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
- **2.A's free list is a blob, not a tree.** It is written at each durable commit, as BoltDB does: inline in the meta page when it fits, otherwise a chain of pages taken from the end of the file, so that writing it never consumes the list it is recording.
  - The first version leaked pages on every reopen, and `check` found it. The chain naming the previous list is freed by the new meta, but that was recorded only in memory, never in the list being written.
- **A `sync = false` commit in 2.A writes its pages but no meta page.** The next sync, or the next synced commit, writes one meta covering every commit since (group commit). A page freed by a commit is reusable only once that commit is durable and no open snapshot predates it.
  - Dropping the "is durable" half was caught at 90 violations in 504 crashes. It is what keeps a fall-back to the older meta safe.
- **2.A cannot keep a commit that had not returned durable**, because only a meta page makes a commit recoverable. A crash keeps one only when it hits that meta write itself.
- **Two meta slots matter only when the meta page's live bytes span sectors.** In a light workload the header, the roots and the inline free list fit in the first 512 bytes, the rest of the page is zero in both versions, and any tear yields one valid page or the other. So writing every meta to one slot passed 6,000 torn crashes at 512 B and at 4 KiB.
  - The perturbation is caught only by a delete-heavy workload, whose free list crosses a sector boundary. That is why `run.sh` has a heavy torn tier.
- **The crash harness gained flush sampling**, in the tree (`gsql_io_sim_flush_points`). Sampled uniformly, a run of thousands of operations rarely lands on the one flush that makes a meta page durable. Half the crash points are now drawn from flushes.
  - Measured on 2.A: unsynced commits kept by a crash rose from 150 to 636 in the same run, so far more of the oracle's range is exercised.
  - The single-slot perturbation went from 1 violation to 5.

## Perturbations proven red

| broken | caught by |
|---|---|
| the B+tree: nine separate defects (child index, leaf link, merge link, overflow freed, root collapse, climb, APPEND, interior split, seek GT) | `test_btree`, each one |
| 2.A: no flush between pages and meta | power (36 violations), torn (24) |
| 2.A: reuse pages freed by a non-durable commit | power and torn (90 each) |
| 2.A: recovery takes the older meta | power and torn (96 each) |
| 2.A: every meta into one slot | the heavy torn tier only (5); safe and green in light workloads, for the reason above |
