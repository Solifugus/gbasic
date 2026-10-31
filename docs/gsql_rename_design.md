# G-SQL `ALTER TABLE ... RENAME TO`

**Status:** designed 2026-11-01. The second of three increments that views depend on (`gsql_view_design.md` §3). Matthew's drop-and-rebuild workflow needs it: build `t_new`, drop `t`, rename `t_new` to `t`.

## 1. What it is

```sql
alter table t_new rename to t;
```

- **It moves one catalog entry.** A table's trees are named by its **id**, not its name (`gsql_table_design.md` §2: "a rename later will touch one catalog entry"). So a rename deletes the catalog entry under the old name and writes the same schema under the new one. The rows, indexes, defaults, `NOT NULL` and the hidden row-id counter are all keyed by the id, so they come along untouched.
- **Index names do not change.** `CREATE INDEX t_new_by_k` keeps that name after the rename.
- **Refusals:**
  - renaming a table that is not there is `NOT_FOUND`;
  - renaming to a name a table already has, including its own, is `EXISTS`;
  - an empty name, or one past the limit, is `SCHEMA`, as `CREATE TABLE`'s are.
- **It is a write like `ADD COLUMN`,** done in the statement's transaction, so a rename inside `begin ... rollback` is undone.
- **It is not `RENAME COLUMN`.** That is a separate change, with no need yet.

## 2. How it is tested

- **`run_gsql_table.sh`, the table layer alone:**
  - a renamed table opens under its new name with every row and index;
  - its old name is `NOT_FOUND`;
  - the invariant check passes;
  - each refusal sits beside the accepted case;
  - a rename in an aborted transaction leaves the old name.
- **`run_gsql_sql.sh`, hand-written answers:** the drop-and-rebuild swap end to end, an index still used after the rename (`EXPLAIN`), the refusals, and a parse case printed back canonically.
