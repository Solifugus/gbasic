# SQLite Standard Module Design

This document records the first implemented SQLite module shape and the
boundaries that should guide follow-up work.

## Goals

- Provide a small synchronous SQLite module that matches gBASIC's existing
  qualified standard-module style.
- Preserve SQLite behavior rather than hiding it behind a generic SQL layer.
- Use parameter binding for values instead of SQL string interpolation.
- Keep the module optional so gBASIC still builds without sqlite3 development
  files.

## Current API

SQLite is loaded explicitly:

```basic
load sqlite
```

The module provides:

```basic
db = sqlite.connect("app.db")
sqlite.close(db)

rows = sqlite.query(db, "select id, name from users where active = ?", [true])
result = sqlite.exec(db, "update users set active = 0 where id = ?", [10])

sqlite.begin(db)
sqlite.commit(db)
sqlite.rollback(db)

rowid = sqlite.last_insert_rowid(db)
```

Connections are opaque `sqlite_connection` values. They close explicitly with
`sqlite.close` and are also closed during interpreter cleanup when references
are released.

## Parameters

Parameters are arrays. Array position maps directly to SQLite positional
placeholders such as `?`.

Supported parameter values:

| gBASIC value | SQLite binding |
| --- | --- |
| `nothing` | `NULL` |
| boolean | integer `1` or `0` |
| number | double |
| string | text |
| date/time | ISO-like text |

Records, arrays, unknown values, connection values, files, directories, money,
durations, and blobs are not accepted as SQLite parameters in the first phase.

## Results

`sqlite.query` returns an array of records. Column names become record field
names exactly as SQLite reports them. Duplicate result column names are runtime
errors so result records remain unambiguous.

SQLite result mapping:

| SQLite storage class | gBASIC value | Notes |
| --- | --- | --- |
| `NULL` | `nothing` | Database null is absence |
| `INTEGER` | number | Large integers may lose precision |
| `REAL` | number | Stored as a gBASIC number |
| `TEXT` | string | No date/time guessing is applied |
| `BLOB` | runtime error | Future work |

`sqlite.exec` is for statements that do not return rows. If a statement returns
columns, it raises a runtime error and asks the program to use `sqlite.query`.
The returned record contains:

- `command`: uppercase first SQL word, such as `"INSERT"` or `"UPDATE"`
- `rows_affected`: `sqlite3_changes()` as a number

`sqlite.last_insert_rowid(db)` returns `sqlite3_last_insert_rowid()` for that
connection as a number.

## Transactions

`sqlite.begin`, `sqlite.commit`, and `sqlite.rollback` execute direct
transaction commands on the connection. Higher-level transaction blocks remain
future work.

## Errors

SQLite module errors use `error.source = "sqlite"` and error code `2002`.
Messages include sqlite3 error text when available.

## Non-Goals

The SQLite module should not be flattened into a generic SQL abstraction layer.
SQLite and PostgreSQL have different type systems, parameter syntax, transaction
behavior, schema semantics, conflict handling, and extension stories. A shared
lowest-common-denominator layer would hide useful backend behavior and still
leak differences in real programs.

Small convenience functions may be added later when they are genuinely useful
and backend-specific semantics are clear.

## Future Work

- `sqlite.changes(db)` or a consistent command-result extension
- readonly/open flags
- busy timeout configuration
- blob parameter and result support
- migration/script execution helper for multiple statements
- clearer large-integer story if gBASIC gains exact integers
