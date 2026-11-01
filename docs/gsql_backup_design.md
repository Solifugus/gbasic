# G-SQL online backup, and refreshing a copy

**Status:** designed and built 2026-11-01 (`backup_*` in `src/gsql/lsm.c`, `BACKUP TO` in `src/gsql/sql_exec.c`, `gsql.backup` in `src/modules/gsql.c`; `tests/run_gsql_store.sh`, `run_gsql_module.sh`, `run_gsql_sql.sh`). Matthew asked whether copying a database's files backs it up. It does only with no owner running, so a backup that does not stop the database is needed. He then asked whether the backup could also act as replication, so long reports can run on a copy (`gsql_scale_measurements.md` §3: the owner is single-threaded, and a report's sort or grouping stalls every other client).

## 1. What already exists, and what does not

**The engine has a consistent online backup** (`backup_begin`/`step`/`end` in `src/gsql/lsm.c`, held by `run_gsql_store.sh`). It has three properties:

- **Consistent:** it copies a read snapshot.
- **Physical:** the snapshot's immutable sorted and blob files are copied byte for byte, and the unflushed changes are written as one more file.
- **Sliced:** each step copies a bounded number of bytes, so writers commit between steps.

**Nothing a program can call reaches it:** neither the owner, the SQL layer nor the `gsql` module.

**And it always starts from nothing:** it clears the destination first, so a second backup copies the whole database again.

## 2. What it is

```basic
r = gsql.backup(db, "/backups/shop-copy")
print r.bytes, r.files, r.reused       ' what was copied, and what was already there
```

- **A consistent copy as of the moment it starts,** taken while the database stays in use. Other clients' reads and writes go on between its slices.
- **The copy is an ordinary database.** `gsql.connect` opens it, and restoring is pointing a program at it, or copying its directory back with no owner running.
- **Backing up into an existing copy of the same database refreshes it:**
  - every file the copy already holds is kept and not copied again, since a sorted file never changes once written;
  - files the database no longer has are removed from the copy;
  - the cost is roughly what changed since the last backup, not the size of the database. A large compaction rewrites files, so the refresh after one copies more.
- **Refused, saying why:**
  - **a copy in use:** an owner holds it. That includes aiming the backup at the live database itself, whose owner holds its lock;
  - **a directory holding another database's files,** or anything that is not a database;
  - **a path that is not text.**

**The statement underneath** is `BACKUP TO 'dir'`, and it returns **progress rows**, one per slice. That gives slicing for nothing:

- a cursor already keeps its snapshot between fetches;
- the owner already serves other clients between fetches.

`gsql.backup` simply reads the rows to the end. A program that wants to show progress can read them itself, with `gsql.query`.

A relative path is resolved against the **program's** working directory, not the owner's, and sent as an absolute path.

## 3. Two things that must be right

**A kept file must be the same file.** A file is kept only when all of these hold:

- the snapshot names a file of that number;
- the copy holds a file of that number and that size;
- the copy's header names the same database, the same kind and the same number.

**The unflushed changes need a number the source will never use.** The engine wrote them under the source's *next* file number, and the source later gives that number to a different file. An incremental refresh would then find a file matching in name, size, kind and database, keep it, and serve wrong data in silence. The backup therefore numbers that file from a range the source never allocates: 2⁶² plus the snapshot's sequence. The next refresh never keeps it, because no snapshot names such a number.

**Order, so a crash mid-refresh never leaves a copy that looks complete but is not:**

1. Remove the copy's manifest.
2. Remove the files the snapshot does not name.
3. Copy the missing files.
4. Write the new manifest, by the store's own temporary file and rename.

A crash before step 4 leaves no manifest: the copy does not open, and the next backup completes it. It never leaves a copy that opens and is wrong.

## 4. The replica this makes, and what it is not yet

A copy refreshed on a schedule is a **replica for reports**: as fresh as its last refresh, and on another owner, so a report on it stalls nobody on the live database.

**Not in this increment:**

- **Refreshing a copy while it is open.** It is refused (§2). Reports and refreshes alternate, or alternate between two copies.

  The real answer is for the copy's own owner to pull its refreshes, installing new files between queries without stopping. That is the next step.
- **Marking a copy read-only,** so a program cannot write to it by mistake and then lose the write at the next refresh. Today a copy is an ordinary, writable database.
- **A live replica, seconds behind.** That needs the change feed (`gsql_multidb_design.md` §2.3).

## 5. How it is tested

- **A backup under load:** a second connection commits between the slices. The copy holds exactly the rows committed before the backup began, none after, and the live database holds them all.
- **A refresh:**
  - after more commits, a second backup into the copy copies only the new files, reports the rest as reused, and holds the new rows;
  - after a compaction, it removes what the database no longer has, and holds the same rows as the database.
- **The number trap:** a refresh after the source has written past the old memtable file's number keeps nothing wrong. The copy's rows equal the source's at the refresh.
- **The refusals:**
  - the live database itself as the destination;
  - a copy with an owner running;
  - a directory holding another database;
  - a relative path, resolved against the program's directory.
- **A crash during a refresh** leaves a copy with no manifest, never a wrong one. Driven through the engine's simulated I/O, as `run_gsql_store.sh` drives the existing backup.
- **The copy opens and answers** through `gsql.connect`.

## 6. What building it found

- **A copy a program has opened stayed held until that program ended,** because an owner lived as long as the program that started it, so a refresh in the same program was refused as `STATE`. *Closed 2026-11-01* by the owner's linger (`gsql_owner_lifetime_design.md`): open the copy with `{ linger: 0 }`, close it, and the refresh goes ahead at once, measured. With the default linger, a refresh succeeds once the copy has been closed for 3 seconds.
- **A refresh always rewrites the unflushed changes,** up to the 32 MB memtable, however little changed. Measured at 32 MB of a 60 MB database: negligible at many gigabytes, half of a small one. The test asserts that a refresh keeps files and copies less, not that it copies "far less".
- **Without the destination's lock, a backup aimed at the live database damaged it in a way the open process could not see.** It removed the database's manifest and files while the process went on reading through handles it already held. The test now checks that the database reopens.
- **The header check had no test** until two databases with identical writes were built. Their files match in number and size and differ only in the identity their headers name, and without the check one's backup over the other's copy kept the wrong database's files.
- **Seven perturbations, all red:**
  - in the engine:
    - the unflushed changes under the source's next number;
    - nothing kept;
    - no lock (red only once the test reopened the database);
    - no header check (red only once the twin databases existed);
  - in the SQL layer and the module:
    - a relative path sent as written;
    - the backup counted as a write;
    - the backup never ended, so its lock stays held.
- **Not observable by any test here:** that a fetch copies one slice. A backup that copied everything in its first fetch would answer the same, and would only hold up other clients for longer. It is held by `backup_batch`'s structure, with this sentence as its record.

