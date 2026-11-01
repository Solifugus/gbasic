# Using G-SQL well: practices, and the current limits

**Status:** Shipped, a living guide (2026-11-01). It is written for gBASIC's books, at Matthew's request, so that what they teach follows G-SQL's real strengths and weaknesses. It has two parts that age differently:

- **§1 lasts:** practices that follow from how G-SQL is designed.
- **§2 is dated:** limits of today's build, each naming the work that removes it.

Every figure cites where it was measured. When a limit closes, its entry in §2 changes in the same commit, so a book drawing on this page can check what it says.

## 1. Practices that follow from the design

### Batch your writes

**A commit costs one sync, and the sync is what slow storage charges for.** Measured (`gsql_scale_measurements.md` §2):

| per sync | 1 row/commit | 1 000 rows/commit |
|---|---|---|
| a local NVMe disk | 3 616 rows/s | 294 501 |
| 10 ms, the shape of a slow NAS | 93 | 67 739 |

So group the rows of one piece of work into one transaction: `gsql.begin`, many `exec`s, `gsql.commit`. A loop that commits every row is the commonest way to make a database look slow.

**The owner runs one writer at a time.** Several programs each committing single rows to one database share that one sync budget between them.

### One database per tenant, when tenants are independent

**A database is cheap.** About 1 MB empty (`gsql_storage_design.md`), with an owner started on demand by the first program that connects. Each tenant in its own database:

- keeps each one small, and so fast;
- means a tenant is backed up, moved or deleted as a whole;
- keeps one tenant's long report from stalling another's work, since each owner is its own process.

gdash and WorkSplicer are designed this way. How to split data is the application's decision. G-SQL works either way, and splitting one large table by key or date across databases is equally possible.

### Nothing spans two databases in one transaction

A program that writes to two databases makes two independent commits, and a crash between them leaves one done. Stay correct with:

- **idempotent writes:** a primary key or `UNIQUE` column holding a request id, so a retry is refused as a duplicate rather than applied twice;
- **an outbox:** the change and the message announcing it committed together in one database, the message delivered and marked done later.

**Aggregates across databases combine if you return their parts.** `count`, `sum`, `min` and `max` merge directly. An average is each database's `sum(x)` and `count(x)` added and divided once, never the averages averaged. What G-SQL may add later is in `gsql_multidb_design.md`.

### Run long reports somewhere else

**The owner is single-threaded** (`gsql_owner_design.md` §6.1), but a long read **runs in slices**: a `GROUP BY`, an aggregate or a sort no index gives yields every thousand or so rows, and every other client is served between slices. Measured at 10⁶ rows, a point read waits at most about 6 ms during such a query, where it waited the query's whole 0.6 to 2 s before (`gsql_slicing_design.md` §5).

- **Short queries** belong on the live database, and stay fast beside long ones.
- **A report over a large table** still runs about 10% slower there, and a hash join's inner side, a derived table, a `WITH` query and a set operation still run as one piece. Such a report belongs on a copy made by `gsql.backup` and refreshed between reports (`gsql_cookbook.md` recipe 14). Each database has its own owner, so a report there stalls nobody.

### Let indexes narrow, and read `EXPLAIN`

**An index makes a lookup cost about the size of the answer, not of the table.** Point reads stayed between 14 and 26 µs from 10⁶ to 10⁸ rows. A full scan instead costs per row: about 3.7 to 6.8 million rows a second to count, and about a million a second to test a condition.

- **Index what you look things up by.**
- **Ask `EXPLAIN`** which path a query takes. It names the index or says `Scan`.
- **An index only narrows.** Every condition is still checked on every row it yields, so a wrong index is slow, never wrong.

### Keys: `serial` for order, `random` for secrecy

- **`id integer serial primary key`** numbers rows 1, 2, 3 with no gaps (a rollback or a refused row spends no number), and never reissues a number.
- **`n integer serial of (task)`** numbers within each task. Declare the key as `(task, n)`.
- **`id text random primary key`** gives 22 URL-safe characters nobody can guess, which is what a link or a user's identifier wants.

(`gsql_generated_design.md`)

### SQL means what gBASIC means

Every operator in a query is gBASIC's own (`gsql_sql_design.md` §2):

- money keeps its currency;
- an integer past 2^53 stays exact;
- `unknown` is SQL's `NULL`, and three-valued logic is gBASIC's.

There are **no implicit casts**: `'1' = 1` is refused, and `CAST` says what you mean. What a program computes and what a query computes therefore cannot quietly disagree.

### A schema change is never blocked, and is always reported

Dropping or rebuilding a table under a view or a foreign key is allowed. Warning 2118 or 2119 says what it affected. Under `on warning stop` inside `gsql.begin`, that warning becomes an error before the commit, so a careful migration can roll back (`gsql_view_design.md`, `gsql_fkey_design.md`).

## 2. Current limits, dated 2026-11-01

Each line names what removes it. The order of that work is `gsql_scale_measurements.md` §4.

| limit | until | meanwhile |
|---|---|---|
| A sort no index gives, `GROUP BY`, `DISTINCT`, a set operation, a derived table, a hash join's inner side and `INSERT ... SELECT` hold their rows **in memory**. A billion-row one ends the owner partway. Nothing committed is lost. | spill-to-disk | Keep such queries to what fits in memory; index the sort or join column |
| *Closed 2026-11-01 for `CREATE INDEX`:* building an index over existing rows grows linearly now, 15.8 s at 10⁷ rows (was 37.5 s), by sorting its entries first and moving sorted runs down unrewritten (`gsql_spill_design.md`). What remains: **a huge `INSERT ... SELECT` or `UPDATE` in one transaction** still writes in row order. | the query operators' spill | Create indexes before or after loading, either is fine now; change data in batches of thousands of rows per transaction |
| Load rate halves between 10⁷ and 10⁸ rows (172 000 to 90 000 rows/s). | profiling compaction | Plan a billion-row load in hours, not minutes |
| **Joins run in the order written.** A large table placed where it is hashed is held whole. | row counts in the planner | Put the large table first in `FROM`, and check `EXPLAIN` |
| *Closed 2026-11-01:* `gsql.backup(db, dir)` copies while the database is in use and refreshes an existing copy (`gsql_backup_design.md`), and a copy a program has opened can be refreshed once it is closed (`gsql_owner_lifetime_design.md`). What remains: a refresh always rewrites the unflushed changes, up to 32 MB. | the copy's owner pulling its own refreshes | Open a copy you will refresh with `{ linger: 0 }`. A plain file copy is still safe with no owner running |
| A hash join's inner side, a derived table or `WITH` query and a set operation's sides **run as one piece**, stalling other clients of the database while they do. A sort, a `GROUP BY` and the aggregates are sliced (`gsql_slicing_design.md`). | slicing those steps | Run such reports on a copy (§1) |
| *Closed 2026-11-01:* `gsql.dump` writes the database as SQL text, schema-only too, and `gsql.restore` runs one (`gsql_dump_design.md`, cookbook recipe 15). What remains: **a record holding a date or money, and a serial numbered within money, cannot be dumped**, and are refused by name. | a typed-text literal, if a real one needs it | Keep record columns JSON-shaped |
| *Closed 2026-11-01:* one owner per database is enforced on any storage, including shared storage whose file locks are local to each machine (`gsql_lease_design.md`). What remains: **after a crash on one machine, opening from another waits about 14 seconds**; and an owner stalled for more than 8 seconds in the middle of a write can still land that write after another machine took over, since a file on NFS or SMB cannot refuse a stale writer. | storage that fences | Open a database from one machine where you can; after a crash, reopen from the same machine, which waits for nothing |
| *Closed 2026-11-01:* concurrent commits share one sync, the owner waiting up to a quarter of its measured sync for the connections that are committing (`gsql_group_commit_design.md`): eight clients at 5 ms a sync make 7 times one client's commits. What remains: **one program committing one row at a time is still paced by its storage**, about 93 commits a second on a 10 ms NAS. | — | Batch writes (§1); many concurrent writers now share syncs |
| A foreign key must reference its parent's **primary key**. | — | Reference the primary key |
| `stddev`, `variance`, median and window functions are not in SQL. | Phase 3 | Fetch the rows and use gBASIC's `stats` library |
