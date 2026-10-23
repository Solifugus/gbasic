# G-SQL: Native Relational Database for gBASIC

**Status:** Proposal — architecture v0.4. Agreed direction, proposals and open decisions are distinguished throughout; **nothing of G-SQL exists.**
**Original date:** 2026-10-17 · **v0.2:** 2026-10-20 · **v0.3 / v0.4:** 2026-10-21
**Project:** [gBASIC](https://github.com/Solifugus/gbasic)
**Scope:** Native embedded relational engine, pre-G-SQL core contracts, temporal history, scoped serials, relationships, dynamic SQL, shared expression semantics, statistical integration, and windowed iteration

**This file supersedes `gbasic_gsql_design.md`, which is retired.** That document was checked line by line against this one before deletion (v0.3): of its 233 non-blank lines, 219 appear here verbatim, and the 14 that do not are the old status/date/scope header, three phase paragraphs extended here rather than dropped, and the two positions v0.3 **reverses with reasons** — a new `integer` type (§3.2) and `NULL` as a gBASIC alias (§3.1). Nothing else was lost. The filename carries no version number on purpose — the status line above is where the version lives, so a revision does not orphan the index row or anybody's link.

## 0. What is already settled in the tree (v0.3)

Everything below this heading was **measured against the repository**, not assumed, because v0.2 carried prerequisites the tree had already decided the other way. §16 records how each number was taken, so it can be re-taken.

| v0.2 said | the tree says | consequence |
|---|---|---|
| Add a signed `INT64` **type** to gBASIC (§3.2, §10A, §12.5, §13.1, §14) | Exactness is already a property of **`number`**; a second numeric type was **deliberately rejected** | §3.2 rewritten. The prerequisite is not a language change |
| — | `serialize`/`deserialize` **loses** exactness; `string`, `encode` and `decode` keep it | The real prerequisite, and it is one open increment, not a new type |
| `NULL` as an alias for `unknown` "ideally in both SQL and ordinary gBASIC" | `null` is a **free identifier** today, and `unknown` already carries the meaning | **Ruled:** SQL accepts both spellings, gBASIC keeps `unknown` alone (§3.1) |
| Surface syntax "subject to parser review" (§2) | Both block heads measured at **zero** bison conflicts, reserving **no** words | §2 priced. G-SQL needs no reserved word |
| `window(before, after)` on the `for each` head (§10B) | **1 shift/reduce** — the slot after the source expression is already taken, twice (a duration unit, and `new T with {…}`) — and the grammar is the *smaller* problem: `tran[-1]` redefines indexing, and out-of-window answering `nothing` reverses a promoted raise | §10B rewritten: an ordinary builtin, `around(source, before, after)`, zero grammar |
| "settle whether a dedicated binary value is needed" (§10A) | PLAT-NUL's door sweep already answered it **per door** | No new value kind; the work is `bytea`/BLOB binding |
| Capability checks as future work (§10A) | `has_builtin` answers per build, `gbasic --capabilities` exists | One decision left, and it is already named in the tree |
| Version metadata "not independent timestamps alone" (§10D) | gBASIC's clock is **second-resolution by design** | The instinct was right; now it has a reason |
| *(v0.4)* `unknown` truth tables identical to SQL `NULL` | **They are not, today**: gBASIC raises on `not unknown`, `true and unknown` and `unknown + 1`, and answers `unknown = unknown` **true** | Agreed to make them identical; a language change with its own increment before G-SQL (§3.1.1) |
| *(v0.4)* "one writer, multiple readers", with no word about processes | A `server` block with `workers: N` is N **OS processes**, and every actor is a process | **Decided:** one owning process per database file, which every other process reaches as a client (§6.1) |

## 1. Purpose and design philosophy

G-SQL replaces gBASIC's dependence on SQLite for **native embedded storage**, not its existing ability to connect to PostgreSQL, SQL Server, and other databases via ODBC. Its distinctive purpose is to eliminate conceptual conversion between a SQL database's types and gBASIC's values.

**Guiding principle:** G-SQL values are gBASIC values; SQL declarations constrain which values may be persisted. The database adds relational storage, indexes, query execution, transactionality, and optimization rather than a competing application-language type system.

Design priorities, in order:
1. Correctness, durability, predictable semantics, and clear business logic.
2. Familiar relational SQL for operations best performed near stored data.
3. Native interoperability with gBASIC's money, temporal, statistical, and other stable value types.
4. Efficient indexed access and scalable streaming execution.
5. Incremental implementation without foreclosing later concurrency, spill-to-disk, partitioning, or more advanced optimization.

Avoid duplicating algorithms and business logic already well expressed in gBASIC. SQL is best at selecting, relating, filtering, sorting, and aggregating; gBASIC at procedural interpretation, workflows, business rules, presentation, and sophisticated modeling.

**Complexity rule (agreed):** Accept flexibility or extensibility beyond the targeted capabilities **only when it does not introduce additional complexity**. In evaluating a proposal, consider implementation, semantics, maintenance, documentation, and the cognitive cost to programmers, not just syntax. Prefer a small reusable mechanism to speculative configuration, abstraction, or generality. Keep advanced capabilities optional and ordinary cases straightforward.

**Status vocabulary:** **Agreed direction** = design intent accepted, not necessarily implemented; **Proposed** = promising mechanism awaiting validation; **Open** = requires decision/testing. Illustrative code throughout is not guaranteed current gBASIC syntax.

**Scope rule (agreed, v0.4):** G-SQL contains **only what cannot be done as well or better in gBASIC itself.** It is an embedded engine, not a database management system; a fuller system can be built *in gBASIC* on top of it afterwards. The test for each candidate is whether a gBASIC program over the engine's interface could do it equally well:

| in the engine: needs knowledge only the engine has | in gBASIC: the developer's, or a library's |
|---|---|
| durability, crash recovery, page checksums and integrity checking | access control, roles and authorization policy (`principal()` already exists to build them on) |
| a **consistent online backup** (only the engine knows a consistent point while writes continue, §6.3) | an admin shell, inspection and reporting tools |
| encryption **at the page level** (§6.4) | import/export: CSV, xlsx and dumps |
| transactions, isolation and snapshots | migration tooling and deployment of schema changes |
| indexes, the planner and `EXPLAIN` | scheduling, retention jobs and archiving policy |
| the schema catalog, and a schema version a migration tool can read and set | column-level encryption of selected fields (the `crypto` library; such columns are not indexable) |

**Non-goals:** Do not recreate every PostgreSQL/SQL Server feature, build a second statistical library inside SQL, add general-purpose unrestricted `eval`, make all ecosystem improvements prerequisites, force business workflows into SQL, or introduce frameworks solely for hypothetical future use.

## 2. Proposed gBASIC surface

Syntax is illustrative; both heads have been priced (§2.1).

```basic
database business(file: "business.gsql")

sql transactions on business
    SELECT account_id, amount, posted
    FROM transactions
    WHERE account_id = :account_id
    ORDER BY posted;
end

for txn in transactions
    print txn.amount
next
```

- `:name` binds a typed gBASIC value; never interpolate SQL strings to bind data.
- A single `SELECT` returns a lazy, per-execution row iterator.
- A single DML statement returns a structured execution result (affected rows, etc.).
- Multiple statements need defined result behavior; proposed default is a per-statement result collection, with an explicit SQL-block `RETURN` to select one result and terminate the block.
- Re-executing a block, including from a gBASIC function, rebinds its parameters and obtains a new iterator. Prepared plans may be cached and invalidated on schema change.
- Provide file-backed and in-memory databases; default database selection should be explicit and documented.
- Existing external database adapters remain supported; native G-SQL extensions must not be silently passed through to external SQL dialects.

### 2.1 Both heads are priced, and they reserve nothing (measured, v0.3)

This project prices grammar in **bison conflicts** before designing around it — it rejected `IDENT expression` as a statement form over four measured conflicts, and the standing requirement is **zero**. Both heads above were measured against `src/parser.y`:

| head | technique | conflicts | occurrences of the shape in the tree |
|---|---|---|---|
| `database business(file: "…")` | lexer trigger on the exact three-token shape `database IDENT (` | **0** | **0** |
| `sql transactions on business … end` | lexer trigger on `sql IDENT on IDENT` | **0** | **0** |

**So G-SQL's surface costs no reserved words.** `database` appears 102 times and `sql` 119 times in the tree as ordinary words, and all of them stay legal, because the trigger fires only on the exact shape. That is the technique the `window` declaration already uses (`WINDOW_OPEN`), chosen there for the same reason and measured the same way: the lexer may look as far ahead as it likes, where LALR(1) cannot.

The `sql` body is priced as **one opaque span** delivered by the lexer, which is how a modifier's `(...)` clause was once read. That keeps SQL's own grammar out of the gBASIC grammar entirely — SQL is parsed by G-SQL's parser, which §10G requires anyway, so the two never compete for tokens and SQL keywords are never reserved in gBASIC.

## 3. Type system and persistence semantics

| gBASIC value | G-SQL declaration | Semantics |
|---|---|---|
| `number` | `NUMBER` | Native binary64 number; no implicit decimal-exactness claim |
| `number` carrying an **exact** integer | `INT64` / `INTEGER` alias | **Already shipped**, and not a second type: exactness is a property of `number` (§3.2). `INT64` and `INTEGER` are SQL *spellings* that constrain a column to exact integers; `type()` still answers `number` |
| `string` | `STRING` | Native Unicode string; explicit collation and ordering rules |
| `boolean` | `BOOLEAN` | Native boolean |
| `money` | `MONEY` / `MONEY(USD)` | Native tagged exact money; currency-constrained variant; retain guard digits and existing rounding/overflow rules |
| temporal values | `DATE`, `TIME`, `DATETIME` (exact names TBD) | Reuse existing gBASIC temporal representation and precision semantics |
| `unknown` | `UNKNOWN` / `NULL` | Exactly one missing-value sentinel with two spellings |
| `nothing` | No persisted SQL type | On parameter binding as a SQL value, normalize to `unknown`; never return `nothing` for a stored NULL |

### 3.1 Null and absence

**Ruled in v0.3: `NULL` is a SQL spelling and gBASIC keeps `unknown` alone.** v0.2 proposed aliasing it "ideally in both SQL and ordinary gBASIC", and the language half is now declined rather than deferred, for three reasons:

- `unknown` already carries the meaning, and a second spelling for one value is vocabulary spent on nothing. In SQL the word is not ours to choose — `NULL` is what every SQL author types, and `UNKNOWN` is what SQL itself calls the third truth value — so accepting both there costs nothing and buys familiarity.
- **`null` is a free identifier today** (measured: `null = 5` runs), so reserving it is not a neutral addition — it would break any program that used the name. The 48 reserved words are a list this project defends; `NULL` inside a SQL block is recognised by G-SQL's own parser, which reserves nothing in gBASIC (§2.1).
- The semantic claim is unaffected: **`unknown` *is* the SQL `NULL` value**. One value, two contexts, each with the spelling its readers expect. What must be specified is the three-valued logic (below), and that is unchanged.

- An explicitly bound `nothing` becomes `unknown`/`NULL`; a `NOT NULL` constraint rejects it.
- Omitting a column in `INSERT` invokes its declared default if present. Explicitly binding `nothing` **does not** invoke the default; it stores NULL or fails the constraint.
- `IS NULL`/`IS UNKNOWN`, joins, grouping and aggregates follow SQL exactly (§3.1.1), specified with tests.
- Statistical reductions should preserve their documented missing-data policies; SQL aggregates should document how they ignore or propagate `unknown`.

### 3.1.1 Truth tables: identical to SQL, which gBASIC is not yet (agreed v0.4)

**Agreed direction:** `unknown` was modelled on SQL `NULL`, and its truth tables should be **identical** to SQL's. Measured today (§16), they are not:

| expression | SQL | gBASIC today |
|---|---|---|
| `not unknown` | NULL | raises *"unknown cannot be used as a condition"* |
| `true and unknown` | NULL | raises |
| `false and unknown` | FALSE | false |
| `unknown and false` | FALSE | **raises**: only the left operand short-circuits, so `and` is not commutative |
| `true or unknown` | TRUE | true |
| `unknown or true` | TRUE | **raises** |
| `false or unknown` | NULL | raises |
| `unknown = 1` | NULL | **false** |
| `unknown = unknown` | NULL | **true** |
| `unknown != 1` | NULL | true |
| `unknown > 1` | NULL | raises *"unknown and nothing support only = and !="* |
| `unknown + 1` | NULL | raises *"unknown cannot be used in arithmetic"* |

So this is a **language change**, and it is its own increment before G-SQL: G-SQL's SQL and gBASIC's expressions must agree, and §10H shares one expression core between them. What it involves:

- **Kleene logic for `not`, `and`, `or`**, commutative, with short-circuit only where an operand *determines* the answer. `stdlib/rules.bas` already implements exactly this (its `run_rules.sh` tier asserts the right operand is never evaluated when the left decides), which makes it the reference.
- **Comparison and arithmetic propagate `unknown`** where they raise today.
- **SQL has two equalities, and gBASIC needs both.** SQL's `=` follows three-valued logic, but `GROUP BY`, `DISTINCT`, `UNION` and `IN`-list deduplication treat two NULLs as **not distinct**. gBASIC's `contains`, `find`, `remove_value`, `unique`, `sort`, record and array comparison, and the watcher change test are all "is this the same value" questions, and they must use the second, or `contains([unknown], unknown)` stops being true. PLAT-EQ already routes all of them through one comparison, which is the one place this split has to be made. **NaN needs the same treatment** (measured 2026-10-22): every comparison with it is false, so `sort` puts it wherever the input had it and `unique` keeps two of it. The "not distinct" equality should place NaN after +inf and equal to itself, PostgreSQL's rule, which is also its position in G-SQL's index keys (`gsql_storage_design.md` §3.2). One language increment covers both.
- **The cost in this tree is small, measured:** about 10 places compare with `= unknown`, against 928 uses of `is_unknown(...)`, the idiom that keeps meaning what it means. Every changed site raises at an `if` rather than answering differently, if the open question below is answered "raise".
- **Decided (v0.4): `if unknown` keeps raising.** SQL has no `if`; `WHERE` and `CASE WHEN` treat NULL as *not true* and silently drop the row. gBASIC raises, which is an anti-silence rule this project has defended repeatedly, and that stays in procedural control flow (`if`, `while`, `until`, an inline `if`). Inside a `sql` block, SQL is followed exactly. That is the one deliberate seam, and it is a boundary of context, not of value: the same `unknown`, a different consumer of the truth value.

### 3.2 Exact integers: already decided, and the prerequisite is smaller than v0.2 thought

**v0.2 asked for a new `integer` type. The tree already took the opposite decision and shipped it**, and the rejection was load-bearing rather than a cost saving: two numeric kinds require a **promotion matrix**, and a wrong promotion rule is a silent wrong answer in exactly the class the work was closing. So exactness became a property of `number` — Scheme's model and Lua 5.3's — `type()` still answers `number`, and there is no 28th value kind. See `docs/exact_number_design.md` and `tests/run_exact.sh`.

Measured (v0.3), with `b = number("9007199254740993")`, which is 2^53 + 1 and the smallest integer a double cannot hold:

| expression | answer | |
|---|---|---|
| `type(b)` | `number` | no second type |
| `b + 1` | `9007199254740994` | exact |
| `b * 2 + 1` | `18014398509481987` | exact |
| `mod(b, 2)` | `1` | a parity test is right; a double answers 0 |
| `b = 9007199254740992` | `false` | comparison moved into the integer domain, not the double's |
| `max([b, 1])` | `9007199254740993` | picks rather than recomputing |
| `pow(2, 62)` | `4611686018427387904` | exact for exact arguments |

So **"primary keys and indexes compare full 64-bit values exactly" is already true of the language**, and `INT64`/`INTEGER` become column constraints rather than a new type. `INTEGER` is also a free identifier today, so the spelling costs nothing.

**What is genuinely missing is one door, and it is the one a database needs.** Measured with the same value:

| route | round trip | |
|---|---|---|
| `string(b)` | exact | |
| `encode(b)` → `decode` | exact | the JSON route |
| `serialize(b)` → `deserialize` | **loses it** — `+ 1` gives `…992` | the **binary** route |

That is `exact_number_design.md`'s **increment 4**, recorded as the only one still open, and it covers both the binary serializer and **the actor wire**. It is therefore the real pre-G-SQL item: a durable row format and a worker pool both cross it. Finishing one increment on an existing serializer is a different and much smaller commitment than adding a numeric type to the language.

- Still to specify, and unchanged from v0.2: division and mixed exact/inexact arithmetic at the boundaries, and the overflow policy. Overflow **degrades with warning 2112** today rather than refusing — deliberately, so a program that answers keeps answering — and a database **must not** inherit that for a key column: a silently inexact key is the one place the language's chosen default is the wrong one, and G-SQL should refuse rather than degrade. That difference is a decision this document is making, not a restatement.
- A column declared `INT64` must reject a value that is not an exact integer, rather than rounding it. The language's degradation is a convenience for arithmetic; a stored key is a contract.

### 3.3 Money

- Reuse the native gBASIC money representation and arithmetic, including currency metadata and precision; never route monetary storage or aggregation through binary64 without an explicit, documented conversion.
- `MONEY(USD)` enforces currency on writes; `MONEY` accepts native currency-tagged money values.
- Currency-incompatible comparisons or aggregations must be rejected or explicitly converted with a dated rate; no implicit FX conversion.
- Define return units/types for `AVG`, `STDDEV`, `VARIANCE`, and other money-related statistics. Variance has squared monetary units and cannot naively return `MONEY`.

### 3.4 Persistence subset

Persist stable, serializable value kinds, not executable/process-bound values such as functions, watchers, handles, or actors. Disk representation must be versioned and independent of process pointers and in-memory layout.

### 3.5 Record-valued columns (decided 2026-10-22, first release)

**A column may hold a gBASIC record**, declared `RECORD`, and its fields may hold any persistable value, arrays and nested records included. This was a "future extension" until WorkSplicer's asset types needed it: each type of asset carries its own parameters, and the usual answers each fail one way. Attribute-per-row (EAV) tables are flexible, slow and make every value text. A table per type is fast and typed, but every new type is a schema change and generic code has to know every table.

**It is PostgreSQL's `jsonb` with gBASIC's types.** That is what an application would use on PostgreSQL, and G-SQL can do one thing `jsonb` cannot: the fields keep their real kinds, so money stays exact money and a date stays a date, with no conversion in or out (§1's guiding principle). A whole record is read with its row, in one fetch.

- **Field indexes.** An index may be declared on one field of a record column, e.g. the `vendor` field of `task_assets.params`. That is a narrow case of the expression indexes deferred in §5.2, and a safe one: extracting a field is a fixed rule of the engine, not a library function whose version could change under the index. A record without the field is indexed as an absence. Indexes on arbitrary expressions stay deferred.
- **Without a field index, a query on a field is a residual filter**, a scan of the rows the other predicates leave, which `EXPLAIN` reports as such.
- **Validation against a type's declared fields belongs to the application** (WorkSplicer's write path, against `asset_types`), under the scope rule: it is a check across two tables, and G-SQL's engine holds only what gBASIC cannot do as well. Constraints the engine itself enforces on a record column (`NOT NULL`, the persistable-value rule) are unchanged.
- **Cost to measure:** each row repeats its field names, as `jsonb` does. If that proves significant, the engine can store field names once per column; the storage measurements decide (`gsql_storage_design.md` §3.3).
- **Open: the field-access syntax in SQL.** `params.vendor` is ambiguous in SQL, where `a.b` means table `a`, column `b`; PostgreSQL resolves it with `(params).vendor` or with `->`. G-SQL needs one spelling, priced against the SQL grammar before it is chosen.
- **Prerequisite:** records cross the same binary serializer as exact integers (§3.2), so `exact_number_design.md`'s increment 4 covers both.

## 4. SQL functionality

### 4.1 Initial relational foundation

- `CREATE TABLE`, `DROP TABLE`, `ALTER TABLE ADD COLUMN` (further migrations later).
- Primary keys, unique constraints, `NOT NULL`, defaults, composite B-tree indexes; foreign keys and check constraints should be designed early, with delivery staged explicitly.
- `SELECT`, `INSERT`, `UPDATE`, `DELETE`.
- `WHERE`, projection, expressions, `ORDER BY`, `LIMIT`/offset (exact syntax TBD).
- `INNER JOIN`, `LEFT JOIN`, `CROSS JOIN`; subqueries.
- `UNION` and `UNION ALL`.
- `GROUP BY`, `HAVING`, `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`.
- Nonrecursive CTEs, ideally as syntactic query composition.
- Views: named relational definitions that can be expanded into and optimized with outer queries, not merely callable procedures returning iterators.
- Transactions and recovery; see §6.

### 4.2 Analytic SQL in stages

- Early: `ROW_NUMBER`, `LAG`, `LEAD`, running `SUM`/`COUNT` over `PARTITION BY` and `ORDER BY`.
- Follow-on: `RANK`, `DENSE_RANK`, rolling windows, explicit frames, `FIRST_VALUE`/`LAST_VALUE`.
- Follow-on: `ROLLUP` and `GROUPING()` for subtotal/grand-total reporting.
- Later: generalized `GROUPING SETS`, `CUBE`, recursive CTEs.
- Defer dedicated `PIVOT`/`UNPIVOT` unless actual workloads show a clear benefit over conditional aggregation or gBASIC/frame reshaping.

**Admission test for new SQL features:** Do they materially improve data locality, optimization, or relational composability, or make the declarative intent meaningfully clearer? If not, favor an ordinary gBASIC operation.

## 5. Query execution and optimization

### 5.1 Initial engine

- Parser → validated typed AST → logical relational plan → physical execution plan.
- Index seek/range scan and table scan; predicate pushdown; projection pruning; constant folding; prepared plan caching with schema-version invalidation.
- Nested-loop/index joins initially where suitable; hash join and hash aggregation for scale.
- Iterator/pipeline-based execution so large result sets need not be materialized wholesale.
- External sort and spillable hash aggregation/join are required on the path to large datasets; define memory limits and temporary-file lifecycle early.
- Basic planner statistics (row counts, selectivity estimates, index statistics) before attempting sophisticated cost-based planning.
- `EXPLAIN` in the first usable release; `EXPLAIN ANALYZE` later.

### 5.2 SARGability and gBASIC functions

Separate **index access predicates** from **residual filters**. A non-SARGable statistical function must not prevent an independent indexed predicate from narrowing the candidate rows.

```sql
SELECT *
FROM transactions
WHERE account_id = :account_id
  AND stats.normal_cdf(risk_score, 0, 1) > 0.95;
```

Expected plan shape: index seek on `account_id`, then residual evaluation of `stats.normal_cdf(...)`.

- Pure/deterministic scalar functions may be invoked in SQL expressions if explicitly approved for SQL execution.
- Function purity is **not** equivalent to SARGability.
- A deterministic function over constants can be folded; a function over a column is normally a residual predicate.
- Algebraic/monotonic rewrites (e.g. a valid inverse-CDF bound) require function-specific metadata, domain checks, correct handling of floating-point boundaries and `unknown`, and proofs/tests. Defer.
- Expression indexes are later work: function versions and library changes must not silently corrupt index meaning. **The exception is an index on one field of a record column** (§3.5), which is in the first release: extracting a field is a fixed rule of the engine, so nothing can change its meaning under the index.
- Show residual predicates and index choices in `EXPLAIN`; never imply an index was used when it was not.

## 6. Transactions, concurrency, and durability

These are non-negotiable architectural requirements even if the first implementation is modest.

- ACID transactions with explicit `BEGIN`, `COMMIT`, `ROLLBACK` semantics and well-defined SQL-block behavior on error.
- Crash-safe recovery, checksums on every page, atomic commit, and startup recovery, **proved by killing processes rather than by reasoning** (§6.2).
- Explicit `fsync`/durability policy and tests for process termination during commits.
- Isolation: start with snapshot reads and serialized writes, which the process model makes natural (§6.1). Do not claim more than is implemented and tested.
- Consider savepoints later; define busy/timeout/error semantics.
- Treat database files as a versioned format with migrations, integrity checks, backups, and recoverability.

### 6.1 One owning process per database (decided, v0.4)

**Decision:** a database file is owned by **exactly one process**, which runs the engine. Every other process that opens the file, including every web worker, every actor and an unrelated script started later, is a **client** sending requests to the owner. Nothing but the owner reads or writes the file.

**Why this fits gBASIC rather than merely being simpler:**
- gBASIC already runs as several processes as a matter of course (`workers: N`, actors). File-level locking shared between many processes is the hardest and least portable part of an embedded database, and this removes it: there is one writer by construction.
- Snapshot reads are natural. A client streaming a large result holds a snapshot in the owner while other clients commit, so an iterator never sees a half-applied transaction.
- The protocol is the boundary where typed gBASIC values cross between processes, and that boundary already exists (`serialize` and the actor wire). That is why exact-integer serialization is a prerequisite (§3.2).

**Proposed shape, to be validated by the storage-design measurements (§6.2):**
- The owner holds an exclusive lock on the file and listens on a local socket beside it (`business.gsql` → a socket the engine names; AF_UNIX on Linux, and Windows 10+ supports AF_UNIX too). A process opening the database tries to connect first; if nothing answers, it takes the lock and becomes the owner, or starts one.
- **One protocol, always**, including for a program that is the only client. A second, in-process path would be a second implementation that can disagree with the first, the drift this project tripwires against. Whether the round trip is affordable is **measured before it is accepted** (a design gate in §6.2), not assumed.
- **The owner's own concurrency:** proposed single-threaded, serving requests in order, with long queries made incremental (a cursor returns a batch per request) so one client cannot stall the others. Measure before adding threads.

**Decided (v0.4): the owner's lifetime is handed off, never detached.**
- gBASIC promises that **nothing it starts outlives it** (`PR_SET_PDEATHSIG`, `run_process_lifetime.sh`), and G-SQL keeps that promise. An owner started by a client is that client's child and dies with it, even while other clients are still using the database.
- **When the owner disappears, a surviving client takes the lock and becomes, or starts, the next owner, which runs crash recovery on open.** An owner ending is therefore always the recovery path, so recovery is exercised on every handoff rather than only after a real crash. A clean exit and a kill -9 take the same route, which is what makes the crash harness (§6.2) a test of ordinary operation.
- **Rejected:** an owner that detaches and lingers after its last client. It would break the lifetime promise, and it would leave recovery as a path that runs only after accidents.
- **To specify and measure:** which client wins the takeover (the first to take the exclusive lock; the others reconnect to it), how long a handoff takes including recovery, and what a client's in-flight request sees (proposed: an open transaction is reported as **failed**, and is never silently retried; a read-only cursor may be reopened at its snapshot if the engine can name one).
- **Consequence found with G-Dash (2026-10-21):** an owner per file does not fit an application that keeps hundreds of small database files, which is G-Dash's file-per-dataset model. `gsql_storage_design.md` §5.1.1a recommends datasets as tables in one database per deployment or tenant, with an owner per directory as the alternative.
- **Also open:** authentication of local clients (socket file permissions as the baseline), and what a client sees mid-transaction when the owner dies (proposed: the transaction is reported as failed, never silently retried).

### 6.2 The storage engine is designed and measured first (decided, v0.4)

**The Phase 0a plan is `docs/gsql_storage_design.md`:** the candidates in detail, the shared components (page format, order-preserving key encoding, I/O layer, version store), the workload, crash and handoff harnesses, and how the choice is made.

**Decision:** the storage engine's design is settled **before** layer-1 implementation (the SQL surface, §11) begins, by building candidate approaches small enough to measure and choosing between them on evidence.

**Layering (judgement, v0.4):** keep a **storage interface** as a named seam between the engine's relational layer and its storage. It is not a plan to ship an interim SQLite-backed product. The interface earns its place because it is what lets the candidates be compared under one workload harness and one crash harness. SQLite may serve as a **reference oracle** in differential tests, and only there.

**Candidates to build and measure:**

| approach | shape | why it is a candidate here |
|---|---|---|
| **Copy-on-write B+tree** (LMDB's shape) | pages never overwritten in place; a commit writes new pages and atomically swaps the root | single writer by design, which §6.1 provides; a snapshot is a root pointer, so consistent reads and **hot backup come almost free** (§6.3); no separate WAL; old roots may also serve temporal history (§10D) |
| **B+tree with a write-ahead log** (SQLite/PostgreSQL shape) | pages updated in place, changes journalled first, periodic checkpoint | the best-understood design; good space efficiency; needs checkpointing and a separate snapshot mechanism |
| **Log-structured merge tree** (RocksDB shape) | writes appended to sorted runs, merged by compaction | strongest for write-heavy ingestion; costs read amplification and compaction tuning, a poorer fit for OLTP point reads |

**Measured on each, under one harness:**
- **Workloads** taken from the target applications rather than invented: point reads and writes by key, range scans, bulk load, a mixed OLTP profile, and long-running readers during writes.
- **Metrics:** throughput, p50/p99 latency, write amplification, file growth and space reclamation, recovery time after a crash, and backup time while writes continue.
- **Crash correctness, as the gating criterion:** kill -9 at every write and `fsync` point across many runs, then verify every committed transaction survived and nothing uncommitted is visible. This scales up the pattern `run_intake.sh`'s crash tier already uses.
- **The §6.1 gate:** client-to-owner round-trip cost per query and per row batch, against an in-process call.

The candidate chosen is the one that passes crash correctness and best serves the measured workloads. The losing designs and their numbers are recorded in this document, so the decision can be revisited with evidence.

### 6.3 Backup and restore

Only the engine knows a consistent point while writes continue, so **a consistent online backup belongs in the engine** (§1 scope rule). Everything around it (scheduling, retention, shipping copies elsewhere) is gBASIC.

- **Required:** a hot backup producing a consistent database file while the owner keeps serving. Under a copy-on-write design this is copying the pages reachable from a snapshot root; under WAL it is a checkpoint plus a copy under a read snapshot. Restore is opening the produced file.
- **Required:** an integrity check that verifies every page checksum and structural invariant, so a backup can be proved readable before it is needed.
- **Proposed, later:** incremental backup (pages changed since a previous backup), and point-in-time recovery from an archived log. Decide after §6.2 picks an engine, since the cost differs sharply between the candidates.
- Logical dumps and exports are gBASIC programs over queries, and are out of scope.

### 6.4 Encryption at rest

Three levels, and the engine owns only the middle one:

1. **Filesystem or disk encryption** (LUKS, BitLocker, cloud volume encryption). Zero engine cost, and it satisfies most compliance requirements. Documented as the baseline recommendation.
2. **Page-level authenticated encryption in the engine.** Each page sealed with AES-256-GCM, with the page number and file identity as associated data so pages cannot be swapped or replayed between positions, and a key supplied by the application at open and never stored in the file. It belongs in the engine because only the engine sees pages. It is an optional module behind `HAVE_LIBCRYPTO`: a build without it **refuses** to open an encrypted file by name.
3. **Field-level encryption** of selected values with the `crypto` library. A gBASIC concern; such columns cannot be indexed or compared in SQL.

**Decision for the page format now, even if level 2 ships later:** reserve space in every page for a nonce and authentication tag from the first format version. Retrofitting encryption would otherwise change the page layout and force a format migration. Nonce uniqueness must be guaranteed per write; a copy-on-write engine, which never rewrites a page in place, makes this simpler, and §6.2's comparison should record that. **Open:** key rotation (re-encrypting every page, online or offline) and the backup of encrypted databases (a backup stays encrypted under the same key).

## 7. Statistical integration

The repository already contains extensive statistics capabilities, including descriptive reductions, distributions, regression/GLMs, ANOVA, clustering/PCA, time-series methods, and econometrics. Preserve its established architecture: **numerical primitives in the runtime, statistical compositions in gBASIC**. Do not reimplement the statistics library as a second SQL statistics engine.

### 7.1 Three execution categories

| Category | Example | Preferred home |
|---|---|---|
| Scalar | `stats.normal_cdf(score, 0, 1)` | SQL expression when pure, safe, and useful |
| Aggregate | `STDDEV_SAMP(balance)`, `CORR(x,y)` | SQL group/window operator with shared numerical kernels |
| Dataset analysis | `stats.ols(y, [x1,x2])`, `stats.logistic_regression(...)` | gBASIC over column-major frame |

### 7.2 Incremental aggregate contract

Expose shared statistical kernels through `init → accumulate → merge → finalize`, with explicit type, missing-value, numerical-stability, and memory contracts. `merge` enables future partial/parallel aggregation. Exact quantiles/medians may require sorting or spilling; approximate algorithms should be opt-in and named explicitly.

Illustrative SQL:

```sql
SELECT branch,
       AVG(balance) AS mean_balance,
       STDDEV_SAMP(balance) AS balance_sd,
       CORR(balance, account_age) AS correlation
FROM accounts
GROUP BY branch;
```

### 7.3 Calling gBASIC scalar functions

```sql
SELECT account_id,
       stats.normal_cdf(risk_score, 0, 1) AS percentile
FROM risk_scores;
```

This is **proposed syntax**, not a claim that arbitrary gBASIC functions already run in SQL. Establish a whitelist or explicit SQL-callable function contract, purity/side-effect rules, stable bindings, failure behavior, cancellation, and resource limits. SQL execution must not inadvertently invoke I/O, mutate database state, or make nondeterministic results appear deterministic.

### 7.4 SQL-to-frame bridge

Make a first-class conversion from a SQL result to `frame`'s existing **record of equal-length column lists**, without requiring a full intermediate list of row records. Preserve types and `unknown`, handle duplicate column names explicitly, and define memory limits/streaming or chunking for large results.

Illustrative gBASIC:

```basic
sql observations
    SELECT age, income, defaulted
    FROM loan_history
    WHERE training_eligible = true;
end

data = frame.from_query(observations)  ' Proposed API
model = stats.logistic_regression(data.defaulted, [data.age, data.income])
```

This unlocks the existing statistics library immediately and keeps regression, inference, and other dataset-level analyses out of SQL grammar until there is a compelling use case.

## 8. Views, functions, and composability

- **SQL view:** relational expression composed with outer SQL and eligible for whole-query optimization and predicate pushdown.
- **gBASIC function containing SQL:** reusable procedural/business operation, can rebind and execute a prepared SQL block, but not automatically optimized together with a caller's relational query.
- **Frame operation:** in-memory statistical or structural transformation when application-level processing is clearer.

Avoid forcing everything into SQL simply because it can be expressed there. Likewise, avoid moving large datasets to gBASIC just to perform an operation that indexes or group execution can do more efficiently in the engine.

## 9. Collation, equality, and ordering

Before indexes are finalized, specify:

- String normalization, Unicode collation, case sensitivity, and whether alternate collations are supported.
- Equality and ordering consistency between SQL operators, gBASIC semantics, and B-tree keys.
- Sort ordering of `unknown`, including ascending/descending and unique-index treatment.
- Cross-type comparison and coercion policy: prefer explicit conversions; no automatic string-to-number conversion merely for SQL compatibility.
- Temporal comparison, timezone/precision rules, and stable serialization.
- Numeric `NaN`, infinities, signed zero, and money-currency ordering.

**Critical invariant:** an index comparator must agree with query equality/order semantics for the types and collation it indexes, or the optimizer may return incorrect results.

## 10. Security and resource management

- Typed bound parameters, never textual interpolation of values.
- Restrict callable gBASIC functions by capability and side-effect policy.
- Statement cancellation, timeout, bounded memory, temp-file quotas, and cleanup on exceptions.
- Ensure iterators and transactions close deterministically when exiting a gBASIC scope or on errors.
- Protect against malicious/corrupt database files and invalid serialized values.
- SQL functions and views must have explicit dependency and version invalidation semantics.

## 10A. Pre-G-SQL gBASIC Core Contracts (proposed)

**Decision:** Implement a focused shared-runtime foundation before G-SQL; do not make all ecosystem improvements prerequisites. These are language-wide capabilities, not features owned by G-SQL. Continue Qt development independently where possible.

| Improvement | Plan | Timing |
|---|---|---|
| Exact integers **through the binary serializer** | The type question is closed (§3.2). What remains is `exact_number_design.md`'s increment 4: `serialize`/`deserialize` loses exactness, and that is the row format's and the actor wire's door. Plus the division/mixed-arithmetic specification | Before G-SQL |
| Connector conversions | Shared semantic contract for integer, decimal, money, temporal, binary, and NULL; adapter-specific mappings remain explicit | Before G-SQL |
| Binary database support | **The value-kind question is already answered**: PLAT-NUL's door sweep settled it per door — sqlite was truncating and is fixed, PostgreSQL `text` *refuses* 0x00 and the answer was a refusal naming `bytea`, and ODBC's `SQL_C_CHAR` is NUL-terminated by construction and documented. gBASIC strings carry arbitrary bytes, so **no dedicated binary value is needed**; the work is BLOB/`bytea` binding and results | Prefer before G-SQL |
| Streaming and batching | Source-independent iterator lifecycle, bounded buffering, cursor-based row consumption, batch operations | Core before G-SQL; extend later |
| Reproducible dependencies | Project manifests, pinned versions, deterministic load resolution | Design early, implementation can parallel G-SQL |
| Capability checks | **Partly shipped**: `has_builtin` was made to answer for *this* build and `gbasic --capabilities` exposes the surface. One decision is left and the tree already names it — `load` **raises** for a module compiled out, so a program cannot ask before loading; changing that is language-visible and not G-SQL's to take | Design early |
| Cross-platform verification | Automate native Windows testing alongside Linux, document platform differences | Before G-SQL |
| Operational conventions | Consistent timeout, cancellation, errors and cleanup across DB, HTTP, files and processes | Before G-SQL |
| Reliability helpers | Opt-in retry/backoff, checkpoints, duplicate prevention, structured logs; never implicitly retry non-idempotent operations | Later/shared libraries |
| Documentation contracts | Test runnable examples, reconcile contradictory descriptions, record error and conversion behavior | Continuous |

### 10A.1 Semantic commitments and limits

- `unknown` **is** the SQL `NULL` value semantically; specify three-valued logic, grouping, joins, comparisons and aggregate handling consistently. `NULL` and `UNKNOWN` are both accepted **in SQL**; gBASIC keeps `unknown` alone and `null` stays a free identifier (§3.1, ruled in v0.3).
- `nothing` remains distinct, denoting absence. In a SQL binding context an explicitly supplied `nothing` maps to SQL NULL; omitted columns still invoke defaults.
- G-SQL operates directly on native gBASIC values. External databases cannot always share exact semantics or representations; adapters must document losses, coercions, precision and unsupported values rather than claiming perfect uniformity.
- Prefer a shared conversion *contract* rather than a compulsory intermediate value-copying layer.
- Stable versioned value serialization and lightweight function metadata (purity, determinism, SQL eligibility, dependency version) are recommended design investigations, not immediate requirements.

### 10A.2 Implementation order

1. **Contracts and tests:** integer and NULL semantics, conversion matrices, error/cancellation/cleanup guarantees, iterator behavior.
2. **Runtime foundations:** exactness through `serialize` and the actor wire (`exact_number_design.md` increment 4 — not a new type, §3.2), BLOB/`bytea` binding, a third iterable kind for `for each` (§10B), database streaming/batching primitives, Windows/Linux CI.
3. **Ecosystem work:** manifests, capabilities, retry/checkpoint/logging libraries, continuing documentation tests; may overlap G-SQL.
4. **G-SQL:** build its durable relational core on the established contracts.

## 10B. Windowed iteration (proposed, rewritten in v0.3)

**The need is agreed; the v0.2 spelling is withdrawn.** A loop that can see the rows either side of the current one is wanted for arrays, query results and streamed files alike, and it belongs to iteration rather than to SQL results. What v0.2 proposed was:

```basic
for each tran in trans window(10, 5)     ' WITHDRAWN -- see below
    print tran[-1].amount
next
```

### 10B.1 Why it is withdrawn (measured, v0.3)

1. **It costs a grammar conflict, and the slot is taken twice.** Added to `src/parser.y` beside the existing `for each` rules, `… IN expression IDENT ( … )` gives **1 shift/reduce**: bison's counterexample is `for each x in 10 window(…)`, where `10 window` is already a duration literal (`5 minutes`). Moving it behind `with` gives **1 shift/reduce** too, against `new T with { … }`. The standard here is zero. A lexer trigger could hide the conflict, but the grammar was never the real objection.
2. **`tran[-1]` redefines indexing on whatever the loop variable holds.** Today `a[-1]` on an array **raises** `array index out of range`, and indexing a record by number raises too. Under the proposal the same text would mean "the previous row". So either the loop variable becomes a wrapper and stops being an ordinary value, which breaks `encode`, comparison and passing it to a function, or `x[-1]` means different things depending on whether `x` is a loop variable. In that case `prev = tran` then `prev[-1]` silently changes meaning. v0.2 named this collision as unresolved, and it has no resolution that keeps the item an ordinary value.
3. **Out-of-window answering `nothing` reverses a decision the tree made on purpose.** An out-of-range read used to print and yield `nothing`. It was promoted to a raise (`tests/run_silent_traps.sh`) because `nothing` is a legitimate value, and a caller could not tell the failure from a real one. v0.2's own bullet then needs a way to "distinguish end-of-stream from a legitimate yielded `nothing`", which is the same ambiguity reintroduced.

### 10B.2 The replacement: `around(source, before, after)`

```basic
for each w in around(trans, 1, 1)
    if len(w.before) > 0 then print w.before[0].amount   ' the previous row
    print w.item.amount                                  ' the current row
    if len(w.after) > 0 then print w.after[0].amount     ' the next row
next
```

- **An ordinary builtin, so zero grammar.** The name is free (no builtin, stdlib function or call site uses it). `window` was not used, because it already names the GUI declaration.
- **Each element is an ordinary record** `{ item, before, after }`. It can be captured, passed, compared and encoded, so the escape/capture problem never arises.
- **`before` and `after` are arrays, nearest first.** `w.before[0]` is the previous item and `w.after[0]` the next, so the common case is index 0 in both directions.
- **A boundary is a shorter array, not a sentinel.** At the first item `w.before` is `[]`. `len` answers whether a neighbour exists, so a legitimate `nothing` item stays unambiguous, and reading past the end still raises as every other array read does.
- **It composes with what exists.** `for each w, i in around(trans, 1, 1)` gives the position too, through the index form that shipped for exactly this kind of write-back.
- Neither argument may be negative, and both must be exact integers. A refusal is better than rounding.

**For arrays this is buildable now**, and for arrays the index form already answers most cases at no cost: `for each t, i in trans` then `trans[i-1]` (asserted in `run_for_each_index.sh`).

**For streams it is blocked on one prerequisite.** `for each` accepts exactly two kinds today: an array and a file (`src/eval.c`, "for in expects an array or a file"). An eager `around` over a file would materialise it, which defeats streaming. So the streaming form needs a **third iterable kind**, a lazy iterator value. That is the "source-independent iterator contract" of §10A, and it is listed there as a runtime foundation (§10A.2). With it, `around` holds `before + 1 + after` items and no more.

### 10B.3 What stays from v0.2

- Lookahead **advances the underlying source** before the current item is processed. For a blocking or side-effecting source, that must be documented and is inherent to any lookahead.
- For SQL, adjacency means something only under an explicit, stable `ORDER BY`. SQL `LAG`/`LEAD` remain separate, optimisable relational operations (§4.2).
- No random access is implied for streamed sources. `around` gives neighbours, not `result[1000]`.

## 10C. Experimental G-SQL syntax register (not adopted)

The following came from exploratory examples and must be reviewed for semantics, implementation complexity, SQL familiarity, and interactions with G-SQL's existing architecture **before adoption**:

1. **Column-level `TEMPORAL` and `AS OF`:** Selective temporal columns, current-first/backward-linked history, current non-temporal values, and optional table-wide declaration are now agreed directions; exact physical layout, effective versus recorded time, retention, and correction semantics remain open (§10D).
2. **`SERIAL OF DATE(request_time)`:** General categorically scoped numbering, not date-only. Define scope expression, composite identity, late inserts, concurrency, rollback/gaps, and update semantics (§10E).
3. **`RELATE ... TO MANY`:** Named, cardinality-aware relationship metadata with multiple relations between the same tables; exact query/navigation syntax and richer predicates remain open (§10F).
4. **`GET name AS SELECT ...`:** Define whether this returns a lazy stream, one record, a collection or a scalar, and how zero/multiple rows and transaction lifetime behave. Compare with the existing SQL-block result proposal.
5. **Native `MONEY('USD')` spelling:** Reconcile with the existing proposed `MONEY(USD)` and gBASIC currency syntax. Enforce exact native currency semantics.
6. **`DATETIME TEMPORAL`, `VARCHAR(n)` and other declarations:** Determine which are familiar SQL aliases and which introduce actual runtime distinctions; avoid unnecessary duplicate type systems.
7. **`tran[-1]` within SQL results:** **Withdrawn** (v0.3). Neighbouring rows come from `around(...)` (§10B.2), which is not SQL-specific and does not redefine indexing.

**Review principle:** Distinguish elegant surface syntax from underlying data model. Prototype semantics and tests before changing parser or persistent storage layout.

## 10D. Selective temporal values and audit history

**Agreed direction:** Temporal tracking is opt-in per column, with an optional whole-table declaration meaning that eligible non-key columns are temporal by default. Keys identify the entity and ordinarily remain non-temporal. Other stable columns need not carry history. A normal read obtains the current value directly; a historical read walks backward through older versions. This is deliberately different from requiring a complete row copy after every change.

**Illustrative syntax (proposed):**

```sql
CREATE TABLE members (
    member_id INT64 PRIMARY KEY,
    name STRING TEMPORAL,
    address STRING TEMPORAL,
    created_at DATETIME
);

SELECT * FROM members AS OF :requested_time;
```

- **Agreed query meaning:** `AS OF` applies to the query's temporal reconstruction point; temporal columns return their values at that point, and non-temporal columns return their *current* values. Document that an `AS OF` result is **not** necessarily a complete historical snapshot of all columns. Whole-table `TEMPORAL` is a declaration convenience, not necessarily a separate engine.
- **Proposed physical representation:** Current value stored/accessed directly; older versions in an append-oriented version store with backward links. Access to old values is proportional to changes traversed. An implementation may add optional skip links or indexes *only if measured workloads justify them*.
- **Atomicity:** A transaction changing multiple temporal columns must leave a consistent committed state; associate versions with a transaction/commit identifier, not independent timestamps alone.
- **Audit/SOX motivation:** Record who/what changed a value, when it was recorded, and ideally a reason/source. Version history alone does not guarantee tamper resistance, SOX compliance, retention, or authorization. These need separate controls and evidence.
- **Open:** Effective time versus recorded time (bitemporal corrections); whether historical versions are immutable; retention and deletion rules; time-zone precision; schema changes; how row deletion and creation interact with `AS OF`; how historical indexes should work. Preserve enough version metadata to avoid blocking later recorded-time queries, without building full bitemporal machinery before needed.
- **Open:** How `AS OF` composes with joins and `RELATE`, including a temporal column evaluated at a time taken from another row.

## 10E. Categorically scoped serials

**Agreed direction:** A serial can increment independently for each value of a deterministic scope expression, not merely by date. `DATE(request_time)` is one scope expression that groups all times in a calendar day; `(business_date, branch_id)` is another. The generated number is unique **within the scope**, not necessarily globally.

```sql
-- Illustrative only
transaction_id INT64 SERIAL OF DATE(request_time)
-- Or a composite scope:
transaction_id INT64 SERIAL OF (business_date, branch_id)
```

- A complete identifier may need a composite key `(scope, serial)`; do not imply the serial alone is a global primary key.
- Allocation must be atomic among concurrent writers; scope expressions must be stable/deterministic and evaluated against the inserted record.
- Backdated inserts require retaining or reconstructing a scope's high-water mark. Changing a scope column after insertion must have explicitly defined behavior.
- **Open:** Rollback gaps, allocation timing, deletion/reuse, default start/increment, scope equality/collation, timezone/date cast, overflow policy, and persistence/recovery of counters. Favor the simplest reliable sequence contract rather than an elaborate sequence framework.

## 10F. Named `RELATE` schema metadata

**Agreed direction:** Relationships are reusable, named, cardinality-aware schema knowledge. Multiple relationships can exist between the same tables (e.g., originating and destination member). They improve query clarity, discoverability, validation, schema tools, G-Dash, and potentially AI-assisted query construction. They do **not** require eager materialization of related rows and are distinct from foreign-key enforcement.

```sql
-- Illustrative only
RELATE originating_member
    FROM share_transactions
    TO ONE members
    ON share_transactions.orig_member_id = members.member_id;

RELATE destination_member
    FROM share_transactions
    TO ONE members
    ON share_transactions.dest_member_id = members.member_id;
```

- Explicit ordinary `JOIN ... ON ...` remains available and can override a declared relationship when a query needs different logic.
- **Proposed:** Store relation name, participating tables, cardinality, join predicate, and dependency/schema version. Make relationships available for discovery and diagnostics.
- **Open:** Exact query/navigation syntax; meaning and enforcement (if any) of cardinality; optional versus mandatory matches; composite/non-equality predicates; ambiguity resolution; rename/schema migration; security permissions. Start with analyzable equality predicates and extend only when real workloads demand more.
- **Exploratory:** Let business rules navigate named relationships through a constrained read-only context. Do not make this a prerequisite or silently allow unrestricted database access.

## 10G. One G-SQL engine, two entry points

**Agreed architectural requirement:** Both inline SQL blocks and dynamically supplied SQL strings must execute through the **same parser semantics, typed AST, planner, and execution engine**. G-Dash, generated reports, and AI-assisted queries require runtime SQL. Inline SQL may receive earlier validation; dynamic SQL validates at runtime. Typed parameter binding is mandatory in both cases.

```basic
' Conceptual only: API and syntax to be finalized
sql recent on business
    SELECT * FROM transactions WHERE member_id = :member_id;
end

result = business.query(sql_text, params)  ' Dynamic SQL example
```

- Do not create separate dialects or diverging NULL, money, date, relationship, or error semantics.
- Cache prepared plans when useful; invalidate them on schema or approved function-definition changes.
- Dynamic SQL is **not** unrestricted gBASIC code evaluation. Distinguish SQL execution privileges from general program execution; enforce authorization, limits, cancellation, and binding.
- **Open:** API names; SQL-block compile/parse boundary; multi-statement policy; plan caching strategy; permissions and sandboxing for untrusted/generated SQL.

## 10H. Shared typed expression semantics with business rules

**Agreed direction:** Reuse value operations, comparison, Boolean/`unknown` semantics, and (where practical) parsing/evaluation infrastructure between G-SQL and the business-rule system. Avoid subtle divergence for the same business condition. SQL and rules can have different available names, functions, capabilities, and side-effect policies.

- **Proposed:** A small typed expression core with context-specific bindings and permitted functions, rather than a universal expression language or unrestricted `eval`.
- SQL retains relational execution/optimization; business rules retain workflow and decision responsibilities in gBASIC.
- Function metadata (purity, determinism, SQL-callability, version) should be minimal and driven by actual use in SQL planning, rule evaluation, and index safety.
- **Open:** Shared grammar subset, three-valued condition handling outside SQL, permission boundaries, and whether rule-side relationship navigation is worthwhile.

## 10I. Design review gates and acceptance tests for experimental features

Before committing storage format or grammar, review: (1) user-facing clarity, (2) native gBASIC value compatibility, (3) failure and boundary semantics, (4) transactional durability, (5) optimizer behavior, (6) implementation/maintenance complexity, and (7) evidence from actual applications. Reject an abstraction when added flexibility costs more complexity than the targeted use case warrants.

Representative tests to add when the corresponding feature is implemented:

- Current temporal reads do not traverse history; `AS OF` reconstructs only temporal columns and returns current non-temporal columns; concurrent multi-column changes are atomic.
- History records retain commit order and required audit provenance; corrected/backdated values follow the selected effective/recorded-time policy.
- Two concurrent inserts in the same serial scope receive distinct numbers; different scopes can independently reuse the same number; crash recovery does not duplicate committed allocations.
- Two named relationships between the same tables resolve without ambiguity; explicit joins remain available; cardinality violations have documented behavior.
- Identical inline and dynamic parameterized SQL produce equivalent values, `unknown` handling, and errors.
- SQL and business rules agree on shared typed operators for supported values; disallowed side-effecting functions cannot be invoked through SQL.
- Windowed `for each` works for arrays and streamed sources, including boundaries, legitimate yielded `nothing`, lookahead side effects, cancellation, and nested loops.

## 11. Implementation roadmap

### Phase 0a: Storage-engine design by measurement (decided, v0.4)

Build the §6.2 candidates far enough to run the workload, crash and round-trip harnesses; choose; record the losing designs and their numbers. Measure the §6.1 handoff (takeover, reconnect and recovery time). Fix the page format, including the reserved encryption fields (§6.4). Nothing in Phase 1 is coded before this is decided.

### Phase 0: Semantic contracts and test harness

Coordinate with §10A Core Contracts and §10B windowed iteration; these are cross-language foundations rather than G-SQL-only work.

Freeze type mapping, `NULL`/`UNKNOWN` aliasing, `nothing` binding, money and temporal comparisons, integer policy, collation, persistence encoding, and transaction guarantees. Specify temporal-history commit metadata, scoped serial allocation, relationship catalog, and the common inline/dynamic SQL execution path **before fixing storage and parser architecture**. Create differential/fixture tests against gBASIC runtime behavior and crash/recovery tests.

### Phase 1: Durable relational core

File format, table storage, WAL/recovery, transactions, basic schema, DML, row iteration, primary keys/B-tree indexes, typed parameter binding, initial `SELECT`/`WHERE`/`ORDER BY`, dynamic SQL entry point and inline SQL sharing one execution path, and `EXPLAIN`. One-writer concurrency model documented and tested. Reserve a versioned representation for selective temporal history without requiring complete temporal query support on day one.

### Phase 2: Useful relational SQL

Joins, grouping/HAVING, common aggregates, unions, subqueries, nonrecursive CTEs, views, composite indexes, query-plan caching, statistics, and efficient hash operators. Add named relationship catalog and scoped serials when their semantics/tests are settled. Introduce spill-to-disk where workloads require it.

### Phase 3: Statistical integration and analytics

Shared statistical aggregate kernels, safe scalar gBASIC function calls, direct SQL-to-frame conversion, window functions including `LAG`/`LEAD`, and explainable residual predicate handling. Add selective `AS OF` queries when temporal history/recovery and audit semantics are tested; keep full bitemporal queries optional.

### Phase 4: Advanced SQL and scaling

ROLLUP/GROUPING, grouping sets/CUBE, richer windows, sophisticated optimizer, improved concurrency, expression indexes with function versioning, optional partitioning, and advanced query plans. Add only when justified by real workloads.

**Phasing note:** exact placement of a feature is negotiable. The architectural contracts and testability are not. A minimally useful G-SQL release should be intentionally smaller than this full target.

## 12. Decisions already aligned

1. Native G-SQL is an embedded relational engine integrated with gBASIC, not a generic SQLite clone.
2. Keep PostgreSQL, SQL Server, ODBC, and other external adapters.
3. Share native gBASIC value semantics; avoid an independent SQL type system.
4. `unknown`'s truth tables are identical to SQL `NULL`'s; gBASIC is changed to match before G-SQL (§3.1.1). `NULL` and `UNKNOWN` are two **SQL** spellings of one value, `unknown`; gBASIC keeps `unknown` alone (ruled v0.3, §3.1). SQL-bound `nothing` becomes `unknown`.
5. No new numeric type. Exact integers are a property of `number` and already shipped; `INT64`/`INTEGER` are SQL column constraints (§3.2). *(v0.2 said "add a single signed `INT64` type"; reversed in v0.3.)*
6. Native money and temporal types remain native end to end.
7. Support joins, unions, grouping, transactions, keys, indexes, and views.
8. Include statistical aggregates and a SQL-to-frame bridge; preserve gBASIC stats compositions.
9. Permit carefully controlled pure scalar gBASIC functions inside SQL.
10. Protect SARGability through access/residual predicate separation and transparent `EXPLAIN` output.
11. Defer costly advanced features where a clear initial benefit has not been demonstrated.
12. One owning process per database file; every other process is a client. When the owner exits, a surviving client becomes the next owner and runs recovery; no owner outlives the processes that started it (§6.1).
13. The storage engine is chosen by measurement before the SQL layer is implemented (§6.2).
14. The engine holds only what cannot be done as well in gBASIC (§1): access control, admin tools, import/export and migration tooling are gBASIC's.
15. `unknown`'s truth tables are SQL's, except that procedural control flow (`if`, `while`, `until`) still raises on `unknown` (§3.1.1).
16. In the first release, one query reads one database; cross-database queries are added only when a real application needs them. A database holds many tables and views, and multi-tenant applications use one database per tenant (WorkSplicer's arrangement).
17. Record-valued columns and indexes on one record field are in the first release (§3.5), so per-type parameters (WorkSplicer's asset types) need neither EAV tables nor a table per type.

## 13. Questions requiring a final engineering decision

1. Literal and mixed-arithmetic exactness are already specified (`exact_number_design.md`). What remains is G-SQL's side: does an `INT64` column refuse an inexact value on write, and refuse on overflow instead of degrading with warning 2112? §3.2 proposes yes to both.
2. Decided: SQL's (§3.1.1), with `if unknown` still raising in procedural code.
3. Which persistent value kinds and date/time variants are supported in the first release?
4. Which storage design wins the §6.2 measurements, and how long does an owner handoff take (§6.1)?
5. What are the first-release collation and comparison rules?
6. Do foreign keys and `CHECK` constraints ship in the first release or the next?
7. How are schema changes, function versions, and prepared plans invalidated?
8. How are large SQL results converted to frames without exhausting memory?
9. Which SQL-callable function contract is safest and simplest to implement first?
10. Which actual workloads (G-Dash, WorkSplicer, Transcend, gProperties) define the initial performance benchmarks?

## 14. Acceptance criteria and representative tests

- Money values round-trip exactly with currency and guard-digit semantics intact.
- `NULL` and `UNKNOWN` behave identically; bound `nothing` becomes SQL NULL; omitted columns apply defaults.
- Exact integers across the full signed 64-bit range round-trip through a stored row without loss. A test must use 2^53 + 1 and assert `+ 1`, since the digits render identically either way. An inexact or overflowing value written to an `INT64` column is refused.
- Crash during a transaction leaves either a fully committed or fully rolled-back state, never partial committed rows.
- Index seek plus residual statistical filter produces identical results to a full scan.
- Views composed with outer filters yield correct results and allow safe predicate pushdown.
- Grouped `STDDEV`/`CORR` match the shared gBASIC numerical implementation within documented tolerances.
- SQL-to-frame preserves row order where specified, native values, column names, and `unknown`.
- `EXPLAIN` accurately reports scans, seeks, joins, and residual filters.
- Memory and temporary disk use are bounded and observable for large joins, sorts, and aggregations.

---

### Existing project references

- [gBASIC repository](https://github.com/Solifugus/gbasic)
- [Statistics design](https://github.com/Solifugus/gbasic/blob/master/docs/statistics_design.md)
- [Statistics library](https://github.com/Solifugus/gbasic/blob/master/stdlib/stats.bas)
- [Statistics scientist plan](https://github.com/Solifugus/gbasic/blob/master/docs/statistics_scientist_plan.md)
- [Money design](https://github.com/Solifugus/gbasic/blob/master/docs/money_design.md)

**Document purpose:** An architectural starting point for implementation planning and review, not a claim that the proposed G-SQL features already exist.


## 15. Revision history

### v0.4 — 2026-10-21

Ruled by the project owner: `if unknown` keeps raising (§3.1.1), and the owner's lifetime is a handoff with recovery, never a detached owner (§6.1).

Decided: one owning process per database (§6.1); the storage engine designed and measured before the SQL layer, with three candidates and a crash harness as the gate (§6.2); the scope rule that the engine holds only what gBASIC cannot do as well (§1). Added backup and restore (§6.3) and encryption at rest (§6.4), reserving the page-format space for encryption now. Measured that gBASIC's `unknown` does not follow SQL's truth tables, and recorded the agreed change, including the need for both of SQL's equalities (§3.1.1).

### v0.3 — 2026-10-21

Checked v0.2 against the repository rather than against itself (§0, §16). Reversed: the new `INT64` type (exactness already lives on `number`; the real prerequisite is the binary serializer) and `NULL` as a gBASIC alias. Priced: both block heads at zero conflicts. Withdrawn and replaced: the `window(...)` head and `tran[-1]` (§10B). Answered from the tree: the binary value kind and part of capability checks. The retired `gbasic_gsql_design.md` was compared line by line before removal.

### v0.2 additions (2026-10-20, first round)

Added the pre-G-SQL Core Contracts plan, source-independent optional windowed `for each` iteration, and an explicit review register for exploratory syntax. These additions do not supersede the relational-engine architecture or imply the experimental features are approved.

### v0.2 — 2026-10-20

Merged decisions from the October 20 discussions: selective linked temporal history, `AS OF` behavior with non-temporal columns, optional table-wide temporal declaration, categorically scoped serials, multiple named `RELATE` relationships, common inline/dynamic SQL architecture, shared expression semantics with constrained business rules, and an explicit anti-overengineering/non-goals policy. Experimental syntax and unresolved semantics remain marked as such.

## 16. How the v0.3 measurements were taken

So they can be re-taken rather than trusted.

- **Exactness** (§3.2): a scratch program with `b = number("9007199254740993")`, printing `type(b)`, `b + 1`, `mod(b, 2)`, `b = 9007199254740992`, `decode(encode(b)) + 1` and `deserialize(serialize(b)) + 1`. The last answers `9007199254740992`, which is the loss. `tests/run_exact.sh` is the standing suite.
- **`null` is free** (§3.1): `null = 5` then `print null` runs and prints 5.
- **Grammar prices** (§2.1, §10B.1): the candidate production inserted beside the `for each` rules in a scratch copy of `src/parser.y`, then `bison -d` on the copy and `bison -Wcounterexamples` for the colliding input. The baseline grammar reports no conflicts. Never measure in `src/` itself: a write to a build input makes the suite a rebuilder in the parallel gate.
- **Indexing** (§10B.1): `a[-1]` and `a[9]` on a three-element array both raise `array index out of range` under `on error goto next`.
- **Iterable kinds** (§10B.2): the `for each` evaluation in `src/eval.c`, whose refusal reads "for in expects an array or a file".
- **The retired document** (top of file): every non-blank line of `git show HEAD:docs/gbasic_gsql_design.md` tested for verbatim presence here; 219 of 233.
- **Truth tables** (§3.1.1, v0.4): a scratch program evaluating each expression in its own function under `on error goto next`, printing the value or the raised message. Usage counts: `= unknown` in a condition position across `stdlib`, `examples` and `tests` (about 10), and `is_unknown(` occurrences (928).
