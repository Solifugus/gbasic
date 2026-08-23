# PostgreSQL Standard Module Design

## Status

Implemented for the synchronous core API and transaction/type-coverage phases:

- `load pg`
- `pg.connect`, `pg.close`
- `pg.query`, `pg.exec`
- positional parameter arrays
- opaque, automatically cleaned-up connection values
- arrays of row records
- explicit transactions
- SQLSTATE runtime diagnostics
- documented scalar, JSON, and date/time mappings

Prepared statements, pooling, `COPY`, `LISTEN`/`NOTIFY`, asynchronous queries,
PostgreSQL arrays, and `with transaction` remain future phases.

## Goals

- Provide a small, readable BASIC-style API.
- Be suitable for long-running and serious applications.
- Preserve useful PostgreSQL behavior instead of hiding it behind a generic
  lowest-common-denominator database layer.
- Represent ordinary query results with gBASIC arrays, records, and scalar
  values.
- Keep connection and statement resources explicit.
- Use parameterized SQL by default in examples and documentation.
- Integrate with the existing gBASIC runtime error model.

## Recommended Module Shape

PostgreSQL should be an explicitly loaded standard module:

```basic
load pg
```

The public API should use the module namespace:

```basic
db = pg.connect({
    host:"localhost",
    database:"worksplicer",
    user:"app",
    password:"secret"
})
```

The namespace keeps related operations together without repeating a `pg_`
prefix, and leaves room for future `sqlite.*` and `mysql.*` modules. Unqualified
aliases such as `pg_connect(...)` should not be part of the initial public API.

## Recommended API

Initial public API:

```basic
db = pg.connect(config)
pg.close(db)

rows = pg.query(db, sql)
rows = pg.query(db, sql, params)

result = pg.exec(db, sql)
result = pg.exec(db, sql, params)

pg.begin(db)
pg.commit(db)
pg.rollback(db)
```

All calls are synchronous in the first implementation.

### Connection Model

`pg.connect(config_record)` should return an opaque native connection value.
It should not return a normal mutable record.

Recommended configuration fields:

- `host`
- `port`
- `database`
- `user`
- `password`
- `sslmode`
- `connect_timeout`
- `application_name`

All fields are optional except those required by the server environment.
Missing fields should allow libpq environment/default resolution. Unknown
configuration fields should be rejected so misspellings do not silently change
connection behavior.

Example:

```basic
db = pg.connect({
    host:"localhost",
    port:5432,
    database:"worksplicer",
    user:"app",
    password:"secret",
    sslmode:"require"
})
```

The opaque value should:

- own one PostgreSQL connection
- reject use after `pg.close`
- close during interpreter cleanup if not closed explicitly
- expose a useful type name such as `"postgres_connection"`
- not reveal passwords through `print`, `string`, or serialization
- not be copyable through `encode`/`decode`

Connection failure should raise a runtime error. The error source should be
`postgres`, and the message should include the useful server or libpq message
without echoing the password or full connection string.

An ordinary record is rejected for the connection itself because records are
copyable, serializable, and user-mutable. A record cannot safely express native
resource ownership or closed-state checks. A record returned by a future
`pg.connection_info(db)` function is appropriate for non-secret metadata.

### Query API

Recommended forms:

```basic
rows = pg.query(db, sql)
rows = pg.query(db, sql, params)
```

Example:

```basic
rows = pg.query(
    db,
    "select id, name from users where active = $1",
    [true]
)
```

Parameters should be an array. Array position maps directly to PostgreSQL
placeholders `$1`, `$2`, and so on. The module must send parameter values
separately through libpq parameter APIs; it must never construct SQL by string
substitution.

Supported parameter values in the first implementation should include:

- `nothing` as SQL `NULL`
- boolean
- number
- string
- date/time values where mapping is unambiguous
- encoded JSON records and arrays when parameter type information permits it

Unsupported parameter types should raise an error before sending the query.
File values, directory values, connection values, functions, and other native
handles must not be converted implicitly.

Named parameters should be deferred. PostgreSQL itself uses positional
parameters, and a first implementation should not add a SQL rewriting layer.
A later helper could accept a record and rewrite documented placeholders, but
that feature must handle SQL strings, comments, casts, and repeated names
correctly before it is considered safe.

`pg.query` is intended for statements that return rows, including
`INSERT ... RETURNING`. A successful statement with no rows should return `[]`.
Protocol or SQL failures should raise runtime errors.

### Command API

Recommended forms:

```basic
result = pg.exec(db, sql)
result = pg.exec(db, sql, params)
```

`pg.exec` should return a record rather than only a number:

```basic
{
    command:"UPDATE",
    rows_affected:3
}
```

Recommended initial fields:

- `command`: PostgreSQL command tag category such as `"INSERT"` or `"UPDATE"`
- `rows_affected`: number when PostgreSQL reports a count, otherwise `nothing`

A record allows future non-breaking additions such as `oid`, notices, or a
complete command tag. Returning only the affected row count is attractive but
does not represent DDL and other commands well.

`pg.exec` should reject row-returning results and direct the caller to
`pg.query`. This catches accidental loss of `RETURNING` data. Conversely,
`pg.query` may accept an empty row result.

### Result Representation

`pg.query` should return an array of records:

```basic
rows = [
    {id:1, name:"Ada"},
    {id:2, name:"Grace"}
]
```

Each row should always be a record, including one-column results. This keeps
iteration and field access consistent:

```basic
for each user in rows
    print(user.name)
end for
```

Column names become record field names exactly as PostgreSQL reports them.
Queries should use aliases when application-friendly names are required.

Duplicate column names should be a runtime error in the first implementation:

```sql
select users.id, teams.id ...
```

must instead use:

```sql
select users.id as user_id, teams.id as team_id ...
```

Silently keeping the first or last value loses data. Automatically generating
names such as `id_2` is convenient but unstable when query columns change.
An alternative future result mode could return column metadata plus positional
row arrays for applications that intentionally need duplicate names.

Column order is not represented by a record and should not be treated as part
of the row API. Applications needing order should specify it separately or use
a future raw-result API.

## PostgreSQL Type Mapping

Type conversion should use PostgreSQL type OIDs, not textual guessing based on
contents.

Recommended initial result mapping:

| PostgreSQL type | gBASIC value | Notes |
| --- | --- | --- |
| SQL `NULL` | `nothing` | Database null is absence; `unknown` remains a language sentinel |
| `boolean` | boolean | Strict PostgreSQL boolean parsing |
| `smallint`, `integer` | number | Exactly representable in current numeric range |
| `real`, `double precision` | number | Includes normal floating-point limitations |
| `bigint` | string initially | Avoid silent loss beyond 53-bit integer precision |
| `numeric`, `decimal` | string initially | Preserve exact decimal text |
| `text`, `varchar`, `char`, `name`, `uuid` | string | UUID remains textual initially |
| `json`, `jsonb` | decoded gBASIC value | Records, arrays, strings, numbers, booleans, or `nothing` |
| `date` | date/time value | Use the existing date precision |
| `time` | date/time value | Use the existing time-only representation |
| `timestamp` | date/time value | No timezone conversion |
| `timestamptz` | string initially | Preserve offset/instant until timezone semantics are defined |
| `interval` | string initially | Existing duration cannot exactly represent every PostgreSQL interval rule |
| `bytea` | string or unsupported initially | Requires an explicit binary-value decision |
| arrays | unsupported initially | Add only with recursive element type mapping |
| enums | string | Preserve label |
| unknown/custom types | string | Include type metadata in diagnostics when conversion fails |

### Numeric Tradeoffs

The current gBASIC number is floating point. Mapping every `bigint` or
`numeric` to number would be convenient but can silently corrupt identifiers,
money, and exact decimal values. Returning strings is less ergonomic but safe.

A future exact decimal or integer runtime type would justify changing these
mappings in a major compatibility phase. Per-query coercion options are not
recommended initially because they make row types depend on configuration.

### JSON Tradeoffs

Decoding `json` and `jsonb` into normal gBASIC values makes rows natural and
fits the language's record/array emphasis. It does mean JSON `null` and SQL
`NULL` both become `nothing` after extraction. Applications that must preserve
that distinction need a future raw-text or metadata result mode.

Malformed JSON returned for a JSON-typed column should be treated as a runtime
error because it indicates protocol, server, or decoder inconsistency.

### Date/Time Tradeoffs

`date`, `time`, and timezone-free `timestamp` fit existing date/time values.
`timestamptz` should remain an ISO-style string until gBASIC defines timezone
and instant semantics. Converting it to local time implicitly would make
results machine-dependent.

## Transactions

The first implementation should provide explicit functions:

```basic
pg.begin(db)
pg.commit(db)
pg.rollback(db)
```

They should return `true` on success and raise runtime errors on failure.
Transaction state should remain PostgreSQL-controlled; the module may track
enough state to improve diagnostics but must not pretend a failed transaction
is usable.

Recommended use:

```basic
pg.begin(db)
pg.exec(db, "update accounts set balance = balance - $1 where id = $2", [25, 1])
pg.exec(db, "update accounts set balance = balance + $1 where id = $2", [25, 2])
pg.commit(db)
```

`with transaction(db) ... end with` is desirable later:

```basic
with transaction(db)
    ...
end with
```

Advantages:

- commits on normal completion
- rolls back on runtime error, return, stop, or other block exit
- reduces forgotten rollbacks

Disadvantages:

- requires parser, AST, evaluator, and cleanup semantics
- nested behavior must define savepoints versus rejection
- interaction with a frame-scoped `on error` handler is subtle

Therefore the block form should be deferred until explicit transaction
functions are stable. A later block should roll back on any unhandled error and
should reject nesting initially unless savepoint semantics are explicitly
designed.

## Error Handling

The recommended model is runtime errors, consistent with current gBASIC:

```basic
on error goto next
rows = pg.query(db, sql, params)

if error then
    print(error.message)
    print(error.source)
end if
```

Database failures should not be returned as ordinary row or command records.
Mixing success and error shapes forces every caller to inspect records manually
and makes ignored failures too easy.

Recommended error behavior:

- `error.source` is `"postgres"`
- `error.message` includes the primary PostgreSQL error and SQLSTATE when
  available
- the existing numeric `error.code` uses a stable gBASIC PostgreSQL/module code,
  not a lossy conversion of the five-character SQLSTATE
- passwords and full connection strings are redacted
- SQL text is not included by default because it may contain sensitive data

A future extension to structured error state could expose:

- `sqlstate`
- `severity`
- `detail`
- `hint`
- `schema`
- `table`
- `column`
- `constraint`

Until the language error record supports module-specific fields, these may be
included selectively in the message. A separate `pg.last_error(db)` record is
not recommended as the primary model because errors can occur before a
connection exists and because it duplicates global runtime error state.

## Prepared Statements

Prepared statements should be deferred to a later phase.

The first implementation should use libpq parameterized execution for every
parameterized `pg.query` and `pg.exec` call. This provides safe parameter
binding without exposing statement lifecycle.

Automatic named prepared-statement caching is not recommended initially:

- cache invalidation and schema changes are complex
- connection-local caches interact with reconnects and pooling
- unbounded distinct SQL can consume server resources
- automatic behavior is difficult to diagnose

Later explicit APIs may be:

```basic
statement = pg.prepare(db, name, sql)
rows = pg.query_prepared(statement, params)
result = pg.exec_prepared(statement, params)
pg.close(statement)
```

The prepared statement should be an opaque handle tied to its connection.
An unnamed one-shot prepared execution may be added internally without changing
the public API.

## Connection Pooling

Pooling should not be in the first implementation.

An initial opaque connection API is sufficient for command-line tools, desktop
applications, jobs, and moderate server workloads. Pooling introduces:

- checkout and release lifecycle
- transaction leakage prevention
- health checks and reconnect behavior
- maximum size and timeout policy
- concurrent access and thread-safety requirements

A future pool should be a separate opaque value:

```basic
pool = pg.pool(config)
db = pg.acquire(pool)
pg.release(pool, db)
```

It should not change `pg.query`, `pg.exec`, or transaction APIs, which should
continue to operate on a connection. An optional future block could safely
acquire and release a connection.

External poolers such as PgBouncer should remain fully usable with ordinary
`pg.connect`.

## PostgreSQL Features

The PostgreSQL-first design should leave room for:

- `RETURNING` through `pg.query`
- server notices
- `COPY`
- `LISTEN`/`NOTIFY`
- savepoints
- prepared statements
- PostgreSQL arrays
- UUID, range, enum, and custom type support
- binary parameters and results
- asynchronous queries
- cancellation

These should be added as focused APIs rather than by expanding every result
into a large abstraction object.

## Rejected Alternatives

### Connection as a Normal Record

Rejected because native ownership, cleanup, closed state, serialization, and
secret handling cannot be made reliable with an ordinary mutable record.

### SQL String Interpolation

Rejected because it is unsafe, type-losing, and incompatible with reliable
escaping. Parameters must use libpq binding.

### Query Returning a Result Wrapper by Default

Rejected for ordinary queries because `rows = pg.query(...)` should naturally
produce an array usable by `for each`. Metadata-heavy access can be a separate
future raw API.

### Rows as Positional Arrays

Rejected as the default because `row.name` and `row["name"]` are clearer than
numeric indexes. Positional rows remain a possible future raw-result mode.

### Silently Renaming Duplicate Columns

Rejected because generated names are unstable and can conceal query mistakes.
Require SQL aliases.

### Errors as Returned Records

Rejected as the primary mechanism because it conflicts with existing runtime
error handling and makes failures easy to ignore.

### Mapping All PostgreSQL Numbers to gBASIC Number

Rejected because `bigint` and exact `numeric` values can silently lose
precision.

### Generic Database API First

Rejected because it would prematurely flatten PostgreSQL-specific behavior and
delay a useful implementation. Compatibility should come from shared shapes
and naming conventions, not from limiting PostgreSQL.

## Future Compatibility

A later generic database layer could define a small common subset:

```basic
db.connect(config)
db.query(connection, sql, params)
db.exec(connection, sql, params)
db.close(connection)
```

It should be an adapter over backend modules, not the foundation required by
the PostgreSQL implementation.

Future backend modules can follow parallel conventions:

- `sqlite.connect`, `sqlite.query`, `sqlite.exec`
- `mysql.connect`, `mysql.query`, `mysql.exec`

Common conventions should include:

- opaque connection handles
- parameter arrays
- query results as arrays of records
- command results as records
- runtime errors
- explicit transactions

Backend-specific functions should remain in backend namespaces. PostgreSQL
features must not be hidden merely because SQLite or MySQL lacks an equivalent.

## Proposed Implementation Phases

### Phase 1: Core Synchronous Access - Complete

- module loading and libpq build integration
- opaque connection value
- `pg.connect`
- `pg.close`
- `pg.query`
- `pg.exec`
- positional parameter arrays
- scalar result conversion
- row arrays of records
- runtime error integration

### Phase 2: Transactions and Type Coverage - Complete

- `pg.begin`
- `pg.commit`
- `pg.rollback`
- date/time mapping
- JSON/JSONB decoding
- UUID and enum strings
- stronger SQLSTATE diagnostics

### Phase 3: Explicit Prepared Statements

- opaque prepared statement values
- prepare, query, execute, and close lifecycle
- invalidation and reconnect rules

### Phase 4: PostgreSQL-Specific Operations

- `COPY`
- `LISTEN`/`NOTIFY`
- cancellation
- notices
- savepoints
- PostgreSQL arrays and selected advanced types

### Phase 5: Pooling and Concurrency

- pool configuration
- acquire/release lifecycle
- transaction cleanup on release
- health checks and timeouts
- concurrency model

### Later Language Integration

- `with transaction(db)`
- safe connection acquisition blocks
- structured module-specific error fields
- asynchronous iteration or streaming results if the language gains the
  necessary control-flow/runtime support

## Open Questions

- Should JSON numbers that exceed gBASIC precision remain strings, and if so,
  how should the JSON decoder expose that distinction?
- When gBASIC gains exact integers or decimals, should PostgreSQL mappings
  change automatically or require an explicit compatibility version?
- Should `timestamptz` remain a string until an instant/timezone value exists,
  or should the module introduce a PostgreSQL-specific timestamp record?
- Should `bytea` be unsupported initially, represented as an encoded string, or
  motivate a native binary value?
- Should duplicate columns always be errors, or should a future raw mode expose
  positional values and column metadata?
- What transaction behavior should follow an absorbed error when a
  statement leaves PostgreSQL's transaction in the failed state?
- Should connection cleanup warnings be emitted when a live transaction is
  rolled back during `pg.close` or interpreter shutdown?

## Implemented Decisions

- The build detects libpq with `pkg-config`. gBASIC still builds without it,
  but `load pg` then raises a clear runtime error.
- PostgreSQL module errors use gBASIC error code `2001` and source
  `"postgres"`.
- `pg.exec` normalizes the command tag to its first word and returns
  `rows_affected` separately.
- `pg.query` returns `[]` for successful command statuses that contain no rows.
- Duplicate result column names are runtime errors.
- PostgreSQL arrays and `bytea` are runtime errors until native mappings are
  designed.
- Fractional seconds are reduced to the runtime's current second precision.
- Live integration tests use standard libpq `PG*` environment variables and
  are run with `GBASIC_POSTGRES_TEST=1 ./tests/run_postgres.sh`.
