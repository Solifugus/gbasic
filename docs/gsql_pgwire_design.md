# G-SQL speaking PostgreSQL's wire protocol

**Status:** Proposal, 2026-11-19. Nothing is built. Q1 ruled 2026-11-22
(§3 B, a front-end process). Q2, Q4 and Q6 ruled 2026-11-23 as proposed:
catalog queries answered by shape, money as `numeric` with the currency in
the column's comment, a duration as `interval`, a record as `jsonb`,
increments 1-3 for 0.14.0. Q3 and Q5 open. §2's capture is real: it was
run, and it can be run again (`research/pgwire/`). §8 holds the questions for
Matthew. This note follows `enterprise_data_review.md` §3, which priced the
protocol and recommended this note, starting from captured client traffic.
Matthew agreed to that order on 2026-11-18.

## 1. The goal

Today only gBASIC can reach a G-SQL database (`enterprise_data_review.md`, gap
G1). This note proposes that a G-SQL database also accept connections from
anything that speaks PostgreSQL's frontend/backend protocol, version 3:

- `psql`;
- libpq and every driver built on it, gBASIC's own `pg` module included;
- psycopg, JDBC, node-postgres and pgx;
- the ORMs and BI tools built on those drivers.

Those clients never learn they are talking to G-SQL. The protocol is the small
part of this. The enterprise review's guess was that the large part is the
catalog queries clients send **when they connect**. **The capture shows that
guess was half wrong (§2).**

## 2. What clients actually send: measured

`research/pgwire/proxy.py` sits between a client and a real PostgreSQL 17 and
logs every message. The client connects over TCP, and the proxy forwards to the
server's unix socket. `research/pgwire/capture.sh WORK` installs the drivers
into a scratch directory and runs each of them through the proxy. Each client:

- connects;
- runs a query with a parameter and one without;
- in the `_meta` runs, asks what a tool asks.

The anonymised logs are `research/pgwire/corpus/*.jsonl`, and
`research/pgwire/summarize.py` prints the table below from them. Captured
2026-11-19.

| client | statements | catalog queries | binary formats | PostgreSQL-only syntax |
|---|---|---|---|---|
| `psql`, a query | 1 | **0** | none | none |
| gBASIC `pg` (libpq) | 1 | **0** | none | none |
| psycopg2 | 4 | **0** | none | none |
| psycopg 3 | 5 | **0** | int parameters | none |
| node-postgres | 2 | **0** | none | none |
| pgx v5.7.1 (Go) | 2 | **0** | parameters; numeric (1700) and timestamp (1114) results | none |
| pgJDBC 42.7.4, queries | 2 | **0** | int parameters | none |
| pgJDBC, `DatabaseMetaData` | 14 | 11 | parameters | `::` casts, `::regclass`, arrays, regex `~`, set-returning functions |
| `psql` `\l \dn \dt \d \dv \di \df \du` | 19 | 19 | none | all of the above, plus `OPERATOR(pg_catalog.~)` |
| SQLAlchemy `inspect` | 46 | 17 | parameters | `::regtype`, `= ANY(array)`, arrays, `SHOW`, savepoints |
| pandas `read_sql` and `to_sql` | 22 | 4 | parameters | `::` casts, `SHOW`, savepoints, `DEALLOCATE ALL` |

**Finding 1: connecting costs nothing.** No driver sends a catalog query when it
connects or when it runs a query:

- every driver sends a startup packet;
- the server sends 14 `ParameterStatus` values, a `BackendKeyData` and
  `ReadyForQuery`;
- the client then sends the program's own SQL.

So an application that runs its own SQL through any of these drivers needs the
protocol and none of `pg_catalog`. That covers gBASIC programs using `pg`,
Python, Java, Go and Node services, and ETL jobs.

**Finding 2: the catalog is what tools need when they *describe* a database.**
Those are:

- `psql`'s `\d` commands;
- JDBC's `DatabaseMetaData`, which is how DBeaver, Metabase and most Java BI
  tools list tables and columns;
- SQLAlchemy's inspector, which pandas' `to_sql` also calls to ask whether a
  table exists.

Across the three, 27 catalog relations and 38 functions appear, among them
`pg_get_expr`, `format_type`, `pg_get_constraintdef` and `to_regtype`. They also
use syntax G-SQL does not have:

- `::` casts, `::regclass` above all;
- regex `~`;
- array values and `= ANY(array)`;
- `generate_series` and `unnest`;
- `OPERATOR(pg_catalog.~)`.

**Finding 3: the plain path still needs binary formats.**

- pgx describes each statement, then asks for numeric and timestamp results in
  binary.
- psycopg 3, JDBC and pgx send integer parameters in binary.

The protocol lets a client choose the format per column, and a server must
honour the choice. Text format alone would leave pgx unable to read a numeric
column.

**Finding 4: session statements.** SQLAlchemy and pandas send:

- `BEGIN`, `ROLLBACK` and `COMMIT`;
- `SHOW transaction isolation level`;
- `SHOW standard_conforming_strings`;
- `select pg_catalog.version()`;
- `select current_schema()`;
- `SAVEPOINT "_pg3_1"` and `RELEASE "_pg3_1"`;
- `DEALLOCATE ALL`.

That is a small, closed set.

**What was not measured:**

- npgsql (.NET), because no `dotnet` here;
- psqlODBC, which is what Power BI and Excel use;
- Metabase, DBeaver and Tableau themselves, as distinct from the JDBC calls
  they make;
- a BI tool's *generated* query dialect, such as `date_trunc`, `extract` and
  schema-qualified quoted names like `"public"."invoice"`.

Each driver was captured at one version, once. Increment 4 (§7) starts by
capturing Metabase, the cheapest real BI tool to run.

## 3. Where it runs: a front-end process

There are three places a PostgreSQL listener could live:

- **(A) In the owner.** The owner would also listen on TCP and decode
  PostgreSQL messages itself. There is no extra hop, but the owner is
  single-threaded, and it would gain TLS, SCRAM, a second protocol's parser
  and a listener that must keep it alive. A defect in any of those takes down
  every client of the database, gBASIC's own included.
- **(B) A front-end process, `gbasic --gsql-pg`.** It is an ordinary client of
  the owner (`src/gsql/client.c`), with one owner connection per PostgreSQL
  connection, and it translates between the two protocols. The owner is
  untouched. While it listens it holds a connection, so the owner's linger
  never stops it. A crash loses that process's connections and nothing else.
  It costs one unix-socket hop: G-SQL's own `select $1` round trip is 12 µs
  (`gsql_sql_design.md`), against a network client's milliseconds.
- **(C) The same front end written in gBASIC over `net` and the `gsql`
  module.** That is attractive as dogfooding, but every value of every row
  would be converted by the interpreter, and a `gsql` call blocks the event
  loop that every other connection waits on.

**Recommended: (B).** Parallel readers carry over unchanged. A long read from a
PostgreSQL client is an autocommit read like any other, and the owner forks it
a reader (`gsql_parallel_readers_design.md`). A PostgreSQL `CancelRequest`
arrives on a new connection carrying the process id and secret key from
`BackendKeyData`. It maps onto the owner's existing `CANCEL`.

```
psql ─┐                      ┌─ owner ─ forked readers
pgx  ─┼─ gbasic --gsql-pg ───┤  (unix socket, G-SQL's own frames)
JDBC ─┘   (TCP / unix)       └─ gBASIC programs (gsql module)
```

## 4. Values: G-SQL kinds to PostgreSQL types

`RowDescription` gives each column a type OID, and drivers decode by it. A
G-SQL column has one kind, so it has one OID:

| G-SQL kind | PostgreSQL type (OID) | text form sent | notes |
|---|---|---|---|
| `integer` | `int8` (20) | decimal | G-SQL integers are 64-bit |
| `number` | `numeric` (1700) | shortest decimal that round-trips | an exact decimal stays exact; a double is sent as its shortest decimal; NaN and infinity as `NaN` and `Infinity` (numeric holds them since PostgreSQL 14) |
| `string` | `text` (25) | UTF-8 | a value holding NUL or invalid UTF-8 cannot be `text`: the row is refused with an error naming the column, never truncated |
| `boolean` | `bool` (16) | `t` / `f` | |
| `date` | `date` (1082) | `YYYY-MM-DD` | |
| `time` | `time` (1083) | `HH:MM:SS` | |
| `datetime` | `timestamptz` (1184) | `YYYY-MM-DD HH:MM:SS+00` | every gBASIC instant is UTC (`utc-is-the-default`), and `TimeZone` is reported as `UTC` |
| `duration` | `interval` (1186) | `IntervalStyle postgres` | §8 Q4: a duration keeps its fields as given, and so does an interval |
| `money(CUR)` | `numeric` (1700), currency in the column's comment | the amount | §8 Q4: the currency is part of the column's type, so nothing is lost; the alternative is text such as `USD 1.50` |
| `record` | `jsonb` (3802) | JSON | §8 Q4: only a record `json_encode` accepts; one holding a date or money is refused, as `json_encode` refuses it |
| any kind, `unknown` | — | SQL `NULL` | |

**Binary formats,** for exactly the types §2 saw requested:

- `int2`, `int4` and `int8`;
- `numeric`;
- `timestamp` and `timestamptz`;
- `date`;
- `bool`;
- `float8`;
- `text`.

Any other binary request is refused, naming the type, never answered wrongly.

**Parameters** arrive either typed by an OID or untyped as text.
G-SQL already reads text as the column's kind (a `date` column compares a text
parameter as a date: `number-text-comparison`), so untyped text needs nothing
new. A binary `int8` parameter becomes an exact integer.

**Errors** carry SQLSTATE codes, because drivers branch on them:

- `23505`, a unique key;
- `23503`, a foreign key;
- `23514`, a check;
- `42P01`, no such table;
- `42703`, no such column;
- `42601`, a syntax error;
- `57014`, a cancel;
- `22P02`, text a column cannot read;
- `XX000` for anything else.

Every G-SQL refusal keeps its own sentence as the message, and the statement it
came from as the detail.

## 5. What G-SQL's SQL already has, and what it lacks

G-SQL's dialect is "standard SQL, PostgreSQL's choice where the standard is
silent" (`gsql_sql_design.md`). Checked in the source on 2026-11-19, it already
accepts:

- `$1` parameters;
- PostgreSQL's type names `bigint`, `int`, `varchar`, `text`, `numeric` and
  `timestamp` in DDL, which is what pandas' `to_sql` writes;
- `RETURNING`;
- `ON CONFLICT`;
- window functions;
- `WITH RECURSIVE`.

That is why §2's application statements would run as sent.

**It lacks:**

- schemas. Clients qualify names with `public.` and ask `current_schema()`.
  Proposed: one schema, `public`, accepted as a qualifier and answered by
  `current_schema()`;
- `::` casts. Clients write `$1::BIGINT`, and pandas does exactly that. This is
  a parser addition with the same meaning as `CAST`;
- named savepoints, `SAVEPOINT x` and `RELEASE x`. G-SQL has savepoints as
  nested `begin`, so these become a stack check: a name must match the newest
  savepoint, or be refused;
- `SHOW`, `SET` and `version()` for the settings in §2's list. These are
  answered by the front end, never sent to the owner.

## 6. Who may connect

G-SQL trusts its unix socket today: the database directory's permissions are
the access control. A network listener changes that.

- **The first increments listen on a unix socket and on loopback only, without
  passwords.** For the socket, the peer's uid must be the owner's, read with
  `SO_PEERCRED` (as `net` already does). For loopback, the same rule as the
  webserver's default bind applies.
- **Any other address requires TLS and SCRAM-SHA-256,** and listening there
  without them is refused at start. SCRAM's pieces (PBKDF2, HMAC and SHA-256)
  are in `crypto`. TLS is `net`'s. Users and their salted verifiers live in a
  file in the database directory. §8 Q5 asks whether that is the right home.

## 7. Increments

Each increment has a test held to an oracle outside G-SQL:

1. **Simple protocol.** This covers:
   - startup;
   - `ParameterStatus`;
   - simple `Query`;
   - text format;
   - `RowDescription` OIDs per §4;
   - errors with SQLSTATE;
   - `Terminate`;
   - `CancelRequest`.

   Oracles:
   - **Differential:** the same statements run against real PostgreSQL and
     against the front end, compared as the client sees them (`psql -A`
     output, and gBASIC's `pg` reading both). This is opt-in, like
     `run_postgres.sh`.
   - **A hand-written byte tier** that always runs (a python3 socket client).
2. **Extended protocol.** This covers:
   - `Parse`, `Bind`, `Describe` and `Execute`;
   - `Sync`;
   - `ParameterDescription`;
   - portals with a row limit, which map onto G-SQL cursors;
   - the binary formats of §4.

   Oracle: **replay of §2's corpus.** The recorded frontend messages are sent
   again to the front end, and the backend message types that come back must
   match the recording. This needs only python3, no drivers. Then each driver
   in turn, where installed.
3. **Session statements.** This covers:
   - §2's finding 4;
   - `::` casts;
   - `public.`;
   - named savepoints.

   Oracle: the SQLAlchemy and pandas corpora replayed, and pandas
   `to_sql`/`read_sql` round trips held to the frame written.
4. **Describing a database:** §8 Q2's choice. It starts by capturing Metabase
   and psqlODBC. Oracle: every catalog query in the corpus answered, and each
   answer held to what real PostgreSQL answers for the same tables created
   there.
5. **The network:** TLS, SCRAM-SHA-256, users, and listening beyond loopback.
   Oracle: `psql` with `sslmode=verify-full`, wrong passwords refused, and
   libpq's own SCRAM.

`COPY FROM STDIN` and `COPY TO STDOUT` would follow if a tool needs them. They
are how other tools bulk-load, and G-SQL's `insert` many rows already has the
shape.

## 8. Questions for Matthew

- **Q1. A front-end process (§3 B),** `gbasic --gsql-pg DIR [--listen ...]`,
  rather than the owner listening itself? Recommended. **Ruled 2026-11-22:
  (B), the front-end process.**
- **Q2. How to answer the catalog (increment 4).** There are two routes:
  - (a) **By shape.** The front end recognises each catalog query it has seen,
    with literals stripped, and answers it from G-SQL's catalog. An unknown one
    is refused, naming itself, and logged for the corpus. That is 43 distinct
    shapes in today's corpus across psql, JDBC, SQLAlchemy and pandas. It is small and testable,
    but each new client version may add shapes.
  - (b) **A real `pg_catalog`:** virtual tables, the 38 functions, arrays,
    `::regclass` and regex `~` added to G-SQL's SQL. It is general, but arrays
    alone are a new value kind in SQL.

  I lean toward (a), switching to (b) if the table of shapes passes a few
  hundred. **Ruled 2026-11-23: (a).**
- **Q3. What version to claim.** Drivers read `server_version`, and SQLAlchemy
  parses `version()`. I propose `16.0 (G-SQL 0.x)`: a real PostgreSQL major, so
  drivers pick their modern paths, with G-SQL named after it.
- **Q4. `money`, `duration` and `record` (§4).** Should money be `numeric` with
  the currency in the column's comment, or text? Should a duration be
  `interval`? Should a record be `jsonb`, refused when it cannot be JSON?
  **Ruled 2026-11-23: yes to all three** (Matthew: "those are good ideas").
- **Q5. Users.** A file of SCRAM verifiers in the database directory, managed
  by a `gsql` verb? Or users as G-SQL rows?
- **Q6. Where it sits.** 0.13.0 already holds the terminal layer, the TUI,
  whisker and parallel readers. I propose increments 1-3 for 0.14.0.
  **Ruled 2026-11-23: 0.14.0.**

## 9. Files

- `research/pgwire/proxy.py`: the logging proxy.
- `research/pgwire/capture.sh`: re-runs the capture.
- `research/pgwire/clients/`: one program per client.
- `research/pgwire/corpus/`: what was sent.
- `research/pgwire/summarize.py`: §2's table.

Research code, not run by the gate.
