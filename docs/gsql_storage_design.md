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
| database | `open(path, options)` | `path` names a **directory**, which the database owns (§7 question 6). Opens or creates; **runs recovery** before returning; takes the file's exclusive lock and refuses a second opener with `BUSY`. Refuses a file whose format version is newer than it understands (`FORMAT`). Options: `create`, `page_size` (at creation only), `cache_bytes`, and the I/O layer to use (§3.4), so the harness can substitute its own |
| | `close(db)` | Refused while a transaction or snapshot is open; the owner closes those first |
| snapshots | `begin_read(db)` | A consistent view as of the **last durable commit** (§1.3). Cheap; any number may be open |
| | `begin_write(db)` | At most one at a time. A second call answers `BUSY` at once, and the owner queues the request (§1.4) |
| | `commit(txn, sync)` | Returns the commit's **commit id**, a strictly increasing integer that the version store (§4) and audit history key on. With `sync = true` it is durable on return. With `sync = false` it is ordered but not yet durable (group commit, §1.3) |
| | `sync(db)` | Makes every commit so far durable, returns the highest durable commit id |
| | `txn_id(txn)` | *(Added in step 5, 2026-10-29.)* The id a write transaction is known by before it commits: greater than every commit id answered before it began, and no greater than its own. The version store (§4) keys a version by it, since a version is written inside the transaction that makes it |
| | `flush(db)` | *(Added in step 5.)* `sync`, and every commit moved out of the log, so the next open replays nothing: what an owner calls before a **planned** handoff (§5.5). `close` does not do it, since that would cost every short session a sorted file |
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

### 3.1 Page format (laid out 2026-10-22)

**Everything on disk is little-endian, by declaration.** A database file must open on any machine gBASIC runs on, and this project builds for big-endian s390x as well as x86 and RISC-V, so byte order is a property of the format, never of the machine that wrote it. A big-endian reader pays a byte swap per field; that is the whole cost.

**Page size** is measured, not chosen (4, 8 or 16 KiB, §5); it is fixed per file at creation and recorded in the header. **Page numbers are 64-bit**, so no file size limit is designed in.

#### 3.1.1 The file header (page 0)

| offset | size | field |
|---|---|---|
| 0 | 8 | magic `GSQLDB\0\x01` |
| 8 | 2 | format major (a reader refuses a major it does not know, `FORMAT`) |
| 10 | 2 | format minor (a reader accepts a newer minor, which may only add) |
| 12 | 4 | page size |
| 16 | 16 | **file identity**, random at creation: what encryption binds a page to (§6.4 of the main document), and what tells a backup of this database from another one |
| 32 | 8 | creation time (seconds) |
| 40 | 4 | flags (bit 0: encrypted) |
| 44 | 36 | the key-check block when encrypted: a known plaintext sealed with the key, so a wrong key is refused at open by name rather than discovered as checksum failures on every page |
| 80 | — | **candidate-specific root area**: two meta slots for §2.A, the log's position for §2.B, the manifest's location for §2.C |
| end − 4 | 4 | header checksum |

#### 3.1.2 The page header (every page)

| offset | size | field |
|---|---|---|
| 0 | 1 | page type (tree interior, tree leaf, overflow, free-list, version store, meta) |
| 1 | 1 | flags |
| 2 | 2 | reserved, zero |
| 4 | 4 | **checksum** of the whole page with this field zeroed (CRC32C unless the §5 measurement prefers another) |
| 8 | 8 | **page number**: a page read from the wrong place is detected, not trusted |
| 16 | 8 | **commit id** that wrote it |
| 24 | 12 | **nonce**, zero while encryption is off |
| 36 | 4 | reserved, zero |
| end − 16 | 16 | **authentication tag**, zero while encryption is off |

**With encryption on**, the page body between the header and the tag is sealed with AES-256-GCM, and the header itself (type, page number, commit id) is the **associated data**, so a page copied to another position or another file fails authentication. The checksum still covers the whole page, which keeps corruption and a wrong key apart: a bad checksum is damage, while a good checksum with a failed tag is tampering or the wrong key.

So the usable body is the page size minus 56 bytes: 4,040 bytes of a 4 KiB page.

#### 3.1.3 A tree page (§2.A and §2.B)

A **slotted page**, the layout SQLite and PostgreSQL use: a slot array growing from the front, and cells growing from the back.

- **Body header:** cell count (u16), start of the free gap (u16), end of the free gap (u16), fragmented free bytes (u16), and for an interior page the **leftmost child** (u64).
- **Slot array:** u16 offsets to cells, **in key order**, so a binary search over the slots finds a key without touching the cells.
- **Leaf cell:** key length and value length (LEB128 varints), a flags byte (bit 0: value is in overflow pages), the key, then the value, or for an overflow value its total length (u64) and first overflow page (u64).
- **Interior cell:** key length, the separator key, and the child page (u64) holding keys at or above it.
- **A value is stored inline when its cell fits in a quarter of the page body**, so every page holds at least four cells; above that it overflows. The threshold is one of §5's measurements.
- **Leaf sibling links differ by candidate, and this is a real difference.** §2.B links each leaf to the next, so a range scan walks leaves. §2.A **cannot**: under copy-on-write, a sibling pointer would force rewriting the neighbour on every change, so its scans walk back up through the parent. That cost is part of what §5's scan workloads compare.
- **Prefix compression** of keys within a page is deliberately not in the first format. The flags byte has room for it, and the §3.2 encoding (`memcmp` order, self-delimiting components) is what makes it possible later.

#### 3.1.4 Other pages

- **Overflow:** body header of next page (u64) and bytes used (u32), then the bytes.
- **Free list:** for §2.A, a tree keyed by the commit id that freed each page, which is what lets a page be reused only once no snapshot can see it. For §2.B, trunk pages listing free page numbers.
- **The catalog** is itself a tree, mapping a tree's name to its root page, its id and its kind. A table, an index and the version store are all ordinary trees, so nothing needs a page type of its own.
- **§2.C's sorted files** use their own block format (data blocks, an index block, a bloom filter and a footer), carrying the same checksum, nonce and tag fields per block.

#### 3.1.5 2.C's format, fixed (2026-10-29)

§3.1.1 to §3.1.4 were written for a page-based engine. 2.C, the choice, writes files instead, and this is the format it writes. **It is format major 1, and it is real**: a database this build writes is one every later build opens. A reader refuses a major it does not know (`FORMAT`) and accepts a newer minor, which may only add where the format leaves room: fields at the end of the manifest's body, and bits or fields in a file header's reserved bytes. Any other change is a new major. Everything is little-endian. Format major 0 was the spike's, with no promise; nothing of it needs reading.

**Encryption has room and is not built.** Every unit a key would seal carries a **seal**: a 12-byte nonce, then a 16-byte AES-256-GCM tag, both zero while encryption is off. A unit is a sorted file's data block, index block, bloom block and footer, a blob value, a log record, and the manifest's body. Turning encryption on later sets a flag, not a new major. A sealed file or record is refused with `FORMAT` by this build, which cannot read one. The checksum covers the seal, as it covers everything, so §3.1.2's split still holds: a bad checksum is damage, a good checksum with a failed tag is the wrong key or tampering.

**The directory** holds `db` (the manifest), `db.lock`, `db.log.N`, `db.N.sst` (sorted files) and `db.N.blob` (blob files). Sorted and blob files share one numbering.

**The manifest** is §3.1.1's 80-byte header (format major 1, minor 0; the "page size" is the data block target; flags bit 0 is "encrypted"), then u32 key epoch and the seal over the body, then the body, then a u32 CRC32C of everything before it. As built, the header's own checksum is at [76, 80) and covers [0, 76), which leaves 32 bytes at [44, 76) for the key-check block: a nonce and a tag sealing nothing, which is enough to refuse a wrong key by name at open. The body, every integer a u64 and every byte string a LEB128 length then its bytes: the next file number, the first log to replay, the last sequence number; then for each of 7 levels a file count and, per file, its number, size, entry count, smallest key, smallest key's sequence, largest key, largest key's sequence, a sequence override (nonzero for a spilled transaction's file, where every entry takes it), its own blob file's number (0: none), and a count of (blob file number, bytes it points at) pairs; then a count of (retired tree id, retiring sequence) pairs.

**A file header** opens every sorted and blob file, 64 bytes: the manifest's magic (8), major (u16), minor (u16), kind (u8: 1 sorted, 2 blob), flags (u8, bit 0 encrypted), 2 reserved, the **database identity** (16, the manifest's), the **file number** (u64), the key epoch (u32, 0 while off), 16 reserved, and a CRC32C of the 60 bytes before it. The identity and number are what make a file restored from another database, or under another number, a refusal (`CORRUPT`) rather than a plausible answer from the wrong data.

**A sorted file** is its header, its data blocks, an index block, a bloom block and a 64-byte footer. Every block ends with a **trailer**: a u32, the seal, and a CRC32C of everything before the checksum. The u32 is the entry count for a data block, the file's entry count for the index, and the number of bits for the bloom.
- A **data block** holds entries in internal order (key ascending, sequence descending): LEB128 key length, the key (the tree id as 8 bytes big-endian, then the user's key), u64 sequence, u8 type (0 delete, 1 value, 2 blob pointer), LEB128 value length, the value. A block is cut once it reaches the target size.
- The **index block** holds, per data block, LEB128 length and the block's last key, its last sequence (u64), offset (u64) and size (u32).
- The **bloom block** is the filter's bytes (bits / 8 + 1 of them), 10 bits a distinct key, 7 probes.
- The **footer**: index offset (u64), index size (u32), bloom offset (u64), bloom size (u32), entry count (u64), the seal, and a CRC32C of the 60 bytes before it.

**A blob file** is its header, then values, each stored as the seal followed by the value. A blob pointer, the 24-byte value of a type-2 entry, is the blob file's number (u64), the offset of the stored value (u64, where its seal begins), the value's length (u32) and a CRC32C over the seal and the value (u32).

**A log** has no header, since a log is recycled by renaming. Each record is a u32 length and a u32 CRC32C of the bytes after them, then u8 flags (bit 0 sealed), 3 reserved, u32 key epoch, the seal, then the payload: base sequence (u64), entry count (u32), the **chain** (u32: the checksum of the record before it in this file, or for the first a seed from the file's number, `num × 0x9E3779B1 XOR 0x5BD1E995` in 32 bits), a value drawn at each open (u32), then per entry u8 type, LEB128 key length and key, LEB128 value length and value, then a u32 count of retired tree ids and the ids (u64). The length counts everything after the first 8 bytes. Replay stops at the first record that does not chain, which covers a recycled log's old records and what a lost commit left behind. It does **not** stop at a record that chains and is sealed or flagged by a newer format: that is `FORMAT`, because stopping there would drop durable commits in silence.

**Held to it by** `tests/run_gsql_store.sh`'s format tier: a committed format-1 database (`tests/gsql/format1/`, every file kind in it) that every build must open and read exactly; an independent reader (`tests/gsql/format_check.py`) that decodes it, and a database this build writes, from this section rather than from the engine; and the refusals above, each beside the case that must still open.

### 3.2 Key encoding: byte order must equal gBASIC order (designed 2026-10-22, built 2026-10-23)

**Status: built** as `src/gsql/key.c` (`include/gsql_key.h`), held to gBASIC by `tests/run_gsql_key.sh`. Its input is one value in `serialize` version 3 (§3.3), so it needs no interpreter. Building it corrected the design in three places, each marked *as built* below.

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
- **Money, untyped column (`MONEY`): decided, it may be indexed.** Tag `0x41`, the currency code, then the amount as above. *As built:* the code is the alphabetic code followed by `0x00` (registered codes run to 7 letters, so it cannot be a fixed 3 bytes), or, for a value whose writer did not know its code, `0x01` and the numeric code as 2 big-endian bytes; no letter is `0x00` or `0x01`, so both forms are self-delimiting. Keys group by currency; equality lookups and ranges **within one currency** use the index. A sort or range that would order **different currencies** against each other raises at execution, exactly as gBASIC does, so the index's grouping by code is a storage detail and never an answer.
- **Dates, times and datetimes:** ~~one tag each, and a column holds one temporal kind because a date against a datetime is not a total order~~. **Corrected as built: it is a total order**, so one tag (`0x52`) covers all three. gBASIC compares time-only first, then year, month and day (skipped for a time), then hour, minute and second, then the **precision**, and equality includes the precision. So a date sorts just before the datetime at its midnight and is never equal to it, which is consistent. The key is the time-only byte, the date fields (unless time-only) and time fields as sign-flipped big-endian int32, then the precision byte. Whether a column may *mix* temporal kinds is now a schema question, not a limit of the index.
- **Durations have no key, as built.** gBASIC orders an exact duration by its total seconds but makes two durations equal only field by field (`1 day` and `24 hours` tie in a sort and are not `=`), so no single byte order agrees with both; one carrying months does not order at all. The encoder refuses a duration, as it does an array, a record, a path, a function and a regex.

**The oracle is gBASIC itself, and nothing above is accepted on reasoning alone.** Phase 0a's first artefact is the encoder with a test that generates value pairs, encodes both, and requires `memcmp` order to equal gBASIC's comparison for every pair. It covers: the exactness edge (2^53 ± 1, ±2^63), subnormals, ±0, ±inf, every kind against itself, absence in both directions, money with guard digits, and composite keys mixing directions. **NaN is the one exception, and it is a stated one:** until the language gets the total order, gBASIC has no order for NaN to agree with, so the test asserts the documented position rather than comparing. *As built*, that position is after +inf, equal to itself, and below absence.

**What the built test does** (`tests/run_gsql_key.sh`): gBASIC serializes a battery of 160 values and writes its own answer for each of about 14,800 ordered pairs, deciding absence placement with its own `sort`. Pairs of different `type()` are not asked, and two currencies are written as refused. The C checker requires `memcmp` to agree ascending, descending and as typed money, over 200,000 composite `(ASC, DESC)` keys, and for every truncation of every value to be refused. **A pinned-key tier was added because ordering alone was not enough**: shifting every subnormal's exponent by one keeps every comparison and changes the bytes, and an index written by one build must be readable by the next. Ten perturbations are proven red.

### 3.3 Row encoding: one portable value encoding (designed 2026-10-22, value encoding shipped 2026-10-23 as `serialize` version 3)

**Status:** the value encoding below is built and is `serialize` version 3 (`src/eval.c`, `tests/run_serialize.sh`). The **row** framing and the record-column measurements are not built yet. Building it changed two rows of the table, marked *as built*.

**Rows cannot use `serialize` as it stands, which was measured rather than assumed.** `serialize` writes its integers by copying them in the machine's own byte order (`serbuf_u64` is a `memcpy`), which its own comment justifies: "round-trips are always same-binary". That holds for actors and is false for a database file, which is copied between machines and outlives the binary that wrote it.

**So G-SQL defines a portable value encoding, and it should become `serialize`'s next version.** Little-endian fixed fields and LEB128 lengths, versioned, with the same tags `serialize` already has (number, string, boolean, money, the temporal kinds, array, record, absence). Three consumers would then share one codec:
- **rows on disk** (here);
- **the client protocol** (`gsql_design.md` §6.5), which carries values between processes that may be different gBASIC versions;
- **`serialize` version 3**, which also completes `exact_number_design.md`'s increment 4 (exact integers through the binary format) as part of the same change, since the number encoding records exactness. The reader keeps accepting versions 1 and 2, because those payloads are in files already.

That turns the serializer prerequisite into one piece of work with three uses, rather than three encodings that could drift apart.

**The encoding of a value:**

| kind | bytes after the tag |
|---|---|
| absence | none |
| boolean | 1 byte |
| number | 1 byte: exact (1) or inexact (0); then an int64 (exact) or an IEEE double's bits (inexact), both LE. Any other flag byte is refused |
| string | LEB128 length, then the bytes |
| money | *as built:* the exact int64 (LE), the storage exponent (1 byte), the numeric code (u16 LE), then the alphabetic code (1-byte length, then the letters). A registered currency's numeric code depends on the order a program registered things in, so the reader looks the alphabetic code up first and uses the number only when the code is unknown there. Codes run to 7 letters, so 3 fixed bytes was too few |
| date, time, datetime | *as built:* year, month, day, hour, minute, second as int32 LE, then a time-only byte and a precision byte (1 = year … 6 = second). The reader refuses a precision outside that range |
| duration | years, months, weeks, days, hours, minutes, seconds as int32 LE |
| array | LEB128 count, then each value |
| file, dir, function, regex | as in v2, with LEB128 lengths (paths, a function's name and library, a regex's pattern and flags) |
| record | LEB128 field count, then per field its name (LEB128 length and bytes) and value |

**A row** is a format byte, a LEB128 column count, then each column's value in declared order. Columns added later by `ALTER TABLE ADD COLUMN` are simply absent from older rows and read as their default, so adding a column never rewrites a table.

**Record-valued columns** (`gsql_design.md` §3.5) use the record encoding, recursively. Two things are measured here rather than assumed:
- **field-name overhead**, since every row repeats its field names, against an alternative storing names once per column;
- **field extraction cost**, since a field index and a residual filter both read one field without decoding the whole record. That argues for a per-record offset table, measured against the plain form.

A **field index** keys on the field's value with the §3.2 encoding, and an absent field is keyed as an absence.

### 3.4 The I/O layer, and why it is not optional (specified 2026-10-22)

**Status: built** (2026-10-24) as `include/gsql_io.h`, `src/gsql/io.c` (the dispatch and the real layer) and `src/gsql/io_sim.c` (the simulated one), held to the rules below by `tests/run_gsql_io.sh`. Choices made in building it are marked *as built*.

Every candidate does all file I/O through one small layer, in the role SQLite's VFS plays. It has **two implementations behind one interface**: the real one, which the performance measurements use, and a simulated one, which the crash harness uses (§5.3).

**Operations:** `open(path, create | exclusive | read_only)`, `read_at(file, offset, n)`, `write_at(file, offset, bytes)`, `sync(file)`, `size(file)`, `truncate(file, n)`, `close(file)`, and on paths `rename(from, to)`, `remove(path)`, `sync_dir(dir)` and `lock(file, nonblocking)`, and on directories `mkdir(path)` and `list(dir)` (*added 2026-10-26* for §7 question 6: a made directory is a name in its parent, durable only after the parent's `sync_dir`, and losing it loses everything beneath it). A read past the end answers short rather than failing.

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

*As built:*
- **Every rule is tested as a difference.** "An unsynced write may be lost" requires it lost by some power-loss image and kept by another, because a layer that always did one would pass a one-sided check. Rule 3 is checked against a write that spans four sectors at absolute offsets: every kept prefix (0 to 3 sectors) is produced, and no tear falls at an offset counted from the write's own start.
- **Rule 5 is modelled as Linux behaves:** after a failed `sync`, each of that file's pending writes either reached the disk or was lost, decided by a seed; either way it leaves the pending list. So a retry `sync` that succeeds does not make a lost write durable, and the process still reads the new data. That is the fsyncgate trap, and the tests assert all three parts.
- **Two places are deliberately harsher than a real filesystem.** Pending namespace operations survive as any subsequence (a journalling filesystem loses a suffix). Pending data writes survive in any subset and order. An engine correct here is correct there; the converse is what this layer exists to prevent.
- **A cross-directory rename is refused (`EXDEV`) by the simulated layer** rather than modelled loosely. Its durability needs both directories synced, and no candidate needs it.
- **Parity with the real layer is a tier of its own.** One seeded sequence of 3,000 operations over four files runs through both layers: opens of every kind, writes and reads at random offsets, truncation, sync, rename, remove, `sync_dir` and locking. Every answer, every byte read and every file's final contents must agree. A census requires each run to meet at least 50 errors and to read back at least 100 KB, so it cannot agree about nothing.
- **Windows is not implemented.** Its `open` answers `ENOSYS` rather than shipping a layer nobody here has run.

## 4. The version store (temporal history, §10D)

The same in every candidate, and taken from AmorphDB's instance chain:

- The **current** value of a temporal column lives in the row, so ordinary reads never touch history.
- An update appends the **old** value to a version tree keyed by (table, row key, column, commit id). The row keeps no pointer; the newest older version is found by a seek to the largest commit id below now. This avoids rewriting the row twice and keeps backward-linked semantics.
- Each version carries the **commit id** (not a timestamp: the clock is second-resolution and commits are ordered), the commit's **author** (the `principal()` that made it, as AmorphDB records the author of every instance) and the commit time.
- `AS OF t` maps t to a commit id through a small commit-time index, then seeks.
- Measured with the candidates: the cost of history on update throughput, and the `AS OF` read cost against depth of history.
- **Built and measured 2026-10-28** (`research/gsql/README.md`, "The version store, and W9"), once over the bench's interface, so all six engines pay for the same layout. A model check covering `AS OF` by commit and by second, rows that never existed, and the counter recovered across a reopen is green on all six and runs in the research gate. Two things were settled in building it:
  - The commit-time index holds **one entry per second**, the last commit made in it, because the clock can say no more. Ids and seconds both only grow, so a floor seek maps a second to a commit.
  - The version is keyed by the id its value **started** at, so `AS OF A` is a single floor seek.
  - Measured:
    - W9 costs the B+trees 3–17% of W2's throughput, and the LSMs about nothing.
    - An `AS OF` read takes 1–16 µs at 1,000 versions per row, all six engines.
    - In 2.A, unsynced commits hold every page they free until a sync. A build of large unsynced commits grew its file 5×.
- **Moved into the tree in step 5 (2026-10-29)**: `src/gsql/version.c`, over the storage interface itself and keyed by the engine's `txn_id` rather than a counter of its own, with removals kept as versions and the commit second clamped never to go back (§6, "Step 5").

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

**Measured 2026-10-28, in process** (`research/gsql/README.md`, "The G-Dash profile, the handoff and space under group commit"; G1 and G4 in Test 3, §6):
- **G2:** a 10⁶-row `sum … group by` takes 120 to 160 ms in every candidate, the same with a refresh half written alongside. A filter changes nothing without an index.
- **G3:** a request (snapshot, one row, release) costs under 1 µs, and holding 1,000 datasets instead of 20 adds about 0.2 µs. The owner's round trip, not yet built, will be the cost.
- **G5:** 2.A and 2.C stay near 80 MB at 10⁷ rows in one transaction. 2.B grows with the transaction (146 MB at 10⁷), about 400 bytes per spilled page in its frame index: engineering, not structure.
- **G6:** a query naming 2 datasets of 20 costs a tenth of one naming all 20.

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

**Status: the harness is built** (2026-10-24) as `include/gsql_crash.h` and `src/gsql/crash.c`, behind an engine interface that is the slice of §1 it needs: commits of puts and deletes, `sync`, and a full read. A candidate adapts its own interface to that slice. *As built:*
- **The harness has to prove that it bites.** One reporting zero violations cannot be told apart from one that checks nothing. So `tests/run_gsql_io.sh` runs it over a toy engine (an append-only log of checksummed commit records) and over eight broken copies of the toy. Each copy omits one thing correctness rests on, and each must be caught by the check written for it, not merely caught:

  | broken copy | mode that catches it | check that fires |
  |---|---|---|
  | does not flush on commit | power loss | the recovered state is missing a commit that returned durable |
  | never syncs the directory after creating the log | power loss | recovery found an empty database, though commits returned durable |
  | trusts a record without its checksum | torn write | the recovered state matches no prefix of the commits |
  | leaves a damaged tail and appends after it | power loss | a commit made after recovery is lost by the next crash |
  | reports a failed flush as success | failed sync | the failure is reported as success |
  | reports a failed flush, then keeps accepting work | failed sync | later work is not refused with `FAILED` |
  | moves the end of the log past a write the full disk refused | full disk | a commit is lost |
  | recovery empties the log and syncs before writing it back | power loss, crashing the recovery itself | a recovery-stage violation; with that stage turned off it passes, which shows that stage is what caught it |
- **The correct toy clears the gate** with zero violations: about 19,000 to 21,000 crashes per mode for process, power and torn (120 workloads), and over 10,000 for failed sync (1,000 workloads) and full disk (700 workloads). A crash of the recovery itself counts as a crash. Each run reports a census: commits that had not returned durable must be both **kept** by some crash and **dropped** by another, so the oracle's whole range is exercised.
- **Workloads vary how often they sync.** Odd seeds sync 20% of commits and even seeds 80%. A workload that syncs most commits leaves few writes pending at a crash, and rules 3 and 4 can only act on pending writes. Measured: at two-thirds synced, the no-checksum copy was caught in only 3 of 20 workloads.
- **The toy's own parser is a partial integrity check, and that blinds a checksum test.** A torn append usually leaves zeros where its tail should be, which fails parsing whether or not the checksum is checked. So the no-checksum copy is caught only when a tear falls inside the last value of a record. Several disks per crash point (`images_per_point`) and rarely-syncing workloads are what make that case common enough to see.

### 5.4 Group commit

The owner serves every client, so it can **group commits**: hold acknowledgements briefly and make one flush cover several clients' transactions. This changes the comparison, since it narrows 2.B's one-flush advantage over 2.A's two. W2 is measured with and without it, at 1, 8 and 64 concurrent clients. Because a commit becomes visible only once durable (§1.3), group commit also delays a **second** client's read-after-write by up to one interval; that latency is measured beside throughput, and the interval is chosen from both.

### 5.5 The owner round trip and the handoff (§6.1 gates)

- **Round trip** (the protocol is `gsql_design.md` §6.5): client to owner and back over the local socket, for an empty request, a point read and a batch of 100 rows, against the same operation called in-process. The protocol is accepted if the added cost is small next to the work of a typical query; the threshold is agreed after the first numbers rather than guessed now.
- **Handoff:** owner exits (cleanly, and by kill -9) → a client takes the lock → opens → recovers → serves. Total time, per candidate and per log size.
- **Many owners on one machine:** one database per tenant (WorkSplicer) means a multi-tenant deployment runs **one owner per active tenant**. Measure an idle owner's memory and file-descriptor cost, and **cold start**: the first request to a tenant whose owner is not running, including recovery. Measure at 1, 100 and 1,000 tenant databases. This decides whether an owner should stay up while it has clients or start on demand per request burst.

- **Measured 2026-10-28** (same README section): the **handoff**, from the owner's death to the new owner's first read, is about 0.1 ms in 2.A at any log size, 1 to 2 ms (clean) and 9 to 15 ms (SIGKILL) in 2.B, and 3 to 107 ms in 2.C, growing with the memtable it replays. 2.C's close does not flush its memtable, so a clean handoff replays too. The **round trip** and **many owners** wait for the owner (step 5).
- **Measured 2026-10-29** (`research/gsql/README.md`, "Step 5"), over the chosen engine:
  - **Round trip:** the owner adds about 5.6 µs to an empty request, 8 µs to a point read (3.7 µs in process becomes 11.7) and 11 µs to a 100-row batch (17 becomes 28), every reply checked. **Accepted 2026-10-29** (§7 question 5): one protocol, always, with a point read to stay within about 20 µs added, re-measured on the real owner.
  - **Many owners:** an idle owner is about 2.3 MB resident and 8 descriptors, flat from 1 to 1,000 owners, and cold-starts in under 1 ms when it has nothing to replay. **An empty database was 134 MB on disk at the default memtable**, two logs zero-filled up front to twice its size, so 1,000 tenants would have been 134 GB of zeros. The zeros now grow a step ahead of the log, at no measured cost to a synced commit, and an empty database is 1.07 MB.
  - **Planned handoff:** with `flush` (§1.1) the next owner opens in 0.1–0.3 ms. The flush itself is 9–12 ms, so a planned handoff wins once the memtable holds a few thousand commits: 10 against 15 ms at 10⁴, 31 against 125 at 10⁵, and loses to a plain close's 3.4 ms after 100.

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

**First measurements, 2026-10-24** (`research/gsql/README.md`, "Measurements", has the tables and the raw lines). These cover the standard workloads (YCSB, TPC-C-like, SSB flight 1) and W1, W2 and W5 to W8, at 4 and 8 KiB pages, against SQLite, LMDB and RocksDB, on this machine only.
- **The overall prediction is refuted on the standard workloads.**
  - 2.C leads YCSB A, B, D, E and F, W2, the TPC-C-like mix and group commit.
  - 2.A leads none of them among the candidates.
  - Group commit does not close 2.A's gap: at group-64 it is 8× behind 2.B and 19× behind 2.C, because its cost is the path copy (32× write amplification), not the flush.
- **2.B's predicted wins in commit latency and space did not appear.** 2.C is narrowly faster to commit and far smaller.
- **2.C's predicted weakness in W1 tail latency did not appear either.** Its real weakness is W8: it is the slowest engine at 100 KiB values, for want of value separation.
- **Lines of code are close across all three**, about 2,050 to 2,140.
- **Running these found two defects that grew the candidates' files without bound**, one in 2.A's free tree and one in 2.B's checkpoint under group commit. Both are fixed and gated, and both are recorded in that README.
- **No final decision is taken here.** W3, W4 and W9 (W9 now measured, above), the larger-than-memory size, a slower device, the hard requirements of point 2 and the G-Dash profile are not yet measured. *(All since measured: Tests 1 to 4 and the 2026-10-28 paragraphs below.)*

**Weights, fixed 2026-10-24, after the first measurements.** Point 4 asked for weights before results were seen, and that did not happen. They are fixed now, and the record says so, so that a reader can judge how much the weighting could have been bent towards the result already in hand.

| Criterion | Weight | Measured by |
|---|---|---|
| Standard workloads (YCSB A–F, TPC-C-like, SSB flight 1) | 35 | throughput, with p99 where reported |
| Engine-specific workloads (W1–W9) | 20 | throughput, with p99 where reported |
| Implementation complexity | 20 | lines of code, and invariants a reader must hold |
| Space amplification | 15 | file size against live data, during and after W7 |
| Recovery and handoff time | 10 | reopen time, and against log size where one exists |

- **Scoring.** Within a criterion, each engine scores the geometric mean, over that criterion's workloads, of its result divided by the best candidate's (inverted where lower is better). A workload an engine cannot run scores zero for that workload.
- **Vetoes stay outside the weights.** The crash gate (point 1), the hard requirements (point 2) and an application profile failed outright each remove a candidate whatever its score.
- **Sensitivity, checked against the first measurements.** No weighting within reason changes the order of 2.C and 2.A. 2.B overtakes 2.C only if recovery and handoff time carries most of the weight, since 2.B reopens in 2.58 ms against 8.42 ms and the two are otherwise close on complexity. Neither figure matters to a user at that size.

**Provisional choice, 2026-10-24: 2.C, with 2.B as the fallback; 2.A is retired.**
- **2.C** leads the standard workloads and space, and its code is no larger than the others'.
- **2.B** is kept because it is the lower-risk design, and its two worst numbers (space at 4 KiB, half-full pages) are largely fixed by 8 KiB pages and right-edge splits. If 2.C fails a confirming test, the answer is 2.B, not 2.A.
- **2.A** is retired because its loss is structural. Every commit copies its path (32× write amplification), which group commit cannot remove. Its one distinctive strength, cheap snapshots, 2.C also provides by pinning versions.
- **What would overturn 2.C**, in the order it is to be tested:
  1. W3, W4, a larger-than-memory size and one slower device, with 2.B run alongside.
  2. Value separation (a WiscKey-style value log) prototyped in 2.C and W8 measured again.
  3. The hard requirements of point 2, checked against 2.C with 2.B alongside: online backup (expected to be a hard-link snapshot of immutable files), bounded recovery on a long log, held-snapshot space, G1 and G4.
  4. Open question 6, whether a database may be more than one file plus a log, since 2.C needs a manifest, logs and sorted files.
- **Common to every outcome:** the scan path is about 10× slower than LMDB's in all three candidates, and is fixed whichever engine is chosen.

**Test 1 run, 2026-10-25: it does not overturn 2.C** (`research/gsql/README.md`, "Confirming tests", has the tables). It covered W3, W4 at 10 M rows, 5 M rows under a 1 GiB memory cap, a SATA SSD over USB and a USB flash stick.
- **The slower devices widen 2.C's lead.** On the flash stick 2.C makes 410 durable commits a second, against 26 for 2.B and 1 for 2.A. 2.A and LMDB both collapse there, because every copy-on-write commit writes scattered pages and flushes twice. That settles 2.A's retirement.
- **Larger than memory, 2.C leads or ties everything but W3**, and its 100-row scans are 9× faster than either B+tree's.
- **It added two items to 2.C's list of fixes**, both engineering rather than structure:
  - **The bulk load.** At its default sizes 2.C loads 10 M random rows at 14 k/s, against 36 k/s for 2.A and 32 k/s for RocksDB, the opposite of the §6 prediction. A 64 MB memtable recovers 2.5× of it. A trivial move, which 2.C lacks, would recover the rest of the sorted case.
  - **Commit stalls.** W3's p99 is 1.85 s against RocksDB's 35 ms, because compaction runs inside the commit that triggers it. Moving compaction off the commit path is the fix.
- **Tests 2 to 4 remain.** Before the choice is final, 2.C is to be rebuilt with value separation, larger levels, a trivial move and compaction off the commit path, and measured again.

**Test 2 run, 2026-10-26: 2.C rebuilt and measured again; it stands** (`research/gsql/README.md`, "The 2.C rebuild", has the tables).
- **All four items are built and pass the crash gate**: value separation into blob files, a 32 MB default memtable, a trivial move, and compaction in slices across commits.
- **W3 and W8 change most.**
  - W3 runs at 40–52 k rows/s at 10⁶ records with a 100–120 ms p99, against 21 k and 724 ms before, and against RocksDB's 93 to 99 k. *(Corrected 2026-10-29: this line first said 242 k and 4.3 ms, which was a 10⁵-record run compared with 10⁶-record ones.)*
  - W8 runs at 2,081 ops/s, the best of the six engines, where it was the slowest.
- **The bulk loads and group-64 improve too.** The YCSB load and group-64 now beat RocksDB. W4 random doubles but still trails 2.A.
- **Building it found a durable-commit loss in 2.C, present from the start.** A manifest written before the first flush after an open recorded the wrong log to replay from. Three file leaks also turned up, found by checking every recovered store. All are fixed and gated.
- **What it costs, and what is left:**
  - **Recovery** is 53 ms on NVMe and 1.3 s on the flash stick, against 8 and 81 ms before, because the memtable is larger.
  - **Blob space** is not reclaimed fast enough: W8's space amplification is 2.91. **Done 2026-10-27**: a compaction relocates out of any blob file at least half dead, and when nothing else is wanted one is begun for the deadest; W8 is 1.96, at write amp 4.28 against 4.09 (`research/gsql/README.md`, "Blob space by dead fraction").
  - **A flush still runs inside a commit**, which is most of W3's remaining p99 larger than memory. **Done 2026-10-27, and the share was wrong:** a flush was about half of a p99 commit, and compaction slices the rest. The memtable is now frozen and flushed in slices, its next log recycled from the frozen one. W3's fast-mode p99 is 86–87 ms against 121, and W8's p999 write 14–15 ms against 55–57; the p50 rises from 4.4 to 5.6 ms. It also found a manifest recording deaths of commits not yet durable, which could drop a live tree after a crash, present before it (`research/gsql/README.md`, "The flush off the commit path").
  - **Recovery time against memtable size. Done 2026-10-28.** An open zeroed the rest of the log, 0.4 ms per MB of log file, whatever it replayed. The log's records are chained by checksum now, with a value drawn at every open, so a record a lost commit left behind can never follow a new one, and nothing is zeroed. A short log opens in about 3 ms at any memtable size (it was 31 ms at 32 MB and 120 at 128 MB). A full one replays at 150–180 MB/s: 79 ms at 32 MB. Replay is what is left, and it is the price of a larger memtable.
  - **Compaction pacing that answers to debt. Measured 2026-10-28 and left at a constant.** A credit rising with the pressure cuts W3's p99 from 82–104 ms to 67–76, and raises its p50 from about 6 ms to 26, at the same mean. Compaction inside commits sets the mean commit, and pacing only shares it out. W3's tail needs less compaction work, or compaction on a thread of its own (`research/gsql/README.md`, "Recovery, and compaction paced by debt").
  - **Point reads are 10 to 20% lower.**
  - Each is engineering rather than structure.
- **Tests 3 and 4 remained:** the hard requirements, and open question 6.

**Test 3 run, 2026-10-26: the hard requirements; 2.C stands, with backup the one requirement unmet as built** (`research/gsql/README.md`, "Test 3", has the tables; `research/gsql/hardreq.c` checks every claim it reports).
- **Met by 2.C:**
  - **G4.** A read-only transaction refuses every write, and nothing changes. This is true of all three candidates.
  - **G1.** The old reader keeps the old dataset, new readers see only the new one, and the swap is O(1) (0.004 ms at 10⁶ rows, against 102 ms for 2.B, whose B+tree frees the old tree's pages).
  - **Bounded recovery.** About 100 ms after 10⁶ commits, held reader or not, bounded by the memtable.
  - **Held-snapshot space.** A held reader costs the data at its snapshot and nothing more.
- **Not met as built, by either candidate: online backup without stopping writers.** Both backups are consistent, but each is one blocking call in a single-threaded owner, and a 10⁶-row backup holds writers for 326 ms in 2.C and 157 ms in 2.B. Meeting it needs a backup taken in slices between commits, an interface change. **The designs pay for that differently, which is the structural result:**
  - a sliced 2.C backup pins its snapshot's immutable files;
  - a sliced 2.B backup is a long reader, and a long reader stops 2.B's log reset, so its log and its recovery time grow with every write made meanwhile (827 MB and 656 ms after 10⁵ commits).
- **It found four defects, all fixed and gated:**
  - **2.C's version retention used the oldest open snapshot as its horizon.** Readers pin their versions, so that kept versions nobody could read: one held reader cost space in proportion to writes, 301 MB left after a release that needed 1.4 MB. The same rule, copied into a new sweep of dropped trees, also deleted a tree whose drop was not yet durable, and a new reader saw 78 of 300 of its records. The horizon is now the last durable commit.
  - **2.B's log never reset** under group commit written as synced commits: 8.3 GB and 6.1 s to recover after 10⁶ commits. Now 4.2 MB and 3 ms.
  - **A replaced dataset in 2.C kept its space** until a compaction reached it. Its files are now dropped whole.
  - **2.C's flushes wrote every version**, not only each key's newest.
- **Two blind spots in the suites are closed.** No workload had put two versions of one key in a memtable at a flush, and the crash gate had never crashed a dropped or replaced tree.
- **Test 4 remains:** open question 6, together with the sliced backup, which changes the §1 interface.

**Test 4 run, 2026-10-26: online backup met by a sliced backup; 2.C stands; open question 6 answered with a recommendation** (`research/gsql/README.md`, "Test 4").
- **§1.1's `backup` gains a sliced form**: `backup_begin(snapshot, dest)`, `backup_step(job, budget, &done)`, `backup_end(job)`. Writers commit between slices, and the one-call `backup` is the same path run to completion. Ending a job that is not done discards it, and nothing at `dest` looks complete until it is.
- **2.C's backup is physical now**: the snapshot's pinned, immutable files copied as they are, then the memtable's view, then the manifest.
- **Online backup is met by both remaining candidates.** At 10⁷ rows with 1 MB slices, the longest slice is 6.2 ms (2.C) and 3.7 ms (2.B), and the longest commit made during the backup is 0.4 and 1.4 ms. They pay for it differently:
  - **2.C's open snapshot costs nothing more** than the files it already pinned.
  - **2.B's is a reader**, whose log grows about 8 KB with every commit made while the backup runs, for as long as the backup takes.
- **It found a latent defect in 2.C's old backup.** The copy took every retired tree, including trees retired after the snapshot, which the copy's own compactions would have deleted while its catalog still listed them. Fixed and gated.

**The G-Dash profile, the handoff and space under group commit, 2026-10-28: 2.C stands** (`research/gsql/README.md`, the section of that name).
- **G-Dash vetoes no candidate.** Its queries are scans of up to 10⁶ rows at 120 to 160 ms in all three, a request costs under a microsecond in process, and the number of datasets barely matters. The scan path, already the common fix, is where G-Dash's page time goes.
- **2.A's predicted handoff win holds** (0.1 ms, constant), and 2.C's is the slowest at up to about 100 ms with a full memtable. The 2026-10-24 sensitivity check found recovery time reorders the candidates only if it carries most of the weight; it carries 10 of 100, and step 4 re-scores with these figures.
- **Space under group commit is a new cost of 2.A, and none of 2.C's.** With 1,000-row transactions synced every 64 commits, 2.A's file reaches 21.9× its data and keeps it. 2.B's log grows 9× and is truncated back. 2.C is unaffected by the group size.
- **2.B's group commit on the flash stick is still unexplained.** The log-reset fix is ruled out on NVMe; the checkpoint's write order is next, and needs the stick.
- **Left for step 3:** nothing that can be measured without the owner, except the stick. Step 4, re-scoring under the fixed weights, can begin.

**Step 4, 2026-10-29: scored under the fixed weights, the choice is 2.C, with 2.B as the fallback** (`research/gsql/README.md`, "Step 4"; `research/gsql/score.py`, committed before the round's numbers existed).
- **One fresh round on one day's code, the deciding parts three times.** Mean points out of 100, with the B+trees at 8 KiB pages, which is what they would ship with: **2.C 73.8, 2.A 69.1, 2.B 66.6.** At 4 KiB: 2.C 79.2, 2.B 57.4, 2.A 57.3.
- **2.C's lead is performance**: 49 of the 55 workload points, against 25 and 32. It gives back 6.3 points of complexity (3,095 lines against 2,118 and 2,210) and, at 8 KiB, about 3 of space on a 10⁵-record store, where its two 64 MB logs are half the file.
- **Robust to the two rule details that decide the margin.** Measuring space at 10⁷ rows, or flooring recovery at 20 ms, 2.C leads in 17 of 18 scorings and ties the last, within single-run noise. Both variants were chosen after seeing the numbers and are reported as sensitivity, not as the rule.
- **2.A stays retired although it scores second at 8 KiB.** The weights hold no slow device: on the flash stick it makes 1 durable commit a second against 2.C's 410. Its group-commit gap is structural, and under large unsynced commits its file grows 21.9× and keeps the space. A veto for a device class is not in point 2's list; this paragraph records that the retirement rests on those measurements, not on the score.
- **Corrected on the way:** 2.C's W3 after the rebuild is 40–52 k rows/s at 10⁶ records, not the 242 k recorded (a 10⁵-record run). RocksDB still leads W3 by 2×.
- **What 2.C still owes, carried into step 5:** the scan path (common to every candidate, and where G-Dash's page time goes); W3's tail, which wants compaction on a thread of its own; point reads in a short read run after a load (about 32% below the build before the flush change); a flush at close, so a planned handoff does not replay; and the invariant count, never made for any candidate.
- **Not measured, and needed by step 5 rather than by the choice:** the owner round trip and many owners per machine (§5.5), and 2.B's flash-stick group commit, which matters only if the fallback is taken.

**Step 5, 2026-10-29: 2.C moved into the tree, under the gate** (`tests/run_gsql_store.sh`).
- **What moved.** The engine is `src/gsql/lsm.c`, the interface `include/gsql_store.h`, the shared field helpers `src/gsql/store_common.c` and `store_util.c`, and the functional test and crash adapter `tests/gsql/store_test.c` and `crash_store.c`. 2.A and 2.B stay in `research/gsql`, whose `run.sh` builds 2.C from its new place beside them, so the fallback is held to the same tests as the choice.
- **The gate suite** runs the build under `-Wpedantic -Werror`, the interface against its model under ASan and UBSan, the version store against a model, and the §5.3 crash gate at a reduced seed range with every mode past 10,000 crashes. About 2.5 minutes; `GSQL_GATE_SCALE=full` runs the research ranges (about 25 minutes for 2.C).
- **The version store takes the engine's commit id** (`src/gsql/version.c`, `include/gsql_version.h`). The bench's version store kept a counter of its own; the engine now names a write transaction before it commits (`txn_id`, §1.1). Built over the storage interface rather than the bench's, on two trees whose names begin with a NUL byte. Two rules the bench's version did not need:
  - **A removal is a version.** A row deleted at D is kept with a removed flag at D, or `AS OF` any id after the delete would answer the value before it.
  - **A second is clamped never to go back**, so a clock stepped backwards cannot break the floor seek that maps a second to an id. The stamp records the second actually indexed.
- **`flush` for a planned handoff** (§1.1), checked through a new `replayed` statistic: a plain close replays at least 250 records of 250 commits, a flushed one none, and both hold every commit. **As first built it cost about 34 ms however little it moved**, because a freeze needs a ready spare log and making one is 64 MB of zeros, synced. It now freezes into an empty log and then, as an open would, writes into the full-size log the install recycled. That second step was found by measuring: a synced commit into a log that is not prefilled costs about twice one that is (515 against 252 µs at the median), and an owner writing on after its own flush paid that until its next freeze. So the prefill is needed; what changed is that it grows a step at a time, which made an empty database 1.07 MB rather than 134 (§5.5). The crash gate never reaches this path, since its spare is always ready by the time it flushes, so `store_test` builds power losses around it directly. §5.5 has the costs.
- **The owner round trip and many owners are measured** (§5.5), with a prototype owner in `research/gsql/roundtrip.c`. The empty database's 134 MB of preallocated logs was the finding that mattered for one owner per tenant, and is fixed: 1.07 MB.
- **Moving it found three things:**
  - **An `unsigned __int128`** in the blob collector, a GCC extension that MSVC lacks, so the engine would not have compiled for Windows. Replaced with a portable 64×64-bit product.
  - **A gap in the shared crash harness.** A `flush` made a no-op passed every crash spec. The sampler crashed just before flushes, and a no-op sync issues none, so its window (the one or two operations before the next synced commit) was almost never sampled; the gate's 2,000-byte memtable also fsyncs the log on nearly every commit, which masks it further. `src/gsql/crash.c` now lands a quarter of its samples just after a sync step returns, and the gate adds a power-loss spec with a 1 MiB memtable. The same no-op gives 0 violations under the old sampler and 131 under the new. A broken toy, `syncstep`, holds the check in `tests/run_gsql_io.sh`.
  - **One guard that is a saving, not a rule.** The version store does not keep a value its own transaction made. Removing the guard leaves the model check green, because such a value sits at the transaction's own id, which no read below it reaches and the current row covers above it. It is recorded as a saving.
- **The scan path, the common fix of §6, is about 2.2× faster** (`research/gsql/README.md`, "The scan path"): a full scan of 10⁶ rows from 166–169 ms to 74–75, and G-Dash's G2 query at 10⁶ rows from 160 ms to 69–86. Most of the cost was the block cache, not the merge: a scan larger than the cache read, checksummed, decoded and evicted every block on every pass. The changes: CRC32C by the processor's instruction where there is one, one allocation per cached block, a scan that has read half the cache reads ahead without filling it, and a leaner merge step. **Against LMDB the same day the gap is about 5×** (15 against 78–80 ms for 10⁶ rows), where it was about 10×.
- **Still owed by 2.C:** W3's tail, point reads in a short run after a load (a frozen memtable flushed by reads, which wants the owner's idle time rather than its reads), and the invariant count.

**Step 6, 2026-10-29: the format fixed, with room for encryption, and 2.A deleted.**
- **2.C's format is major 1 and real** (§3.1.5): from here a database written is one every later build opens. Sorted and blob files open with a 64-byte header naming the database and the file's number; every block, footer, blob value, log record and the manifest's body carries a 28-byte nonce-and-tag reserve, zero while encryption is off; a sealed unit or a newer major is `FORMAT`, and a newer minor's added fields are passed over.
- **What the reserve costs** is 28 bytes per block (0.7% of a 4 KiB block), 28 per blob value, 36 per log record, 80 per sorted file (the header and a footer 16 bytes longer), 64 per blob file and 32 in the manifest. The background work a commit pays for is still charged on its record without the reserve, so pacing is what it was.
- **Held to it** by a committed format-1 database (`tests/gsql/format1/`, 12 KB, every file kind) that the gate opens and reads exactly; by an independent reader written from §3.1.5 (`tests/gsql/format_check.py`), which decodes that database and a busy one this build writes (compactions to level 2, relocated blobs, several logs); and by the refusals, each beside the case that must still open.
- **What the header buys, beyond a version:** a sorted file copied in from another database of the same shape used to be read as this one's, a plausible value from the wrong data. It is `CORRUPT` now, and so is a file under another number.
- **One effect of the header was a defect, found by counting:** the gate's main store has a 40-byte memtable, and compaction cut its outputs at twice that measured from the start of the file, so with 64 bytes of header every output was cut after its first key. Compactions begun rose from 1,339 to 3,242 and fsyncs nearly doubled, with every check still green. Output size and level size measure data, past the header, and the counts are back to within one fsync of the old build.
- **2.A deleted** (`research/gsql/cow.c`). Its numbers stay in the README and in §5 and §6 above; `score.py` keeps its measured line count so the recorded rounds still score.

## 7. Open questions

1. *(G-Dash volumes answered, §5.1.1.)* WorkSplicer's profile once its redesign settles (§5.1.2); G-Dash's refresh duration, which it has never measured.
2. *(Designed 2026-10-22, §3.2: one 11-byte number encoding for exact integers and doubles; −0 keyed as 0; NaN after +inf and equal to itself.)* To verify with the oracle.
3. *(Decided 2026-10-22, §3.2: an untyped `MONEY` column may be indexed, grouped by currency; ordering across currencies raises.)*
4. *(Settled 2026-10-21: the §2.D reading is right, and G-SQL is a new development.)*
5. *(Decided 2026-10-29.)* The round-trip threshold for accepting the single-protocol rule (§5.5). **The rule is accepted.** The first numbers (§5.5) are 5.6 µs added to an empty request, 8 to a point read and 11 to a 100-row batch, against queries G-Dash measures in tens of milliseconds. Matthew's reasoning, in his words: "Simplicity is a value of its own. The gain needs to be substantial to override that." An in-process path would be a second implementation of everything a client sees, which is the drift this project tripwires against.
   - **The line to re-check:** a point read should add no more than about 20 µs. Today's figures come from a prototype owner (`research/gsql/roundtrip.c`) that does not yet convert gBASIC values; they are measured again on the real owner and its SQL layer, and only a result well past the line reopens the question.
   - **Where it costs most,** and the answer that is not a second path: many tiny requests one at a time (10,000 single-row lookups, about 37 ms in process against 117 through the owner). Batching (100 rows cost 11 µs added) and pipelining, which the protocol already allows (`gsql_design.md` §6.5).
6. **Cross-database queries.** **Decided (2026-10-21) for the first release: one query reads one database.** SQLite's `ATTACH` (several databases in one query) is not offered at first, because across owners it is a distributed query; it is added only if a real application needs it. Separately: whether one database is always one file plus at most a log (proposed: yes). **Test 4 (2026-10-26) recommends no: a database is a directory.**
   - **The files.** 2.C is a manifest, a lock, a log and one sorted file per ~64 MB: 22 files at 10⁷ rows, about 16,000 at 1 TB.
   - **One file would need an extent allocator inside it**, a second space manager neither B+tree candidate needed.
   - **The sibling layout as built** interleaves two databases' files in one directory.
   - **What a directory buys.** Copy, move and delete become one operation each (`cp -r`, an atomic rename, `remove_tree`). It also closes the quieter hazard 2.B shares with SQLite: copying the main file without its log silently loses every commit not yet checkpointed.
   - **Separately, 2.C needed a cap on open sorted files**, since read files stayed open and 1 TB would exceed the default 1,024 descriptors. **Done 2026-10-27**: at most 512 read handles, the least recently used closed (`research/gsql/README.md`, "A cap on open files"). It was worse than stated here: a flush kept its output open too, so under a lowered limit the commits themselves failed; and running out of descriptors was reported as `CORRUPT`. Eviction costs about 9% of point-read throughput at worst, and nothing until a store has more files than the cap.
   - **Decided 2026-10-26: a database is a directory, and owns everything in it.** A database opens only if every name in its directory is one it could have written, so a stranger's file is refused, never deleted. At open, anything store-shaped that the database does not name is garbage and is deleted. That one rule replaced four kinds of crash bookkeeping, measured redundant and removed. For a multi-tenant deployment (G-Dash, WorkSplicer) it means one directory per tenant under a common root. A single-file artifact, where one is wanted, is the backup's or an export's output, not the live store's. The I/O layer gained `mkdir` and `list` for it (§3.4); `research/gsql/README.md`, "The directory layout", has the measurements. G-Dash's case is §5.1.1a.

## 8. Reading list

- LMDB: Howard Chu's design papers, and the `mdb.c` meta-page and free-list logic.
- BoltDB: a smaller copy-on-write B+tree, easier to read than LMDB.
- SQLite: the file format, WAL mode, the VFS layer, and "How SQLite Is Tested" (its crash and I/O-error testing is the model for §5.3).
- PostgreSQL WAL and full-page writes (torn-page protection).
- RocksDB: leveled versus universal compaction, and the RocksDB tuning guide.
- Pillai et al., *All File Systems Are Not Created Equal* (OSDI 2014), on what applications wrongly assume about `fsync` and rename.
- FoundationDB's tuple-layer encoding and CockroachDB's key encoding (§3.2).
