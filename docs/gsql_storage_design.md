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

## 1. The storage interface (the seam) (specified 2026-10-22)

All candidates implement one interface, so one workload harness and one crash harness drive all of them. The relational layer (tables, indexes, the catalog) is built on it later and is not part of Phase 0a.

**Shape: an ordered key-value store with named trees and snapshot transactions.** This is how SQLite, LMDB and most relational engines divide the work: a table is a tree keyed by its primary key, and a secondary index is a tree keyed by (indexed value, primary key). Keys and values are **bytes** at this boundary; turning gBASIC values into bytes is a shared component (§3), so encoding cost is measured once and identically for every candidate.

**The interface lives inside the owner process (`gsql_design.md` §6.1), which is single-threaded.** Nothing below is called concurrently, so the interface has no locks of its own; "concurrent readers" means several open snapshots interleaved by the owner, not threads.

### 1.1 Operations

| group | operation | contract |
|---|---|---|
| database | `open(path, options)` | Opens or creates; **runs recovery** before returning; takes the file's exclusive lock and refuses a second opener with `BUSY`. Refuses a file whose format version is newer than it understands (`FORMAT`). Options: `create`, `page_size` (at creation only), `cache_bytes`, and the I/O layer to use (§3.4), so the harness can substitute its own |
| | `close(db)` | Refused while a transaction or snapshot is open; the owner closes those first |
| snapshots | `begin_read(db)` | A consistent view as of the **last durable commit** (§1.3). Cheap; any number may be open |
| | `begin_write(db)` | At most one at a time. A second call answers `BUSY` at once, and the owner queues the request (§1.4) |
| | `commit(txn, sync)` | Returns the commit's **commit id**, a strictly increasing integer that the version store (§4) and audit history key on. With `sync = true` it is durable on return. With `sync = false` it is ordered but not yet durable (group commit, §1.3) |
| | `sync(db)` | Makes every commit so far durable, returns the highest durable commit id |
| | `abort(txn)` / `release(snapshot)` | Always succeed |
| trees | `tree_create`, `tree_drop`, `tree_list` | In a write transaction. Names are byte strings; the catalog of trees is itself a tree |
| | `tree_replace(txn, target, source)` | **Atomically makes `source`'s contents `target`'s**, dropping the old ones. This is G-Dash's whole-dataset refresh (§5.1.1, G1): readers holding older snapshots keep reading the old contents until they release them |
| records | `get(txn, tree, key)` | The value, or `NOT_FOUND` |
| | `put(txn, tree, key, value, flags)` | `NO_OVERWRITE` refuses an existing key (`EXISTS`); `APPEND` promises the key is greater than every key in the tree, which lets a bulk load fill pages in order (G5) and is checked, not trusted (`ORDER`) |
| | `delete(txn, tree, key)` | Answers whether the key was there |
| cursors | `cursor_open(txn, tree)` | Within a snapshot or the write transaction |
| | `seek(cursor, key, mode)` | `mode` is `ge`, `gt`, `le`, `lt` or `exact`; answers whether a record is positioned |
| | `next`, `prev`, `key`, `value` | `key` and `value` are borrowed until the cursor moves or closes |
| | `cursor_put`, `cursor_delete` | Modify at the cursor's position, keeping that cursor valid; this is how an `UPDATE` walks and writes |
| maintenance | `backup(snapshot, dest)` | A consistent copy as of the snapshot, while writes continue (`gsql_design.md` §6.3). Written to a temporary file, flushed, then renamed into place, so an interrupted backup never leaves a file that looks complete |
| | `check(db)` | Verifies every page checksum and structural invariant; returns a report, never repairs |
| | `stats(db)` | Pages read and written, bytes written, flushes, cache hits: what §5.2's write and read amplification are computed from |

### 1.2 Limits

- **A key is at most 1,024 bytes.** Keys live in interior pages, and a bound is what keeps fan-out predictable. A longer key is refused (`TOO_LARGE`), never truncated. The relational layer keeps index keys within it; the bound is revisited if a real schema hits it.
- **A value has no size limit** beyond the file's; large values go to overflow pages (§3.1).
- **A write transaction's memory is bounded by `cache_bytes`, however large the transaction.** A candidate must spill dirty pages before commit rather than hold them, which is G-Dash's third want (bounded-memory writes, G5) and is asserted by measuring peak memory during a 10⁶-row transaction.

### 1.3 Durability and visibility

- **A commit becomes visible to new snapshots only once it is durable.** Under group commit (§5.4), a commit made with `sync = false` is held back from other readers until a `sync` covers it, so **no client ever reads data a crash could remove**. The writing transaction itself sees its own writes. The cost is that a second client's read-after-write waits up to one group-commit interval, which §5.4 measures.
- **A failed flush makes the database unusable until it is reopened, and the flush is never retried.** After an `fsync` failure the operating system may already have discarded the dirty pages it could not write, so a retry that succeeds proves nothing (PostgreSQL's 2018 "fsyncgate"). The commit reports `IO`, every later operation reports `FAILED`, and reopening runs recovery from what is really on disk.

### 1.4 Errors, as values

Every operation answers success or one of a closed set, so the owner can map each to a gBASIC error without guessing:

| error | meaning |
|---|---|
| `NOT_FOUND` | no such key |
| `EXISTS` | `NO_OVERWRITE` met an existing key |
| `ORDER` | an `APPEND` key was not greater than the last |
| `BUSY` | a write transaction is already open, or another process holds the file |
| `TOO_LARGE` | a key over the limit |
| `INVALIDATED` | a cursor whose tree was modified other than through it; it never returns stale data |
| `CORRUPT` | a checksum or structural check failed, naming the page |
| `IO` | an operating-system error, naming the operation and errno |
| `FULL` | the disk is full; the transaction is aborted and the database stays usable |
| `FAILED` | an earlier flush failed; reopen to recover |
| `FORMAT` | not a G-SQL file, or a newer format |

The owner's policy for `BUSY` on `begin_write`, decided here because it changes what a client sees: requests are **queued in arrival order**, and a write transaction **idle longer than a timeout** (proposed 30 s, configurable) is aborted, so one slow client cannot stop every writer. The aborted client is told why.

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

### 3.2 Key encoding: byte order must equal gBASIC order (designed 2026-10-22)

A tree compares keys with `memcmp`, so each key is encoded such that **byte order is exactly the order gBASIC's own comparison gives**. That is §9's critical invariant from `gsql_design.md`; an index that disagrees with the language returns wrong answers. The alternative, a typed comparator called per comparison, is slower and blocks prefix compression. Prior art: the order-preserving encodings of FoundationDB's tuple layer and CockroachDB.

**gBASIC's ordering, measured first** (a scratch program per rule, 2026-10-22):

| rule | measured |
|---|---|
| exact integers and doubles interleave by mathematical value | `9007199254740993 > 9007199254740992.0` is true, and `sort([2, 1.5, 9007199254740993, 9007199254740992.0])` is correct |
| a number equals its value in the other form | `9007199254740993 - 1 = 9007199254740992.0` is true |
| −0 equals 0, but is a different value | `0.0 = -0.0` is true, and `string(-0.0)` is `-0` |
| strings order by byte, which is codepoint order for UTF-8 | `["B","Z","a","b","e","é"]`, and `"a" < "a"+chr(0) < "ab"` |
| `false < true` | true |
| money of one currency orders by its exact value, guard digits included; different currencies **refuse** to order | `{USD}"1.00001" > {USD}"1.00"` is true; USD against EUR raises |
| an absence sorts **last** ascending and **first** descending | the 2026-10-07 ruling, in `docs/reference.md` |
| a date orders against a datetime, yet a date never **equals** the datetime at its midnight | both measured |
| **NaN has no place at all** | every comparison with it is false, so `sort` puts it wherever the input had it (`sort([1, nan, 1])` is unsorted) — a language defect, recorded in DOGFOOD 2026-10-22 |

**The encoding, per component.** Every component is self-delimiting, so a composite key is the concatenation of its components, and a `DESC` component is the same bytes **inverted**. Inversion is what puts an absence first under `DESC`, which is exactly the documented rule, with no second mechanism.

- **Absence** (`unknown`; a bound `nothing` was already normalized to it): one byte `0xF0`, above every value's tag, so it sorts last ascending.
- **Boolean:** tag `0x10`, then `0x00` or `0x01`.
- **Number: a class byte, then for finite non-zero values a 2-byte exponent and an 8-byte fraction**, 11 bytes in all.
  - Classes in order: `0x21` −inf, `0x22` negative, `0x23` zero, `0x24` positive, `0x25` +inf, `0x26` NaN.
  - **Every number is a dyadic rational**, an int64 as much as a double, so each is written as 1.f × 2^E, normalized. An int64 needs at most 63 fraction bits and a double 52, so a 63-bit fraction, left-aligned, holds either **without loss**. E spans −1074 (the smallest subnormal) to 1023 and is stored big-endian with a bias of 1100. A negative number's exponent and fraction bytes are inverted, so a larger magnitude sorts first.
  - **The key encodes the mathematical value, not the representation.** Exact 5 and double 5.0 produce the same key, which agrees with gBASIC's `5 = 5.0`. Whether a value is exact lives in the **row**, so an index-only scan must not hand back a value without consulting the row.
  - **−0 is keyed as zero**, agreeing with `0.0 = -0.0`; the row keeps the −0.
  - **NaN gets a fixed place: after +inf, equal to itself**, which is PostgreSQL's rule. That is the "not distinct" equality `gsql_design.md` §3.1.1 already requires for `GROUP BY`, `DISTINCT` and the language's own `sort`/`unique`/`contains`, so the index is not a second rule. Expression equality stays IEEE (`nan = nan` is false), so the planner **must not** turn `WHERE x = <NaN>` into an index seek; it is a residual filter that matches nothing.
- **String:** tag `0x30`, then the UTF-8 bytes with `0x00` written as `0x00 0xFF`, then the terminator `0x00 0x00`. That preserves the measured `"a" < "a"+chr(0) < "ab"`. Other collations are later work (`gsql_design.md` §9).
- **Money, typed column (`MONEY(USD)`):** tag `0x40`, then the exact stored integer (its guard digits included) as a sign-flipped big-endian int64. The currency's scale is fixed, so integer order is value order.
- **Money, untyped column (`MONEY`): decided, it may be indexed.** Tag `0x41`, the 3-letter currency code, then the amount as above. Keys group by currency; equality lookups and ranges **within one currency** use the index. A sort or range that would order **different currencies** against each other raises at execution, exactly as gBASIC does, so the index's grouping by code is a storage detail and never an answer.
- **Dates, times and datetimes:** one tag each (`0x50`, `0x51`, `0x52`), then the exact integer representation the value already has, sign-flipped and big-endian. **A column holds one temporal kind.** gBASIC orders a date against a datetime but never makes them equal, which is not a total order an index can hold, so G-SQL refuses a value of another temporal kind on write rather than mixing them.

**The oracle is gBASIC itself, and nothing above is accepted on reasoning alone.** Phase 0a's first artefact is the encoder with a test that generates value pairs, encodes both, and requires `memcmp` order to equal gBASIC's comparison for every pair. It covers: the exactness edge (2^53 ± 1, ±2^63), subnormals, ±0, ±inf, every kind against itself, absence in both directions, money with guard digits, and composite keys mixing directions. **NaN is the one exception, and it is a stated one:** until the language gets the total order, gBASIC has no order for NaN to agree with, so the test asserts the documented position rather than comparing.

### 3.3 Row encoding

A row value is the gBASIC values of its columns. The format is versioned and self-describing per column, so `ALTER TABLE ADD COLUMN` does not rewrite old rows; missing trailing columns read as their default. It reuses the type knowledge of `serialize`, which requires `exact_number_design.md`'s increment 4 (exact integers through the binary serializer) to land first.

**Record-valued columns** (`gsql_design.md` §3.5) are encoded the same way, recursively, with each field's name before its value. Two things are measured here rather than assumed:
- **field-name overhead**, since every row repeats its field names, against an alternative storing names once per column;
- **field extraction cost**, since a field index and a residual filter both read one field without decoding the whole record, which argues for an encoding with field offsets.

A **field index** keys on the field's value with the §3.2 encoding, and an absent field is keyed as an absence.

### 3.4 The I/O layer, and why it is not optional (specified 2026-10-22)

Every candidate does all file I/O through one small layer, in the role SQLite's VFS plays. It has **two implementations behind one interface**: the real one, which the performance measurements use, and a simulated one, which the crash harness uses (§5.3).

**Operations:** `open(path, create | exclusive | read_only)`, `read_at(file, offset, n)`, `write_at(file, offset, bytes)`, `sync(file)`, `size(file)`, `truncate(file, n)`, `close(file)`, and on paths `rename(from, to)`, `remove(path)`, `sync_dir(dir)` and `lock(file, nonblocking)`. A read past the end answers short rather than failing.

**The durability contract is the minimal one POSIX actually gives, and an engine may assume nothing stronger.** Every candidate must be correct under exactly these rules:

1. **A write is durable only after `sync` of that file returns success.**
2. **A file's creation, a rename and a removal are durable only after `sync_dir` of its directory.** Crash-consistency research (the ALICE study, OSDI 2014) found applications routinely forgetting this.
3. **A write is atomic only per 512-byte sector.** A page write interrupted by power loss may leave any prefix of its sectors new and the rest old.
4. **Unflushed writes may reach the disk in any order**, and any subset of them may.
5. **After a failed `sync`, the state of every unflushed write is unknown** (§1.3).

Behaviour that ext4's default `data=ordered` mode, or a particular SSD, happens to give is **not** relied on: a design correct only on the developer's filesystem is the failure this contract exists to prevent.

**The real implementation** handles platform differences once:
- `pread`/`pwrite` and `fdatasync` on Linux;
- `F_FULLFSYNC` on macOS, because plain `fsync` there does not reach the platter;
- `FlushFileBuffers` on Windows, and directory durability through its own route there;
- `flock` for the exclusive lock.

**The simulated implementation** keeps files in memory, split into a **durable** image and a list of **pending** operations since the last `sync`. It records every operation in order, numbered, and can stop the world at any operation and produce the disk as a crash would leave it (§5.3). It also injects failures on request: `sync` failing, `write_at` answering `ENOSPC`, `read_at` answering `EIO`.

**The risk of two implementations is that they diverge**, which would make the crash harness test a disk that does not exist. Two defences: the simulated layer's own tests check it against the five rules above, and every candidate is also crash-tested against the **real** layer by kill -9, the one crash mode a real kill can produce.

## 4. The version store (temporal history, §10D)

The same in every candidate, and taken from AmorphDB's instance chain:

- The **current** value of a temporal column lives in the row, so ordinary reads never touch history.
- An update appends the **old** value to a version tree keyed by (table, row key, column, commit id). The row keeps no pointer; the newest older version is found by a seek to the largest commit id below now. This avoids rewriting the row twice and keeps backward-linked semantics.
- Each version carries the **commit id** (not a timestamp: the clock is second-resolution and commits are ordered), the commit's **author** (the `principal()` that made it, as AmorphDB records the author of every instance) and the commit time.
- `AS OF t` maps t to a commit id through a small commit-time index, then seeks.
- Measured with the candidates: the cost of history on update throughput, and the `AS OF` read cost against depth of history.

## 5. The evaluation harness

### 5.1 Workloads

**How databases fit (clarified 2026-10-21).** A G-SQL **database** is one file (plus at most a log) holding **many tables, views and indexes**, as a SQLite file or a PostgreSQL database does. One owning process serves one database however many tables it holds (`gsql_design.md` §6.1). The normal application opens **one** database, sometimes a few (one per tenant, or an archive). WorkSplicer, with hundreds of tables and views in one database, is the shape the engine is designed for.

**Four tiers, and the first is the yardstick (decided 2026-10-22).** G-SQL serves many applications, and G-Dash and WorkSplicer are its first, not its last. So the evaluation is built **application-independent first**:

1. **Standard workloads (§5.1.0), the primary yardstick:** YCSB, a TPC-C transaction mix and the Star Schema Benchmark. Their shapes are published and widely run, so the comparison rests on workloads nobody here designed. The reference engines (SQLite, LMDB, RocksDB) are run **on the same machine under the same workloads**, because published figures come from other hardware and do not transfer.
2. **Engine-specific workloads (§5.1.4)** that the standards do not reach: a reader holding a snapshot for a minute, space reclamation, large values, temporal history.
3. **Application profiles, as named examples:** G-Dash (§5.1.1), the first consumer, with real code and supplied numbers; and WorkSplicer (§5.1.2), the more demanding future case, which uses **one database per tenant** and is provisional while it is redesigned.
4. **estateforge (§5.1.3):** messy, realistic schemas and data, and later an answer key for the SQL layer.

**No single application's profile may dominate the weighting** (§6), which is fixed before results are seen. An application profile can veto a design that fails it outright, but it cannot choose among designs that all serve it.

#### 5.1.0 Standard workloads

**YCSB** (Yahoo! Cloud Serving Benchmark) is the key-value standard, and maps directly onto the storage interface (§1). Records of ten 100-byte fields, Zipfian key choice unless stated:

| workload | mix | exposes |
|---|---|---|
| A | 50% read, 50% update | update-heavy contention |
| B | 95% read, 5% update | the read-mostly common case |
| C | 100% read | the read path alone |
| D | 95% read, 5% insert, reads favouring the newest records | recency |
| E | 95% short range scans (up to 100 records), 5% insert | scans under inserts |
| F | 50% read, 50% read-modify-write | a transaction reading then writing |

Plus YCSB's **load phase**, an ordered bulk insert, which is the ingestion case §2.C exists for.

**TPC-C** is the transactional standard: nine tables (warehouse, district, customer, history, new_order, order, order_line, item, stock), sized by the number of warehouses, and five transactions in a fixed mix: New-Order (~45%), Payment (~43%), Order-Status, Delivery and Stock-Level (~4% each). It is the workload with **real multi-table transactions**, which YCSB has none of. Phase 0a runs it **hand-coded over the storage interface** (each table a tree with composite keys), because the SQL layer does not exist yet. That makes it **TPC-C-like**, as sysbench-tpcc and HammerDB's default are: the transaction mix without the specification's terminal think times, so its throughput is **not comparable to an official tpmC** and must never be reported as one.

**The Star Schema Benchmark (SSB)** is the analytic standard: one fact table (lineorder, about 6 million rows at scale factor 1) and four dimensions, with thirteen queries in four flights. It is a simplification of TPC-H that keeps the scan-filter-aggregate shape. Phase 0a runs **flight 1** (a filtered scan of the fact table joined to one dimension and summed), hand-coded, since that measures the scan path dashboards depend on. The full SSB, and TPC-H's twenty-two queries, belong to the **SQL layer's** evaluation later, run against PostgreSQL, SQLite and DuckDB as references.

**The reference engines** are the honest comparison: SQLite (installed), LMDB and RocksDB (Ubuntu packages, installed when the harness is built). They are measurement tools here, never shipped. Each is driven by the same workload generator through a thin adapter, with its durability set to match G-SQL's: synchronous commit, and SQLite in WAL mode with `synchronous=FULL`. Otherwise one engine would be measured flushing and the other not.

#### 5.1.1 G-Dash, the first consumer's profile (read from its source)

G-Dash's staging store (`gdash/src/gdash_store.bas`) shows its data pattern directly:

- **Refresh is whole-dataset replacement.** A dataset is rebuilt from an ODBC or PostgreSQL source into a staging file (`materialize`), and made live by **one atomic rename** (`swap`). Readers never see a half-built dataset.
- **Reads are read-only analytic queries across several datasets.** Up to 10 dataset files are attached **read-only** to one connection, and queried with filters, `GROUP BY` aggregates and column samples. Some of the SQL is **written by a model** (`nlq`), and the read-only attach is a deliberate second line of defence: with the SQL check bypassed, a generated `delete` is still refused by SQLite.
- **Polling:** the browser polls a small JSON endpoint, so many small concurrent reads arrive through `workers: N` processes, which are owner clients under §6.1.
- **The money boundary is hand-built:** about 100 lines (`to_minor` and its helpers) carry money and integers above 2^53 through SQLite as decimal text, because SQLite's binding loses them. **G-SQL exists to delete that code**, and doing so is an acceptance case (§6).

**Volumes, answered by G-Dash** (`gdash/docs/platform_answer_gsql_volumes.md`, 2026-10-21). G-Dash has **never been deployed**, so most figures are design-intent guesses; the answer labels each as measured, structural (enforced by code) or a guess, and that labelling is kept here:

| quantity | figure | kind |
|---|---|---|
| rows per dataset | typically 10²–10⁴, largest ~10⁶ | guess |
| row width | 3–10 columns, ~50–150 bytes; short text keys, ISO dates, integers | guess |
| money columns | 10–30% of columns; integers above 2^53 rare | guess |
| datasets per dashboard | 2–5, occasionally 15–20; capped at 11 today by SQLite | guess / structural |
| datasets per deployment | 10²–10³ files, multiplied by tenants | guess |
| datasets one query reads | 1–2, though it **attaches all** of its dashboard's datasets, because nothing parses the SQL to say which | structural |
| refresh cadence | 5–15 minutes for operational figures, nightly for most; always a whole-dataset rewrite, overlapping with reads by design | guess / structural |
| refresh duration | **not measured** | — |
| poll | every 5 s per tab, and it **never reads a dataset** (it reads a version file); 30 tabs cost one worker nothing measurable | measured |
| page load | 57 ms per full page on one worker, ~20 pages/s; a burst of 20 loads finishes in under 1 s | measured |
| peak query rate | ~10–100 dataset queries/s, near zero otherwise | guess |
| query shape | almost all `sum … group by` over a whole dataset; filters are an equality or range on one or two columns; results of 1–100 rows | structural / guess |

**What this changes in the evaluation:**

- **G-Dash is not a volume stress.** Its largest dataset is about 10⁶ rows of ~100 bytes, roughly 100 MB. The 10⁷-row workloads stay, for WorkSplicer and the generic set, but G-Dash's measurements are taken at 10², 10⁴ and 10⁶.
- **Scans and opening matter more than indexes.** G-Dash's own conclusion: "an index matters much less than scanning fast and opening/attaching cheaply". Because its datasets are written once, sequentially, and never updated in place, page locality for scans should be excellent under every candidate. The measurement should confirm that rather than assume it.
- **Per-request cost counts as much as query speed.** Workers share no state, so every visual query opens, attaches, queries and closes. Under §6.1 that becomes connecting to an owner and starting a snapshot.

The G-Dash workloads, revised:

| id | workload | why |
|---|---|---|
| G1 | **write a fresh dataset and make it live atomically** while readers keep the old one, at 10², 10⁴ and 10⁶ rows | G-Dash's refresh. Readers' snapshots keep the old data until they finish ("stale but coherent") |
| G2 | whole-dataset `sum … group by`, with and without a one-column filter, at the same sizes, during G1 | the dominant query |
| G3 | the per-request cost: connect, snapshot, a one-row query, release; at 10–100 requests/s from 1–8 workers | G-Dash's "cheap open" requirement, and §5.5's round-trip gate in its real shape |
| G4 | a **read-only transaction refusing every write at the engine level** | G-Dash's safety net for model-written SQL must survive the move off SQLite |
| G5 | **bounded-memory bulk write**: stream 10⁶ rows into one transaction with peak memory recorded | G-Dash's third want: the dataset ceiling should be disk, not RAM. The engine must take a large write without holding it all; under §2.A that means writing dirty pages out before commit |
| G6 | a query naming 2 of 20 datasets, against one naming all 20 | G-Dash's second want: no attach cap, and touching only what a query names |

**What G-Dash wants, in its own order:** a native money type with its scale stored with it; reading across many datasets with no cap, touching only those a query names; and bounded-memory writes. It **does not want** partial or incremental refresh: whole replacement is its coherence model, and G-SQL should keep that cheap rather than replace it.

**Money comparisons in model-written SQL, raised by the answer:** today stored values are minor units, so generated SQL compares cents to dollars, and G-Dash has to warn about it. Native money removes the unit confusion, but leaves one question for `gsql_design.md` §3.3: what `amount > 100` means when `amount` is `MONEY(USD)` and `100` is a plain number. gBASIC's own answer for that comparison must be the SQL answer.

### 5.1.1a G-Dash's file-per-dataset arrangement is a SQLite habit, not a G-SQL requirement

This section is about **one application's arrangement**, and does not change the general model above. G-Dash keeps **one file per dataset, 10²–10³ of them per deployment**, and makes a refresh live by renaming a fresh file over the old one. Under `gsql_design.md` §6.1 every database file has its own owning process, so this arrangement would mean:
- **hundreds of owner processes**;
- a dashboard query reading several datasets would span **several owners**, which is a distributed query;
- a rename-in would need the old file's owner to notice its file was replaced and hand over.

None of that is worth building. Two ways out:

1. **Recommended: datasets are tables in one database**, one per deployment or one per tenant. A refresh writes a new table in a single transaction (G5) and swaps it in through the catalog (G1); readers keep their snapshots of the old table, which is exactly G-Dash's stale-but-coherent model without a rename. All three of G-Dash's wants then follow from the design rather than being added:
   - one owner per deployment, not per dataset;
   - cross-dataset queries with **no attach cap**, where the planner touches only the tables a query names;
   - per-request cost is a socket round trip, not opening eleven files.

   The costs, stated: a dataset is no longer a file an operator can copy, inspect or delete on its own (the engine's backup and integrity check, §6.3 of the main document, replace that); and one database holds every dataset, so corruption and recovery are per database. One database per tenant keeps tenants apart, which is also what G-Dash's multi-tenant design needs.
2. **Alternative: one owner process per directory**, owning many files and treating each as a dataset it can attach. This keeps G-Dash's file model, at the price of an owner that must manage many files, detect a file renamed in under it, and coordinate snapshots across them.

**This is a decision for G-Dash and the project together.** G-Dash used a file per dataset because, under SQLite, a rename was the easy way to get an atomic swap and a read-only attach. G-SQL provides both inside one database: atomic table replacement under live readers (G1) and engine-enforced read-only transactions (G4). Option 1 changes G-Dash's storage design; G-Dash's own seam (`gdash_store.bas` is the only module that touches storage, and its header says a backend swap "is this module's rewrite") is what makes that affordable.

#### 5.1.2 WorkSplicer, the provisional future profile

**The core schema, as Matthew described it (2026-10-22), provisional while WorkSplicer is redesigned.** One database per tenant (§5.1).

| table | columns |
|---|---|
| `tasks` | task_id, label, start_time, due_time |
| `task_triggers` | task_id, condition, consequence |
| `task_log` | task_log_id, task_id, user_id, log_time, message |
| `task_assets` | asset_id, asset_type_id, list_position, label, detail |
| `asset_types` | asset_type_id, name |
| `task_users` | task_id, user_id, enabled_time, disabled_time, role (observer, performer or assessor) |
| `users` | user_id, super_id, display_name, name |

**Read as I have read it, to be confirmed:** the second `task_users` in the description is taken to be `users`, and `task_assets` is taken to carry a `task_id` (its `list_position` orders assets within a task).

**What the schema asks of the engine:**

- **Work by time.** "What starts or falls due now" is a range scan on `start_time` and `due_time`, which a scheduler runs on every tick, so it is an index the engine must keep cheap under constant small writes.
- **A person's current work.** "My tasks as performer, now" joins `task_users` to `tasks` on `user_id` and `role`, filtered to assignments where `enabled_time <= now` and `disabled_time` is absent or later. That filter is on an interval, so an index on `(user_id, role)` narrows the rows and the interval is a residual filter (`gsql_design.md` §5.2).
- **Two different kinds of time, which must not be conflated.** `task_users`' enabled/disabled times are **business-effective time**, modelled explicitly in the data, saying when an assignment holds. G-SQL's `TEMPORAL` columns (§4) are **recorded time**, saying when a value changed and who changed it. WorkSplicer needs both: the assignment interval as data, and the audit of who edited it.
- **A hierarchy.** `users.super_id` makes "everyone under this supervisor" a **recursive** query. `gsql_design.md` defers recursive CTEs to its last phase (§4.2), and WorkSplicer argues for moving them earlier. The alternative is to walk the hierarchy in gBASIC, which is also how it is done today on databases without recursion.
- **An append-only log.** `task_log` grows forever, written by every action and read per task in time order, so its key is `(task_id, log_time, task_log_id)`. `task_log_id` is a natural **per-task serial** (`SERIAL OF task_id`, §10E). How much of it the `TEMPORAL` version store would duplicate is a question for WorkSplicer's design, not the engine's.
- **An ordered list.** `list_position` renumbers every later asset on a mid-list insert unless positions leave gaps or are fractional. That is the application's choice, but the engine should make the renumbering cheap: one write transaction over a handful of rows.
- **Triggers.** `condition` → `consequence` reads as work for the `rules` library. The storage question is how triggers are **found**: by time, by event, or by scanning all of them on each tick. Scanning is a full read of `task_triggers` per tick, which bounds how often a tick can run.

**The workloads this gives:**

| id | workload | why |
|---|---|---|
| S1 | **due-work scan:** a range scan on `due_time` (and `start_time`) every tick, under a steady stream of small writes | the scheduler's read path, contending with ordinary updates |
| S2 | **assignment lookup:** a user's current tasks by role, with the interval as a residual filter, at 10², 10⁴ and 10⁶ assignments | "my work", the commonest interactive query |
| S3 | **log append:** single-row inserts into `task_log` under concurrency, keyed per task with a scoped serial | the write-heavy path; group commit's main beneficiary (§5.4) |
| S4 | **per-task history:** a task's log in time order, 10 to 10⁴ entries | the read side of S3 |
| S5 | **hierarchy walk:** everyone under a supervisor, 3 to 10 levels deep | whether recursion is needed in the first release |
| S6 | **list edit:** insert an asset mid-list and renumber, in one write transaction | small transactions touching several rows |
| S7 | **trigger scan:** read every trigger per tick, at 10², 10⁴ and 10⁵ triggers | what a scan-all trigger design costs, to inform WorkSplicer's |
| S8 | a temporal column on `tasks` updated by state changes | §4's version-store cost under orchestration's update pattern |

**Confirmed (2026-10-22):** both readings above are right, and this schema should be close to what general work orchestration needs.

**Per-type parameters, decided:** each asset type needs its own parameters, and they live in a record-valued column on `task_assets` (`gsql_design.md` §3.5), PostgreSQL's `jsonb` with gBASIC's types, with field indexes for the parameters queried by. One more workload follows:

| id | workload | why |
|---|---|---|
| S9 | assets of one type queried by one parameter, with and without a field index, at 10³ and 10⁶ assets | what the field index buys, and what a residual scan costs |

**Needed from WorkSplicer** when its redesign settles:
- rough volumes per tenant (tasks, assignments, log rows per day, triggers);
- how triggers are found;
- whether task state lives in a column or is derived from the log and triggers;
- how many tenants one machine serves (§5.5's many-owners measurement).

#### 5.1.3 estateforge: realistic data, and an answer key no engine wrote

`estateforge` (`~/development/estateforge`) generates a fabricated corporate estate from one declaration: tables, views, procedures, ETL flows and rows. It comes at three scales (25, 116 and 500 tables), in two vocabularies (energy trading with Enron-corpus names, and a US core bank), with named pathologies planted on purpose (null regions, undeclared foreign keys, SCD2 dimensions, denormalised copies, and others). Its distinctive property is that the declaration produces **the answer key as well as the database**: it computes each question's answer by folding over the generated rows, never by running SQL.

Its role in G-SQL, by phase:

- **Phase 0a (this document):** realistic row shapes, widths, key distributions and a 500-table catalog for the workloads, in place of uniform random keys. Its value distributions (trend, seasonality, planted anomalies) make compression and page-fill measurements honest.
- **The SQL layer, later, and the stronger use: a differential oracle.** Build the same estate in PostgreSQL and in G-SQL, run its questions in both, and compare each against the answer key. Agreement between two engines alone could be one shared mistake; the answer key was written by neither engine. `nlq`'s value tier in this repository already uses estateforge exactly this way, against PostgreSQL.
- **Semantics:** its `null_region` pathology and SCD2 dimensions exercise `unknown` and grouping under the SQL truth tables (`gsql_design.md` §3.1.1).

**Gap:** estateforge's demo estate is about 4,800 rows, and its row loading goes through SQL inserts (12.7 ms/row on SQL Server, from its EF-4 report). The volume workloads here need 10⁵ to 10⁷ rows. So either estateforge gains a bulk export mode that the harness loads directly, or the harness generates volume from estateforge's schemas and distributions. **Decide with estateforge's session** when the harness is designed.

#### 5.1.4 Engine-specific workloads

What the standard workloads do not reach. W1 to W3 and W5 overlap YCSB (C, A, the load phase and E respectively); they stay because they isolate one cost each, where YCSB mixes them:

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

### 5.3 Crash correctness: the gate (specified 2026-10-22)

**A kill -9 alone does not test power loss.** After a process dies the operating system's page cache still writes its data out, so a kill-only harness never sees a lost or torn write. That is why the simulated I/O layer (§3.4) exists.

**The run is deterministic.** The engine is single-threaded and the simulated layer is in memory, so a workload plus a seed produce the same sequence of I/O operations every time. A failing case is reported as **(candidate, workload, seed, operation number, crash mode)**, and replaying those five reproduces it exactly. A small workload is crashed at **every** operation; a large one at operations chosen by the seed.

**Crash modes,** applied at operation N:

| mode | the disk left behind |
|---|---|
| process crash | every write so far, durable or pending, applied in order (the page cache survived) |
| power loss | the durable image, plus a **seed-chosen subset** of pending writes applied **in a seed-chosen order** (rules 3 and 4) |
| torn write | as power loss, and one of the applied pending writes keeps only a seed-chosen prefix of its sectors (rule 3) |
| failed sync | the next `sync` fails; the engine must report `IO`, refuse further work with `FAILED`, and recover correctly when reopened (rule 5) |
| full disk | a `write_at` answers `ENOSPC`; the transaction aborts, the database stays usable, and nothing committed is lost |

**The oracle is a model, not the engine.** The workload generator records what each transaction wrote and whether its commit **returned durable** (synced, or covered by a returned `sync`). After the crash, the harness reopens the database, which runs recovery, and reads every tree. The recovered state must equal the model's state after **some prefix P** of commits, where:
- P includes every commit that returned durable;
- P includes nothing that never reached commit;
- commits made but not yet durable may or may not be in P, **as a prefix**, never with a gap.

Beyond the state itself:
- **A torn page is detected**, by its checksum, and never read as data. Recovery either repairs it from the log or copy that the design keeps, or reports `CORRUPT`; returning garbage is a failure.
- **`check` passes** after recovery.
- **Recovery is idempotent:** reopening a second time gives the same state.
- **A crash during recovery is survivable.** The harness crashes the recovery itself at every operation, then recovers again, and the result must still satisfy the oracle. This matters more than usual here, because the owner handoff (`gsql_design.md` §6.1) runs recovery on every takeover.
- **A crash during backup** leaves the source untouched and no file at the destination that passes `check`.
- **Work continues after recovery:** a further workload runs, crashes and recovers correctly.

**Gate:** zero violations across at least 10,000 injected crashes per mode per candidate, before any performance number is considered. A candidate failing the gate is fixed and rerun, or eliminated.

### 5.4 Group commit

The owner serves every client, so it can **group commits**: hold acknowledgements briefly and make one flush cover several clients' transactions. This changes the comparison, since it narrows 2.B's one-flush advantage over 2.A's two. W2 is measured with and without it, at 1, 8 and 64 concurrent clients. Because a commit becomes visible only once durable (§1.3), group commit also delays a **second** client's read-after-write by up to one interval; that latency is measured beside throughput, and the interval is chosen from both.

### 5.5 The owner round trip and the handoff (§6.1 gates)

- **Round trip:** client to owner and back over the local socket, for an empty request, a point read and a batch of 100 rows, against the same operation called in-process. The protocol is accepted if the added cost is small next to the work of a typical query; the threshold is agreed after the first numbers rather than guessed now.
- **Handoff:** owner exits (cleanly, and by kill -9) → a client takes the lock → opens → recovers → serves. Total time, per candidate and per log size.
- **Many owners on one machine:** one database per tenant (WorkSplicer) means a multi-tenant deployment runs **one owner per active tenant**. Measure an idle owner's memory and file-descriptor cost, and **cold start**: the first request to a tenant whose owner is not running, including recovery. Measure at 1, 100 and 1,000 tenant databases. This decides whether an owner should stay up while it has clients or start on demand per request burst.

## 6. How the choice is made

1. **Gate:** crash correctness (§5.3). Absolute.
2. **Hard requirements:** online backup without stopping writers; bounded recovery time; a correct long-reader story; nonce uniqueness for encryption; atomic table replacement under live readers (G1); engine-enforced read-only transactions (G4).
3. **Acceptance for the chosen design, before the SQL layer is accepted:** G-Dash's staging store can be rewritten against G-SQL with its decimal-text money boundary deleted, and its results are identical.
4. **Weighed, with weights fixed before results are seen:** latency and throughput on the **standard workloads first** (§5.1.0), then the engine-specific ones, with each application profile able to veto a design that fails it outright but not to choose among designs that serve it, space amplification, handoff time, and **implementation complexity**: lines of code, and the number of invariants a reader must hold to change it safely. Complexity counts because the engine is maintained for the life of the language.

**Predictions, written now so measurement can refute them:**
- 2.A wins recovery and handoff time (near constant), backup simplicity and the long-reader story, and is the simplest to make correct. It loses single-row commit latency without group commit, and space during W6.
- 2.B wins single-row commit latency and space efficiency; recovery time grows with the log.
- 2.C wins W4 by a wide margin and loses W1 tail latency, with the highest complexity.
- G-Dash's profile (G1, G2, G3) favours 2.A further: table replacement is a root swap and its readers keep their snapshot for free.
- **Overall prediction: 2.A, with group commit closing most of the commit-latency gap.** If 2.B or 2.C wins on the agreed workloads, that is the answer, and this paragraph stays as the record of what was expected.

## 7. Open questions

1. *(G-Dash volumes answered, §5.1.1.)* WorkSplicer's profile once its redesign settles (§5.1.2); G-Dash's refresh duration, which it has never measured.
2. *(Designed 2026-10-22, §3.2: one 11-byte number encoding for exact integers and doubles; −0 keyed as 0; NaN after +inf and equal to itself.)* To verify with the oracle.
3. *(Decided 2026-10-22, §3.2: an untyped `MONEY` column may be indexed, grouped by currency; ordering across currencies raises.)*
4. *(Settled 2026-10-21: the §2.D reading is right, and G-SQL is a new development.)*
5. The round-trip threshold for accepting the single-protocol rule (§5.5), agreed after first numbers.
6. **Cross-database queries.** **Decided (2026-10-21) for the first release: one query reads one database.** SQLite's `ATTACH` (several databases in one query) is not offered at first, because across owners it is a distributed query; it is added only if a real application needs it. Separately: whether one database is always one file plus at most a log (proposed: yes). G-Dash's case is §5.1.1a.

## 8. Reading list

- LMDB: Howard Chu's design papers, and the `mdb.c` meta-page and free-list logic.
- BoltDB: a smaller copy-on-write B+tree, easier to read than LMDB.
- SQLite: the file format, WAL mode, the VFS layer, and "How SQLite Is Tested" (its crash and I/O-error testing is the model for §5.3).
- PostgreSQL WAL and full-page writes (torn-page protection).
- RocksDB: leveled versus universal compaction, and the RocksDB tuning guide.
- Pillai et al., *All File Systems Are Not Created Equal* (OSDI 2014), on what applications wrongly assume about `fsync` and rename.
- FoundationDB's tuple-layer encoding and CockroachDB's key encoding (§3.2).
