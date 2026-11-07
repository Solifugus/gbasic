# G-SQL `INSERT ... RETURNING` and `?` parameters

**Status:** designed and built 2026-11-07, at Matthew's request.

| part | where |
|---|---|
| parse | `item_list` and `insert_tail` in `src/gsql/sql_parse.c` |
| plan | `plan_returning` in `src/gsql/sql_plan.c` |
| execute | `ret_row` in `src/gsql/sql_exec.c` |
| module | `gsql_statement_returns_rows` in `src/modules/gsql.c` |
| tests | `tests/run_gsql_sql.sh` and `run_gsql_module.sh` |

**Why:** Transward asked for a way to learn the id a serial gave a row. Its 20 tables used SQLite's `INTEGER PRIMARY KEY` and read the id back with `last_insert_rowid` in 15 places, and G-SQL had no way to give it back. Transward drops SQLite at its M32, so the fix is the standard one: `integer serial primary key` and `RETURNING id`, not SQLite's implicit rowid. `?` placeholders are Matthew's ruling of the same day, the choice `docs/gsql_transward_corpus.md` left open.

## 1. `RETURNING`

```sql
insert into notification_rules (channel_id, event) values (?, ?) returning id;
insert into settings (key, value) values (:k, :v)
    on conflict (key) do update set value = excluded.value returning key, value;
```

- **On `INSERT`**, after `VALUES`, a query, or `ON CONFLICT`. It takes a select list: columns, `*`, `t.*`, expressions over the table's columns, with aliases.
- **A row comes back for each row the statement wrote,** in the order written. Each is read back as it was stored, so its serial, `RANDOM` values and defaults are the stored ones.
  - A row `ON CONFLICT DO UPDATE` changed comes back as it ended.
  - A row left by `DO NOTHING`, or by a `WHERE` that was not true, does not come back. These are PostgreSQL's rules, and SQLite's.
- **It reads only the row written.** `excluded`, aggregates, window functions and subqueries are refused there.
- **Not on `UPDATE` or `DELETE` in this release.** That is refused by name: nothing has asked for it yet, and the same machinery would serve it.
- **`RETURNING` is a reserved word now,** as it is in PostgreSQL. Otherwise `INSERT INTO t SELECT * FROM u RETURNING id` would read `returning` as `u`'s alias. A column named `returning` must be quoted.

**From a program:**

- **`gsql.query`** answers the rows.
- **`gsql.exec`** answers `{ command: "INSERT", rows_affected, rows }`, where `rows_affected` is the number of rows returned, as PostgreSQL's command tag counts them.
- **A `sql` block** ending in an `INSERT ... RETURNING` is iterated by `for each`, like a query block.
- **The reply is whole.** A write's reply carries no cursor, so a `RETURNING` over a million-row `INSERT ... SELECT` holds its rows in one reply. The cost is stated, not removed.
- **Inside a transaction** the rows come back at once. A rollback takes the rows away, and the serial numbers with them, so a serial stays gapless.

**How it is built.** The list is planned as `SELECT <list> FROM <table>`, so names bind, `*` expands, and the result's columns are named and kinded as a query's are. Each written row is then read back by its key and the plan's outputs are evaluated against it, as a `CHECK` is evaluated against a row in hand.

## 2. `?` parameters

```sql
select name from chan where id = ? or name = ?;   -- $1, $2
```

- **The k-th `?` in the statement's text is `$k`.** This is how SQLite, ODBC and JDBC number them. `gsql.exec(db, sql, [a, b])` binds them as it binds `$1, $2`. The canonical print is `$k`.
- **A statement uses one kind.** `?` beside `$n` or `:name` is refused (`SYNTAX`), naming where the other kind is. `?` counts by position, so `$2` beside it could mean either of two things.
- **A `?` the parser reads twice keeps its number.** The parser looks ahead and re-reads tokens, so a `?` is numbered by its place in the text, not by how many times it was met. A perturbation that dropped this went red: the re-reading really happens.
- **A `?` inside a string is text,** as before.

## 3. How it is tested

- **parse:** each `RETURNING` form printed back canonically; the `UPDATE` and `DELETE` refusals; `?` printed as `$k`, inside a subquery too; the mixed-kind refusals at their positions; `'?'` as text; `RETURNING` not taken as an alias.
- **semantics, hand-written answers:**
  - a serial's id;
  - defaults and random values as stored;
  - `*` and an expression with an alias;
  - `?` in `VALUES`, `WHERE` and a subquery;
  - `INSERT ... SELECT`;
  - `ON CONFLICT` updated rows returned as they ended, and skipped rows returned as nothing;
  - a refusal part way undone;
  - each `RETURNING` refusal.
- **the SQLite differential** (`tests/gsql/gen_dml_diff.py`). Half its upserts carry `RETURNING id, v`, `*` or an expression, and SQLite 3.35+ answers each. A census floor requires statements that return rows and ones that return none.
- **through the owner** (`tests/gsql/returning_test.bas`):
  - `gsql.query` and `gsql.exec` with `?` parameters;
  - a block's `exec` and `for each`;
  - inside a transaction, then rolled back, with the serial not used up;
  - the refusals.
- **Perturbations,** each turning a tier red:
  - no row returned for an updated row;
  - a row returned for a skipped one;
  - every `?` numbered 1;
  - the re-read numbering dropped;
  - the mixed-kind check removed;
  - `exec`'s count left unset;
  - the returned row not the stored one.
