# G-SQL foreign keys

**Status:** designed and built 2026-11-01; `ON DELETE CASCADE` added 2026-11-07 (§6) (`fk_*` in `src/gsql/sql_exec.c`, `gsql_fkey_*` in `src/gsql/table.c`; `tests/run_gsql_sql.sh`, `run_gsql_module.sh`), from Matthew's ruling that named foreign keys replace `RELATE` (`gsql_design.md` §10F). They are the item `gsql_design.md` §13 question 7 deferred: "`FOREIGN KEY` and `CHECK` follow in the next [release], each with its own tests". `CHECK` is not in this increment.

## 1. What it is

```sql
create table share_transactions (
    id integer primary key,
    orig_member_id integer references members (member_id),
    dest_member_id integer,
    constraint destination_member foreign key (dest_member_id) references members (member_id) not enforced
);
alter table share_transactions add constraint originating_member foreign key (orig_member_id) references members (member_id);
alter table share_transactions drop constraint destination_member;
```

- **Two spellings, as standard SQL has them:** a column's `REFERENCES t (c)`, and a table's `[CONSTRAINT name] FOREIGN KEY (cols) REFERENCES t (cols)`. A foreign key may span several columns.
- **`ALTER TABLE ... ADD [CONSTRAINT name] FOREIGN KEY ...`** and **`ALTER TABLE ... DROP CONSTRAINT name`.**
- **Every foreign key has a name,** unique in the database. One not given a name is called `<table>_<columns>_fkey`, PostgreSQL's spelling, so a refusal and a tool can always name it. Several foreign keys may join the same two tables.
- **`NOT ENFORCED`** declares the relationship without checking it, the standard's form for schema knowledge over data that may not obey it. It is stored, reported and never checked.
- **The referenced columns must be the referenced table's primary key,** in its order: written out, or left off (`REFERENCES t`) to mean exactly that. That is what makes the relationship many-to-one. *As built:* referencing a unique index other than the primary key is left for later. Then a parent lookup would be an index seek rather than a row read, and nothing has asked for it yet.
- **Each pair of columns must hold the same kind,** money's currency included, since gBASIC's `=` refuses to compare across kinds.
- **It is not:**
  - `ON DELETE` actions other than `CASCADE` and `NO ACTION` (`SET NULL`, `SET DEFAULT`, `RESTRICT`), or any `ON UPDATE` action but `NO ACTION`. They are refused by name. `ON DELETE CASCADE` was added 2026-11-07 (§6); otherwise the behaviour is the standard's `NO ACTION`: a parent row still referenced is not deleted, nor its key changed.
  - `MATCH FULL`.
  - deferred checking.

## 2. What is checked, and when

Following the standard's `MATCH SIMPLE`: **a child row with any `NULL` in a foreign key's columns references nothing,** and is not checked.

- **An `INSERT` into a child table,** and an `UPDATE` changing a foreign key's columns: each new row must find its parent by one read of the parent's primary key. Otherwise the statement is refused with **`FOREIGN_KEY`**, naming the constraint and both tables. It is checked before the row is written, as `UNIQUE` is, so a refusal of a statement's first row leaves an explicit transaction usable. One on a later row of a multi-row `INSERT` is part way, as a duplicate there is.
- **A `DELETE` from a parent table,** and an `UPDATE` changing a referenced key: no child row may still reference the old key. Otherwise the statement is refused, naming the constraint and the child table. The child is **scanned** for a referencing row. **The cost is stated:** an `UPDATE` changing many referenced keys of a large child table is a scan per row. A `DELETE` since 2026-11-07 (§6) walks each child once for all the rows it removes. Using an index that leads with the foreign key's columns, the remedy PostgreSQL advises, is left for a workload that measures the need.
- **A row may not reference a row the same statement writes.** Checks run before the statement writes, so a self-referencing row inserted together with its parent is refused. Write the parent first.
- **`ALTER TABLE ... ADD ... FOREIGN KEY` checks the rows already there,** and is refused, saying how many reference nothing, unless it is `NOT ENFORCED`.

## 3. A change under a foreign key: never blocked, always reported

Matthew's rule for views (`gsql_view_design.md` §3) is that a change is never blocked and what it did is reported. It applies here for the same reason: drop-and-rebuild must work under a table's relationships just as under its views.

- **A foreign key binds to its referenced table by name.**
- **Dropping or renaming away the referenced table is allowed.** The foreign key is then **not checked**, and the statement that did it warns (**2119**): ``foreign key `originating_member` on `share_transactions` references `members`, which no longer exists: it is not checked until a table of that name returns``.
- **When a table of that name returns,** by `CREATE TABLE` or `RENAME TO`, the foreign key binds to it.
  - If the new table has no matching unique key, or the kinds differ, the warning says so and the key stays unchecked.
  - If it binds, the existing child rows are checked once. Any that reference nothing are reported (``... 3 rows of `share_transactions` reference no row of `members` ``) and are left in place, since a warning cannot refuse a change already made. New writes are checked from then on.
- **Every foreign key is re-checked after every schema change,** as views are. Its stored state (`bound`) lets a warning say what this change did, and stay silent about a key already unbound.
- **Dropping a child table drops its foreign keys.** **Renaming a child table carries them,** since a foreign key belongs to its table.
- **A program decides how strict to be,** as for views: `on warning stop` inside `begin ... commit` makes the warning an error before the commit, so the program can roll the drop back.

## 4. Storage

- **A new tree `\0fkey`**, mapping a constraint's name to a `serialize` v3 record: `{ format: 1, table, columns, references, ref_columns, enforced, bound }`, with `on_delete: "cascade"` added for a cascading key (§6). The record holds names, never positions, so it survives `ADD COLUMN` and a rebuilt parent.
- **Older binaries are unaffected,** as with views (`gsql_view_design.md` §3): an older binary does not see the tree and so does not check the keys. A database's constraints are only as strong as the binaries that write to it, which is true of any engine adding a constraint kind.
- **The checks are the SQL layer's.** They read other tables, which the table layer, whose world is one table at a time, does not.

## 5. How it is tested

- **parse:** each spelling printed back canonically, and each refusal: `MATCH FULL`, `DEFERRABLE` (and, until §6, `ON DELETE CASCADE`).
- **semantics, hand-written answers:**
  - an insert with no parent refused, and the transaction still usable;
  - `NULL` unchecked;
  - a composite key;
  - two foreign keys between the same tables;
  - a delete of a referenced parent refused, then allowed once the child row is gone;
  - an update of a referenced key refused;
  - `NOT ENFORCED` never checked;
  - `ADD CONSTRAINT` refused over orphaned rows, then accepted once they are fixed;
  - `DROP CONSTRAINT`;
  - the referenced columns not a unique key, and the kinds differing, both refused;
  - an action refused by name;
  - the generated name;
  - one namespace for constraint names.
- **the change rule:**
  - dropping the parent warns once and leaves the child writable;
  - the rebuilt parent rebinds and reports the orphans it finds;
  - renaming the child carries its keys;
  - the stored state makes a second drop silent;
  - through the real owner, `on warning stop` rolls a drop back.
- **the code:** `FOREIGN_KEY` reaches a program as `error.details.code`.

## 6. What building it found

- **A `CREATE TABLE` refused for its foreign key was "part way".** The table and its indexes were made before its keys were checked. That is harmless in autocommit, which aborts, but inside `begin` it failed the whole transaction over a declaration. A refused key now undoes its table, its indexes and the keys made before it. The statement is refused cleanly, and a case asserts the transaction carries on.
- **The undo needed a case of its own.** A perturbation skipping it would have been invisible until a case read the refused table back inside the transaction.
- **Nine perturbations, all red:**
  - an insert unchecked;
  - a parent delete unchecked;
  - `NULL` treated as a value;
  - `NOT ENFORCED` checked;
  - orphans not counted when a key is added;
  - the bound state not stored;
  - a rename not carrying the keys;
  - a refused declaration not undone;
  - the kinds unchecked.

## 6. `ON DELETE CASCADE` (2026-11-07)

Added for Transward, whose schema has ten cascading foreign keys and could not be created without them (`docs/gsql_transward_corpus.md`).

```sql
create table runs (id integer primary key, job integer references jobs on delete cascade);
create table run_files (run integer references runs on delete cascade, path text, primary key (run, path));
delete from jobs where id = 7;   -- its runs go, and their files
```

- **Spelled as the standard spells it,** on a column's `REFERENCES` or a table's `FOREIGN KEY`, and on `ALTER TABLE ... ADD`. `ON DELETE NO ACTION` and `ON UPDATE NO ACTION` are accepted and are the default, so they print as nothing. `ON DELETE` written twice is refused.
- **Everything is gathered before anything is removed.** The statement's rows come first, then every row a cascading key reaches from them, a level at a time, to the end of the chain. Each child table is walked once per level for the keys of all the rows that level found. A table referencing itself works the same way: a row and everything under it. A cycle is walked once, since a row already gathered is not gathered again.
- **Then `NO ACTION` is checked against what will be left.** A row is still referenced only by a row that is not itself being removed. So one statement may delete a parent and the rows that point at it (`DELETE FROM emp` with a boss column). Before this change that was refused, since each row was checked against the table as it stood. That is now the standard's statement-level answer. A `NO ACTION` key anywhere below a cascade refuses the whole delete, and nothing goes. The refusal names the row the cascade reached: ``foreign key `note_ord_fkey`: 1 row of `note` still references a row of `ord` that this delete of `cust` reaches by ON DELETE CASCADE``.
- **The statement counts the rows its `WHERE` named,** not the rows the cascade took. This is PostgreSQL's count. SQLite counts fewer when its own cascade reaches a named row first, in a table that references itself. The differential tier records how often that happens.
- **Not enforced, not cascaded.** A `NOT ENFORCED` key is never checked, so it never deletes either. Neither does an unbound one (§3).
- **Storage:** `on_delete: "cascade"` in the key's record, written only for a cascading key. A `NO ACTION` key's record is byte for byte what it was. A binary from before this change ignores the field, so it refuses such a delete as `NO ACTION`; it does not delete what it was not asked to. `DUMP TO` writes `ON DELETE CASCADE`, and a restore carries it.
- **The cost:** the gathered row keys are held in memory, as an `UPDATE`'s changes already are.

**How it is tested.**

- **parse:** each spelling, the canonical print, and the refusals (`SET NULL`, `RESTRICT`, `ON UPDATE CASCADE`, written twice).
- **semantics:** two levels; a `NO ACTION` key below a cascade refusing the whole statement; a rollback taking a cascade back; a tree; a cycle; `NOT ENFORCED`; a parent and its children in one statement; a key change still `NO ACTION`.
- **a SQLite differential** (`tests/gsql/gen_dml_diff.py`, five seeds of 200 statements). The schema is a chain whose keys cascade or not by the seed, plus a table referencing itself. SQLite, with foreign keys on, runs each statement first. Every table is then read back whole, so a cascade that went too far or not far enough is a difference in rows, not only in a count.
- **dump and restore** carry the action (`tests/gsql/dump_test.bas`).
- **Perturbations,** each turning a tier red:
  - no cascade followed;
  - one level only;
  - `NO ACTION` counting rows that are themselves being deleted;
  - the cascaded rows counted;
  - `DUMP` dropping the action.

