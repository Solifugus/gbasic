# G-SQL `INSERT ... SELECT`

**Status:** designed 2026-11-01. Matthew's drop-and-rebuild workflow needs it: build `t_new`, copy `t`'s rows into it, drop `t`, then rename `t_new` to `t` (`gsql_rename_design.md`). Until now the copy step had to be done row by row from a program.

## 1. What it is

```sql
insert into t_new (id, k, note) select id, k, 'migrated' from t where k is not null;
insert into archive with old as (select * from orders where placed < date '2026-01-01') select * from old;
```

- **Any `SELECT`** may follow the column list: joins, grouping, set operations, `WITH`, `ORDER BY`, `LIMIT`.
- **It must yield as many columns as the target names,** or as the table has when no column list is written. Otherwise it is refused, naming both counts.
- **A column the list leaves out takes its default,** as with `VALUES`.

## 2. Semantics

- **The query is read in full before any row is written.** So an `INSERT INTO t SELECT ... FROM t` sees `t` as the statement found it, never its own new rows. This is the Halloween rule `UPDATE` and `DELETE` already follow (`sql_exec.c`'s change list).
- **Each row is then inserted exactly as a `VALUES` row is:** the column kinds, `NOT NULL`, `UNIQUE`, defaults and foreign keys (`gsql_fkey_design.md`) all apply.
- **A refusal on the first row leaves an explicit transaction usable. One on a later row is part way,** as for a multi-row `VALUES`. The message names the row's position in the query's result.
- **The rows are held in memory** between reading and writing. That is the third place spill-to-disk will be needed, after derived tables and hash joins.

## 3. How it is tested

`run_gsql_sql.sh`, hand-written answers:

- a copy with a column list and a default;
- a copy of a table into itself, doubling it exactly once;
- a `WITH` and a join as the source;
- a column-count mismatch refused;
- a kind refused on a later row, part way, with its position;
- a foreign key checked on each row;
- the drop-and-rebuild workflow end to end, with its view and foreign key coming back.

The parse tier prints each form back canonically.
