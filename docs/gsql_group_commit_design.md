# G-SQL: group commit, paced by the storage it finds

**Status:** Shipped 2026-11-01 (`group_*` in `src/gsql/owner.c`; the group tier of `tests/run_gsql_owner.sh`). What building it changed is §8. It is item 5 of the work order in `gsql_scale_measurements.md` §4: *"group commit sized from the measured sync, and a storage probe"*. It closes the limit `gsql_practices.md` §2 records: *the owner sizes nothing to the storage: commit pacing is the same on NVMe as on a NAS*.

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

**A commit's reply waits for the sync that covers it. The owner syncs once per round of serving its connections, and a round waits up to a quarter of one measured sync for connections that are committing.**

- **Alone, nothing changes.** A commit with nobody else committing is synced at the end of its round and answered, as before. The one connection that could commit again is waiting for this very reply, so nothing waits on its behalf.
- **Under concurrency, arrival forms the group.** The owner is single-threaded, so the requests that arrive while one sync runs are all served in the next round, each committed without a sync, and one sync covers them.
- **A round waits briefly for stragglers.** A client answers its reply a moment after the sync that released it, and would miss the next round's sync by microseconds. So a round with commits in it waits, up to **a quarter of the measured sync** (at most 2 ms), while some connection that has committed in the last second is not already in the group. Waiting a quarter of a sync to save whole syncs is the trade, and it is sized from the storage rather than configured.
- **The measured sync is the owner's own:** the median of its last 16 group syncs. **The storage probe is its first value:** the owner times one lease renewal at open (an open, a sector written and synced, and a close), so it knows its storage before its first commit, without a benchmark of its own.

**Nothing about durability changes.** A client is told its commit succeeded only once a sync covers it, exactly as before. A crash before that sync loses commits nobody was told about, which is what a crash before a synced commit's reply already did.

## 3. What a waiting reply sees

- **Its own transaction's writes** are already in the memtable, as now.
- **Another client's read** begins at the last durable commit, so it does not see a group's commits until their sync. A different client can only have learned of a commit from its writer, after the reply, and by then the sync is done; a read sent before that is not ordered after the commit. The writing client is answered nothing more until the sync (§4).
- **A sync that fails** fails every commit in the group: each waiting reply becomes a `STORAGE` error saying the commit's durability is unknown, and the store is unusable until reopened, which is what a failed sync does today (`gsql_storage_design.md` §1.3, fsyncgate).

## 4. Where the owner does it

- **`do_commit` and an autocommit write** call `commit(t, 0, &id)` and keep their `DONE` frame back, with the connection, instead of committing with a sync and replying. **An autocommit statement whose reply carries rows** is still synced at once: its reply is built from a cursor, and nothing measured needs it grouped.
- **A connection with a reply waiting is answered nothing more** until the sync, so its replies stay in order. A client that pipelines therefore waits for its commit before its next request is served.
- **The serve loop**, after every connection has been served and every working read has had its slice, syncs the group unless it is gathering (§2), then sends the replies. While gathering, it polls again within the bound.
- **A connection that drops** with a reply waiting loses only the reply; its commit stands, and the sync still covers it.
- **The planned handoff and SIGTERM** sync the group and send its replies before flushing.

## 5. Decisions

- **No fixed interval.** A group-commit interval every commit waits out makes the common single-client case slower to help the concurrent one.
- **No option.** The bound is measured, not configured.
- **A quarter, capped at 2 ms**, because waiting a fraction of a sync can only cost that fraction, and the cap keeps a slow NAS from turning a straggler wait into a visible pause.

## 6. Tests

`tests/run_gsql_owner.sh`'s **group** tier injects a 5 ms sync into the test owner (the seam `research/gsql/syncbench.c` used for §1's table) and runs one client, then eight at once, half committing explicitly and half in autocommit:

- **The gain, as a ratio:** eight clients must make more than 3 times one client's commits a second. It was 5.5 for a day, and the parallel gate measured 4.56 on a working build, its eight clients competing for the CPU; the builds it must catch read 1.96 (no gather) and 1.55 at most (no grouping).
- **No early reply:** a single client must stay under 210 commits a second, since a client that waits for each reply cannot outrun a 5 ms sync.
- **Every commit lands:** the database holds every row committed.
- **A failed sync** answers no commit `DONE`, and says the commit's durability is unknown.

## 7. Measured

| storage | one client | eight clients | ratio |
|---|---|---|---|
| 5 ms a sync, before | 182/s | 181/s | 1.00 |
| 5 ms a sync, groups by round only | 186/s | 744/s | 4.0 |
| 5 ms a sync, with the gather | 182–190/s | 1 222–1 394/s | 6.6–8.2 |
| NVMe ext4, before | 2 829–2 902/s | 3 032/s | 1.04 |
| NVMe ext4, with the gather | 2 673–2 855/s | 16 981/s | 6.2 |

One client on NVMe is 1.7% slower at the median, which is inside run-to-run spread. Under sixteen busy loops on 22 cores the ratio held at 7.3.

## 8. What building it changed and found

- **The design's first rule was the wrong mechanism.** It said a group forms from writers queued for the writer slot, and an autocommit write never queues there: it takes the slot and releases it within one call. What forms a group is the owner's round. Grouping by round alone gave 4.0, and waiting for stragglers gave the rest.
- **A check that could not fail, twice:** the first group tier used only autocommit, so removing the explicit `COMMIT`'s grouping left it green; and its threshold of 3 passed the build without the gather, at 3.93. Half its clients commit explicitly now, the threshold is 5.5, and without the gather the ratio falls to 1.96, since an explicit transaction holds the writer across round trips and a group then rarely forms by itself.
- **Not asserted:** a single client that wrongly waited for others would make about 155 commits a second here, against 171 to 192 measured, including under load. That margin is too thin to assert without a flaky gate, so the property rests on the code (a connection already waiting for its reply is not counted) and this note.

**Perturbations proven red:** no gather, no grouping on the explicit path, no grouping in autocommit, a failed sync answered `DONE`, and replies sent before their sync.
