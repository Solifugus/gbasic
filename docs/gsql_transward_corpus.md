# Transward's SQL against G-SQL

**2026-11-07.** Transward's §6 asked for a dialect check of the SQL it runs, as
gdash's was checked. Every statement Transward runs is a function in
`transward/src/database.bas` (its M19), so that file is the corpus. Measured
against the gBASIC tree at eb1c87c plus the `try` work (no G-SQL change
between them).

## How it was checked

- **The schema as Transward builds it.** The check loads Transward's own
  `database` library and calls `database._migrations()`, so the DDL is exactly
  what Transward applies: 56 statements over 12 migrations, run in order.
- **Every other statement, extracted.** 155 driver calls: 151 are a literal or
  a `join([...])` of literal lines, extracted by a script; 4 are built at run
  time (the connection pragmas, the migration loop, `outbox_due`, whose
  `join` holds a comment, and `inventory`'s eight `count(*)`), checked by hand.
- **Planned, not run.** Each statement goes through `db.columns`, which parses
  and plans it with nothing run (gdash's A2). A statement that is not a query
  reaches its "returns no rows" refusal only after it planned, so that refusal
  counts as a pass. This finds every parse, name, function and grouping gap;
  it does not measure runtime behaviour (comparisons over Transward's epoch
  integers and text, NULL handling in its data).
- **`?` converted to `$1`, `$2`, ...** so the rest could be seen; that is the
  first finding below.

## What it found

| finding | where | whose change |
|---|---|---|
| `?` placeholders: G-SQL takes `$1` or `:name`, and refuses `?` naming both | every statement with parameters | Transward's mechanical edit, or G-SQL accepting `?` (SQLite's, ODBC's and JDBC's spelling) |
| `ON DELETE CASCADE` on a foreign key: "not in this release of G-SQL" | 10 foreign keys, in 9 tables; every table after them failed to exist | **G-SQL gap** |
| a column named `key`, a reserved word in G-SQL's SQL | `settings`, `idempotency_keys` (and their statements) | G-SQL (PostgreSQL treats KEY as non-reserved), or Transward quotes it |
| `INSERT ... ON CONFLICT (key) DO UPDATE` (upsert): "not in this release" | `setting_put` | **G-SQL gap** |
| `INSERT OR IGNORE`, `INSERT OR REPLACE` | `idempotency_remember`, `transcript_save`, `rule_insert` | SQLite's own spellings; Transward rewrites them as `ON CONFLICT ... DO NOTHING / DO UPDATE` once G-SQL has it |
| `pragma journal_mode=wal`, `foreign_keys=on`, `busy_timeout=` | `connect` | Transward drops them: G-SQL has no pragmas and needs none (one owner, constraints always on) |
| a bare column beside `max()` in a `GROUP BY` | `negotiated_by_endpoint`, `banner_by_endpoint` | Transward: SQLite fills a bare column from the row holding the max, a SQLite-only behaviour PostgreSQL also refuses; a window function or a join on the max says it portably |

With the two DDL gaps worked around inside the check only (the cascade
dropped, `key` quoted), all 56 migration statements ran, and 140 of the 151
extracted statements planned; the other 11 are the rows above, plus two that
failed only because the check planned rather than ran Transward's `create
table if not exists schema_version` (G-SQL takes `if not exists`; measured).
Both hand-checked dynamic statements planned.

## For G-SQL

Three gaps are G-SQL's, in the order they block Transward: `ON DELETE
CASCADE` (a schema cannot be created without it, or without rewriting the
design's deletions), the upsert (`ON CONFLICT`), and `key` as a name. A fourth
is a choice: accepting `?`.

**The three were built 2026-11-07**, at Matthew's request:

- `ON DELETE CASCADE`: `docs/gsql_fkey_design.md` §6.
- `INSERT ... ON CONFLICT`: `docs/gsql_upsert_design.md`.
- `key` is no longer reserved.

Each is checked against SQLite by `tests/gsql/gen_dml_diff.py`. The corpus
check above was not re-run against them. What changes for Transward:

- its migrations should now run without the two workarounds;
- `setting_put` runs as written;
- `INSERT OR IGNORE` and `INSERT OR REPLACE` are refused with the `ON
  CONFLICT` spelling to use instead.

`?` is still not accepted.
