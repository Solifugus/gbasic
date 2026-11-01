# G-SQL: group commit, paced by the storage it finds

**Status:** Designed 2026-11-01. It is item 5 of the work order in `gsql_scale_measurements.md` §4: *"group commit sized from the measured sync, and a storage probe"*. It closes the limit `gsql_practices.md` §2 records: *the owner sizes nothing to the storage: commit pacing is the same on NVMe as on a NAS*.

## 1. The gap

Every commit the owner makes is synced before it answers (`owner.c`, `commit(t, 1, ...)`). The owner runs one writer at a time, so every client of a database shares one budget of commits per second, and that budget is the sync's inverse. Measured with a sync delay injected (`gsql_scale_measurements.md` §2):

| added per sync | commits/s, one row each |
|---|---|
| 0 (NVMe) | 3 616 |
| 2 ms | 417 |
| 10 ms | 93 |

On a NAS at 10 ms, **eight programs each inserting one row at a time get 93 rows a second between them**, while the disk could have made each of those syncs cover all eight.

The engine already supports the fix (`gsql_storage_design.md` §1.3): `commit(txn, sync = false)` orders a commit without making it durable, `sync(db)` makes everything before it durable in one flush, and **a commit is visible to new readers only once it is durable**, so no client ever reads what a crash could remove.

## 2. The rule

**A commit's reply waits for the sync that covers it, and the owner syncs when nobody else is about to commit, or when the oldest waiting reply has waited about as long as one sync takes.**

- **Alone, nothing changes.** A commit with no other writer queued is synced at once and answered, as today. Group commit never adds latency to a client that has the database to itself.
- **Under concurrency, demand forms the group.** When a commit finishes and another connection is already queued for the writer, the owner commits without syncing, lets that writer go, and keeps the reply. Each further queued writer joins the group the same way. When the writer queue is empty, one sync covers the whole group, and every reply goes out.
- **The bound comes from the storage.** A group is also synced once its oldest reply has waited `max(1 ms, the measured sync)`: waiting longer than a sync takes would buy less than it costs. On NVMe, where a sync is about 0.3 ms, a group lives at most 1 ms. On a NAS at 10 ms, it lives up to 10 ms and covers whatever arrived meanwhile.
- **The measured sync is the store's own,** a running median of the last 32 log syncs and lease renewals, which are the same operation on the same filesystem. The **storage probe** is its first value: the lease's first write at open, timed. So the owner knows its storage before its first commit, without a benchmark of its own.

**Nothing about durability changes.** A client is told its commit succeeded only once a sync covers it, exactly as now. A crash before that sync loses commits nobody was told about, which is what a crash before a synced commit's reply already does.

## 3. What a waiting reply sees

- **Its own transaction's writes** are already in the memtable, as now.
- **Another client's read** begins at the last durable commit, so it does not see a group's commits until their sync. A read-after-write by a *different* client therefore waits up to one bound, which is the cost `gsql_storage_design.md` §5.4 named. The writing client itself, having had no reply, cannot have sent its next request yet (unless it pipelines, §5).
- **A sync that fails** fails every commit in the group: each waiting reply becomes a `STORAGE` error saying the commit's durability is unknown, and the store is unusable until reopened, which is what a failed sync does today (`gsql_storage_design.md` §1.3, fsyncgate).

## 4. Where the owner does it

- **`do_commit` and an autocommit write** call `commit(t, 0, &id)` and append `(connection, request, id, result)` to the group, instead of committing with sync and replying.
- **The serve loop**, after `process` has run every connection and before it polls, syncs and replies if the group is not empty and either **no request is parked waiting for the writer** or the group's oldest entry is past the bound. The poll's timeout is capped at the bound's remainder.
- **A connection that drops** with a reply in the group loses only the reply; its commit stands, and the sync still covers it.
- **The planned handoff and SIGTERM** sync the group and send its replies before flushing.

## 5. Decisions

- **No timer when alone.** A group-commit interval that every commit waits out, as some databases offer, makes the common single-client case slower to help the concurrent one. Demand-formed groups cost nothing when there is no demand.
- **No option.** The bound is measured, not configured. A setting would be one more thing to get wrong on storage nobody can see.
- **A pipelining client** (several requests sent before reading replies) has its later requests processed while its commit's reply waits. They see the client's own commit, since they run on the same owner after it, but another client does not until the sync. That is consistent with a snapshot read beginning at the durable commit.

## 6. Tests

- **The gain, as a ratio:** with a sync delay injected (the same seam §2's table used), eight concurrent clients committing one row each must reach several times the commits per second of the same clients without group commit. The control is one client, whose rate must not fall.
- **Durability:** a reply is never sent before a sync covers its commit, asserted with the simulated layer: crash after any operation, and every commit a client was told about is present after recovery.
- **Visibility:** a second client reading while a group waits does not see it, and sees it once replied.
- **A failed sync** fails every reply in the group, and none succeeds.
- **The bound:** the group's oldest reply waits no more than about twice the measured sync.
- **The probe:** the store reports the measured sync, and it moves with the injected delay.
