# G-SQL foreign keys

**Status:** designed and built 2026-11-01 (`fk_*` in `src/gsql/sql_exec.c`, `gsql_fkey_*` in `src/gsql/table.c`; `tests/run_gsql_sql.sh`, `run_gsql_module.sh`), from Matthew's ruling that named foreign keys replace `RELATE` (`gsql_design.md` §10F). They are the item `gsql_design.md` §13 question 7 deferred: "`FOREIGN KEY` and `CHECK` follow in the next [release], each with its own tests". `CHECK` is not in this increment.

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
  - `ON DELETE` or `ON UPDATE` actions (`CASCADE`, `SET NULL`, `SET DEFAULT`). They are refused by name. The only behaviour is the standard's `NO ACTION`: a parent row still referenced is not deleted, nor its key changed.
  - `MATCH FULL`.
  - deferred checking.

## 2. What is checked, and when

Following the standard's `MATCH SIMPLE`: **a child row with any `NULL` in a foreign key's columns references nothing,** and is not checked.

- **An `INSERT` into a child table,** and an `UPDATE` changing a foreign key's columns: each new row must find its parent by one read of the parent's primary key. Otherwise the statement is refused with **`FOREIGN_KEY`**, naming the constraint and both tables. It is checked before the row is written, as `UNIQUE` is, so a refusal of a statement's first row leaves an explicit transaction usable. One on a later row of a multi-row `INSERT` is part way, as a duplicate there is.
- **A `DELETE` from a parent table,** and an `UPDATE` changing a referenced key: no child row may still reference the old key. Otherwise the statement is refused, naming the constraint and the child table. The child is read through an index that leads with the foreign key's columns if there is one, and is scanned otherwise. **The cost is stated:** deleting many parent rows from a large child table with no such index is a scan per row, as it is in PostgreSQL, which advises the same index.
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

- **A new tree `\0fkey`**, mapping a constraint's name to a `serialize` v3 record: `{ format: 1, table, columns, references, ref_columns, enforced, bound }`. The record holds names, never positions, so it survives `ADD COLUMN` and a rebuilt parent.
- **Older binaries are unaffected,** as with views (`gsql_view_design.md` §3): an older binary does not see the tree and so does not check the keys. A database's constraints are only as strong as the binaries that write to it, which is true of any engine adding a constraint kind.
- **The checks are the SQL layer's.** They read other tables, which the table layer, whose world is one table at a time, does not.

## 5. How it is tested

- **parse:** each spelling printed back canonically, and each refusal: `ON DELETE CASCADE`, `MATCH FULL`, `DEFERRABLE`.
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
