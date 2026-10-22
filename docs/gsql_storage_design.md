# G-SQL storage engine: Phase 0a design and evaluation plan

**Status:** Proposal, design phase (2026-10-21). **No code exists and none is to be written until this plan is agreed.** It is Phase 0a of `docs/gsql_design.md` (§6.2, §11): the storage engine is chosen by building candidates small enough to measure, and this document specifies the candidates, what they share, how they are measured and how the choice is made.

**Inputs this plan must honour** (all decided in `gsql_design.md`):

| decision | consequence for storage |
|---|---|
| One owning process per file; clients reach it over a local socket (§6.1) | **One writer, by construction.** Readers are all inside the owner, so there is no shared-memory reader table and no cross-process page locking |
| Owner handoff runs recovery on every takeover (§6.1) | Recovery runs constantly, so **its time is a first-class metric**, not an afterthought |
| Snapshot reads; a client's cursor may span many requests (§6.1) | A reader can hold a snapshot for a long time, so **what a long reader costs** (pinned pages, blocked checkpoints, file growth) is measured |
| Consistent online backup is the engine's job (§6.3) | Each candidate needs a backup story that does not stop writers |
| Page-level encryption, with space reserved in the format now (§6.4) | Every page carries room for a nonce and tag, and **nonce uniqueness** per write is a design property to compare |
| Values are gBASIC values (§3) | Keys need an **order-preserving encoding** that agrees with gBASIC comparison (§3 of this document) |
| Selective temporal history, current-first and backward-linked (§10D) | A **version store** is needed whatever the engine (§4) |

## 1. The storage interface (the seam)

All candidates implement one interface, so one workload harness and one crash harness drive all of them. The relational layer (tables, indexes, the catalog) is built on it later and is not part of Phase 0a.

**Shape: an ordered key-value store with multiple named trees and snapshot transactions.** This is how SQLite, LMDB and most relational engines divide the work: a table is a tree keyed by its primary key, and a secondary index is a tree keyed by (indexed value, primary key).

| operation | meaning |
|---|---|
| open / close | open a file, running recovery if needed |
| read snapshot | a consistent view as of the last commit, held until released |
| write transaction | at most one at a time (the owner serializes them) |
| get, put, delete | on (tree, key bytes) → value bytes |
| cursor | ordered iteration over a key range within a snapshot, forward and backward |
| commit / abort | commit is durable when it returns |
| backup | write a consistent copy as of a snapshot, while writes continue |
| check | verify every checksum and structural invariant |

Keys and values are **bytes** at this boundary. Turning gBASIC values into bytes is a shared component (§3), so encoding cost is measured once and identically for every candidate.

The spikes are standalone C11 programs under a research directory outside `src/`. Nothing in them is linked into `gbasic`, they are not run by the gate, and a written-off candidate is deleted with its numbers recorded here.

## 2. The three candidates

### 2.A Copy-on-write B+tree (LMDB's and BoltDB's shape)

- **How it works:** a page is never overwritten while any snapshot can see it. A commit writes every modified page, and every ancestor up to the root, to free locations. It flushes them, then writes a new **meta page** naming the new root, and flushes again. There are two meta pages, used alternately, each with a transaction id and a checksum.
- **Recovery:** read both meta pages and take the valid one with the higher transaction id. This takes about constant time, with no log to replay, which matters because every handoff recovers.
- **Snapshots:** a snapshot is a root page number. A long reader is free to *read* but pins every page reachable from its root, so pages freed after it began cannot be reused and the file grows while it lives.
- **Free space:** pages freed by transaction N are reusable once no snapshot older than N exists. In LMDB that bookkeeping is a reader table in shared memory; here every reader is inside the owner, so it is an in-memory list. That is a real simplification over LMDB.
- **Backup:** copy the pages reachable from a snapshot root, while writes continue, with no extra mechanism.
- **Encryption:** a page is written once at a given location and transaction, so a nonce built from the transaction id and page number is unique by construction.
- **Expected costs:** two flushes per commit, and **path copying** (a one-row update rewrites tree-depth pages), amortised by batching commits (§5.4).

### 2.B B+tree with a write-ahead log (SQLite's WAL mode, PostgreSQL)

- **How it works:** pages are updated in place, but never before the change is in the log. A commit appends full page images (or deltas) to the log with a checksum chain and a commit marker, then flushes **once**. A checkpoint later copies log pages back into the main file and truncates the log.
- **Recovery:** replay the log up to the last valid commit marker. The time is proportional to the log since the last checkpoint, which makes checkpoint frequency a recovery-time knob.
- **Snapshots:** a reader sees, for each page, the newest log copy at or before its snapshot, otherwise the main file. That requires an index from page to log frame. A checkpoint cannot move past the oldest reader, so **a long reader makes the log grow**.
- **Backup:** checkpoint up to a snapshot, then copy the main file plus the log tail under that snapshot.
- **Encryption:** pages are rewritten in place, so every write needs a fresh nonce stored in the page, for example from the log frame sequence number. It is workable, and a property to get exactly right.
- **Expected costs:** one sequential flush per commit, so the best single-row commit latency; the cost is checkpoint work, plus a second copy of every changed page.

### 2.C Log-structured merge tree (RocksDB's, LevelDB's shape)

- **How it works:** writes go to a log and an in-memory sorted table. When that fills it is written out as an immutable sorted file, and background **compaction** merges files level by level.
- **Recovery:** replay the log into the in-memory table.
- **Snapshots:** a sequence number; compaction keeps versions a live snapshot needs.
- **Reads:** a point read may consult several files, so it relies on bloom filters. A range scan merges several sorted inputs.
- **Backup:** sorted files are immutable, so a backup is the set of files plus the manifest at a snapshot (hard links where available).
- **Encryption:** files are written once, so nonces are easy.
- **Expected costs:** the best random-write and bulk-ingest throughput; worse point-read tail latency; space and CPU spent on compaction; the most moving parts of the three.
- **Why it is still a candidate:** gBASIC's ETL work (`intake`, xlsx pipelines) is write-heavy ingestion, which is exactly where an LSM tree wins. If ingestion turns out to dominate the target workloads, it should win the comparison.

### 2.D Why AmorphDB's storage is prior art, not a fourth engine

AmorphDB (`~/development/amorphdb/internal/storage`) stores attributes, instances and values in separate fixed-record files. Each instance links to the older one, values are deduplicated and reference-counted, and compaction relocates values. **Its instance chain is where G-SQL's temporal model came from**, and §4 borrows it directly, including the author on every version. As a general engine it does not fit, for reasons that come from what G-SQL needs rather than from any defect:

- G-SQL needs **ordered range indexes** (for `ORDER BY`, range predicates and joins), where AmorphDB's index is a hash index.
- G-SQL needs **atomic multi-row transactions** across tables and indexes. Each AmorphDB store flushes its own file, and nothing makes one change atomic across files (confirmed by its author, 2026-10-21).
- AmorphDB's compaction hands write authority to mesh peers while it runs, and an embedded engine has no peers.

**Decided (2026-10-21):** AmorphDB is inspiration and lessons learned; G-SQL's storage is a new development, sharing no code with it.

## 3. Components shared by every candidate

These are designed once and used by all three spikes, so the comparison measures the engines and not three different encodings.

### 3.1 Page format

- **Header:** magic and format version, page type, page number, the transaction id (or log sequence number) that wrote it, and a **checksum** over the whole page. CRC32C has hardware support on x86 and ARM; measure it against xxHash before choosing.
- **Reserved from version one:** 12 bytes of nonce and 16 bytes of authentication tag, unused while encryption is off (§6.4 of the main document).
- **Page size** is measured, not chosen: 4 KiB, 8 KiB and 16 KiB.
- **Values larger than a page** go to overflow pages. The threshold is measured, since it moves the balance between point-read speed and space.

### 3.2 Key encoding: byte order must equal gBASIC order

A tree compares keys with `memcmp`, so each key is encoded such that **byte order is exactly the order gBASIC's own comparison gives**. That is §9's critical invariant from `gsql_design.md`; an index that disagrees with the language returns wrong answers. The alternative, a typed comparator called per comparison, is slower and blocks prefix compression. Prior art: the order-preserving encodings of FoundationDB's tuple layer and CockroachDB.

- **A type tag first**, with a fixed order between kinds.
- **`unknown` sorts last**, ascending, matching gBASIC's ruling on absences. Descending order is a separate encoding (inverted bytes) rather than a reversed scan, so a composite index can mix directions.
- **Numbers are the hard case.** An exact integer beyond 2^53 and a double must interleave in mathematical order, because gBASIC compares them mathematically: `9007199254740993 > 9007199254740992.0`. The standard trick of flipping a double's bits orders doubles alone. A unified encoding (sign, magnitude class, integer part, fraction) is required, and NaN and signed zero need a stated place. **Open.**
- **Strings:** UTF-8 bytes, which order by codepoint, with terminator escaping so a prefix sorts before its extensions. Other collations are later work (§9).
- **Money:** a `MONEY(USD)` column holds one currency, so its key is the exact minor-unit integer. An untyped `MONEY` column orders by (currency, amount), since gBASIC refuses to order different currencies. **Open:** whether an untyped money column may be indexed at all.
- **Dates and times:** the existing exact representations, encoded as integers.
- **The oracle is gBASIC itself:** generate value pairs, encode both, and require `memcmp` order to equal gBASIC's comparison. Include the exactness edge, every pair of kinds, `unknown` in both directions, and money. Nothing about this encoding is accepted on reasoning alone.

### 3.3 Row encoding

A row value is the gBASIC values of its columns. The format is versioned and self-describing per column, so `ALTER TABLE ADD COLUMN` does not rewrite old rows; missing trailing columns read as their default. It reuses the type knowledge of `serialize`, which requires `exact_number_design.md`'s increment 4 (exact integers through the binary serializer) to land first.

### 3.4 The I/O layer, and why it is not optional

Every candidate does all file I/O through one small layer (open, read at, write at, flush, truncate, rename, directory flush), in the role SQLite's VFS plays. That layer is what makes the crash harness possible (§5.3), and it is where platform durability differences are handled once:
- `fdatasync` on Linux;
- `F_FULLFSYNC` on macOS, because plain `fsync` there does not reach the platter;
- `FlushFileBuffers` on Windows;
- flushing the **directory** after creating or renaming a file, which crash-consistency research (the ALICE study, OSDI 2014) found applications routinely forget.

## 4. The version store (temporal history, §10D)

The same in every candidate, and taken from AmorphDB's instance chain:

- The **current** value of a temporal column lives in the row, so ordinary reads never touch history.
- An update appends the **old** value to a version tree keyed by (table, row key, column, commit id). The row keeps no pointer; the newest older version is found by a seek to the largest commit id below now. This avoids rewriting the row twice and keeps backward-linked semantics.
- Each version carries the **commit id** (not a timestamp: the clock is second-resolution and commits are ordered), the commit's **author** (the `principal()` that made it, as AmorphDB records the author of every instance) and the commit time.
- `AS OF t` maps t to a commit id through a small commit-time index, then seeks.
- Measured with the candidates: the cost of history on update throughput, and the `AS OF` read cost against depth of history.

## 5. The evaluation harness

### 5.1 Workloads

**Decided (2026-10-21): G-Dash is the representative application now; WorkSplicer will be more so later.** WorkSplicer is being redesigned, so its profile below is provisional and is revisited when its design settles. Its eventual role, work orchestration plus a platform for enterprise vertical applications, makes it the long-term reference.

#### 5.1.1 G-Dash, the primary profile (read from its source)

G-Dash's staging store (`gdash/src/gdash_store.bas`) shows its data pattern directly:

- **Refresh is whole-dataset replacement.** A dataset is rebuilt from an ODBC or PostgreSQL source into a staging file (`materialize`), and made live by **one atomic rename** (`swap`). Readers never see a half-built dataset.
- **Reads are read-only analytic queries across several datasets.** Up to 10 dataset files are attached **read-only** to one connection, and queried with filters, `GROUP BY` aggregates and column samples. Some of the SQL is **written by a model** (`nlq`), and the read-only attach is a deliberate second line of defence: with the SQL check bypassed, a generated `delete` is still refused by SQLite.
- **Polling:** the browser polls a small JSON endpoint, so many small concurrent reads arrive through `workers: N` processes, which are owner clients under §6.1.
- **The money boundary is hand-built:** about 100 lines (`to_minor` and its helpers) carry money and integers above 2^53 through SQLite as decimal text, because SQLite's binding loses them. **G-SQL exists to delete that code**, and doing so is an acceptance case (§6).

What that asks of the engine, beyond the generic workloads:

| id | workload | why |
|---|---|---|
| G1 | **replace a whole table atomically** while readers keep reading the old one, at 10⁵ and 10⁷ rows | G-Dash's refresh. Under §2.A this is a new tree plus a root swap; under §2.B and §2.C it is a bulk write plus a catalog change. Readers' snapshots must keep the old table alive until they finish |
| G2 | aggregate scans (`GROUP BY` over a few columns, filters) across several tables, during G1 | the dashboard read path |
| G3 | many small concurrent reads through the owner socket, 8 to 64 clients | the poll endpoint, and the §5.5 round-trip gate in its real shape |
| G4 | a **read-only transaction refusing every write at the engine level** | G-Dash's safety net for model-written SQL must survive the move off SQLite. It is cheap at the storage interface (a read-only snapshot has no write path) and impossible to retrofit safely above it |

**Open, raised by G-Dash:** G-Dash uses **one file per dataset** so that a refresh is one rename and a dataset can be attached read-only. In G-SQL, the same guarantees can come from one database with snapshot-isolated table replacement (G1) and read-only transactions (G4), or from attaching several G-SQL files. This is §7's single-file question with a concrete case attached.

**Known:** at most 11 datasets in one query today, but that is SQLite's attach cap (one opened plus ten attached), not a G-Dash requirement. **Asked, awaiting an answer:** representative volumes (rows and width per dataset, datasets per deployment, refresh frequency and duration, read load, query shape), in `gdash/docs/platform_ask_gsql_volumes.md` (2026-10-21).

#### 5.1.2 WorkSplicer, the provisional future profile

Work orchestration has a different shape from dashboards: **many small write transactions** (state transitions on work items), a **queue claim** (find the next ready item and mark it taken, atomically), an **audit history** of every transition (§4's version store), and **per-scope numbering** (`gsql_design.md` §10E). Provisionally:

| id | workload | why |
|---|---|---|
| S1 | claim the next ready item by index order and update its state, one transaction per claim, 1 to 64 clients | the queue; the single writer makes a claim a plain write transaction, with no row locks to design |
| S2 | state-transition updates with a temporal state column | §4's version-store cost under the update pattern orchestration actually has |
| S3 | scoped serial allocation under concurrency | §10E's numbering |

#### 5.1.3 estateforge: realistic data, and an answer key no engine wrote

`estateforge` (`~/development/estateforge`) generates a fabricated corporate estate from one declaration: tables, views, procedures, ETL flows and rows. It comes at three scales (25, 116 and 500 tables), in two vocabularies (energy trading with Enron-corpus names, and a US core bank), with named pathologies planted on purpose (null regions, undeclared foreign keys, SCD2 dimensions, denormalised copies, and others). Its distinctive property is that the declaration produces **the answer key as well as the database**: it computes each question's answer by folding over the generated rows, never by running SQL.

Its role in G-SQL, by phase:

- **Phase 0a (this document):** realistic row shapes, widths, key distributions and a 500-table catalog for the workloads, in place of uniform random keys. Its value distributions (trend, seasonality, planted anomalies) make compression and page-fill measurements honest.
- **The SQL layer, later, and the stronger use: a differential oracle.** Build the same estate in PostgreSQL and in G-SQL, run its questions in both, and compare each against the answer key. Agreement between two engines alone could be one shared mistake; the answer key was written by neither engine. `nlq`'s value tier in this repository already uses estateforge exactly this way, against PostgreSQL.
- **Semantics:** its `null_region` pathology and SCD2 dimensions exercise `unknown` and grouping under the SQL truth tables (`gsql_design.md` §3.1.1).

**Gap:** estateforge's demo estate is about 4,800 rows, and its row loading goes through SQL inserts (12.7 ms/row on SQL Server, from its EF-4 report). The volume workloads here need 10⁵ to 10⁷ rows. So either estateforge gains a bulk export mode that the harness loads directly, or the harness generates volume from estateforge's schemas and distributions. **Decide with estateforge's session** when the harness is designed.

#### 5.1.4 Generic workloads

Kept so the comparison is not shaped by one application:

| id | workload | what it exposes |
|---|---|---|
| W1 | point reads by primary key, uniform and Zipfian | read path, cache behaviour |
| W2 | single-row write transactions, each durable | commit latency, flushes per commit |
| W3 | batched writes, 1,000 rows per transaction | amortised write path |
| W4 | bulk load of 10 million rows, sorted and random order | ingestion (§2.C's case) |
| W5 | range scans of 1, 100 and 10,000 rows | scan throughput |
| W6 | 80/20 read/write mix, with one reader holding a snapshot for 60 s | the long-reader cost per candidate |
| W7 | repeated updates of the same keys for an hour | space reclamation, file growth |
| W8 | 100 KiB values | overflow handling |
| W9 | W2 with a temporal column | the version store's cost |

Each at two sizes: data that fits in memory, and data several times larger than memory, with the page cache dropped between runs.

### 5.2 Metrics

- Throughput, with p50, p99 and p99.9 latency.
- Flushes per commit, and bytes written per logical byte (write amplification).
- File size against live data (space amplification), during and after W7.
- Pages read per point lookup (read amplification).
- **Recovery time**, against log size where one exists, which is the handoff cost.
- Backup duration, and the p99 impact on concurrent writers while it runs.
- Peak memory.

Every run records hardware, filesystem, page size and options. This machine (22 cores, 62 GiB RAM, NVMe, ext4) is one configuration, not the reference. At least one slower storage device must be measured, since NVMe flatters the designs that flush most.

### 5.3 Crash correctness: the gate

**A kill -9 alone does not test power loss.** After a process dies the operating system's page cache still writes its data out, so a kill-only harness never sees a lost or torn write. That is why the I/O layer (§3.4) exists. The harness uses it in three modes:

1. **Process crash:** kill -9 at the Nth I/O operation, for every N across a workload.
2. **Power loss:** at a chosen point, discard every write not yet flushed and stop.
3. **Torn write:** write only a prefix of the last unflushed page, in 512-byte sectors.

After each, recover and check against a **model**: the harness records every transaction whose commit *returned*, and the recovered state must equal the state after some prefix of all commits. That prefix must include every returned commit, and at most the one commit in flight may or may not appear. A checksum failure on a torn page must be detected, never read as data. Plus `check` passes.

**Gate:** zero violations across at least 10,000 injected crashes per mode per candidate, before any performance number is considered. A candidate failing the gate is fixed and rerun, or eliminated.

### 5.4 Group commit

The owner serves every client, so it can **group commits**: hold acknowledgements briefly and make one flush cover several clients' transactions. This changes the comparison, since it narrows 2.B's one-flush advantage over 2.A's two. W2 is measured with and without it, at 1, 8 and 64 concurrent clients.

### 5.5 The owner round trip and the handoff (§6.1 gates)

- **Round trip:** client to owner and back over the local socket, for an empty request, a point read and a batch of 100 rows, against the same operation called in-process. The protocol is accepted if the added cost is small next to the work of a typical query; the threshold is agreed after the first numbers rather than guessed now.
- **Handoff:** owner exits (cleanly, and by kill -9) → a client takes the lock → opens → recovers → serves. Total time, per candidate and per log size.

## 6. How the choice is made

1. **Gate:** crash correctness (§5.3). Absolute.
2. **Hard requirements:** online backup without stopping writers; bounded recovery time; a correct long-reader story; nonce uniqueness for encryption; atomic table replacement under live readers (G1); engine-enforced read-only transactions (G4).
3. **Acceptance for the chosen design, before the SQL layer is accepted:** G-Dash's staging store can be rewritten against G-SQL with its decimal-text money boundary deleted, and its results are identical.
4. **Weighed, with weights fixed before results are seen:** latency and throughput on the agreed workloads, space amplification, handoff time, and **implementation complexity**: lines of code, and the number of invariants a reader must hold to change it safely. Complexity counts because the engine is maintained for the life of the language.

**Predictions, written now so measurement can refute them:**
- 2.A wins recovery and handoff time (near constant), backup simplicity and the long-reader story, and is the simplest to make correct. It loses single-row commit latency without group commit, and space during W6.
- 2.B wins single-row commit latency and space efficiency; recovery time grows with the log.
- 2.C wins W4 by a wide margin and loses W1 tail latency, with the highest complexity.
- G-Dash's profile (G1, G2, G3) favours 2.A further: table replacement is a root swap and its readers keep their snapshot for free.
- **Overall prediction: 2.A, with group commit closing most of the commit-latency gap.** If 2.B or 2.C wins on the agreed workloads, that is the answer, and this paragraph stays as the record of what was expected.

## 7. Open questions

1. G-Dash volumes (§5.1.1); WorkSplicer's profile once its redesign settles (§5.1.2).
2. The unified number encoding: exact integers and doubles in one order, plus NaN and signed zero (§3.2).
3. Whether an untyped `MONEY` column may be indexed (§3.2).
4. *(Settled 2026-10-21: the §2.D reading is right, and G-SQL is a new development.)*
5. The round-trip threshold for accepting the single-protocol rule (§5.5), agreed after first numbers.
6. Whether a database should ever span several files, or always be one file plus at most a log. G-Dash's file-per-dataset arrangement (§5.1.1) and AmorphDB's per-zone files are the two precedents.

## 8. Reading list

- LMDB: Howard Chu's design papers, and the `mdb.c` meta-page and free-list logic.
- BoltDB: a smaller copy-on-write B+tree, easier to read than LMDB.
- SQLite: the file format, WAL mode, the VFS layer, and "How SQLite Is Tested" (its crash and I/O-error testing is the model for §5.3).
- PostgreSQL WAL and full-page writes (torn-page protection).
- RocksDB: leveled versus universal compaction, and the RocksDB tuning guide.
- Pillai et al., *All File Systems Are Not Created Equal* (OSDI 2014), on what applications wrongly assume about `fsync` and rename.
- FoundationDB's tuple-layer encoding and CockroachDB's key encoding (§3.2).
