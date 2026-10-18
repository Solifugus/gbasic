# G-SQL: Native Relational Database for gBASIC

**Status:** Proposed architecture and phased implementation plan  
**Date:** 2026-10-17  
**Project:** [gBASIC](https://github.com/Solifugus/gbasic)  
**Scope:** Native embedded relational engine, SQL blocks, shared gBASIC values, statistics integration

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

## 2. Proposed gBASIC surface

Syntax is illustrative and subject to parser review.

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

## 3. Type system and persistence semantics

| gBASIC value | G-SQL declaration | Semantics |
|---|---|---|
| `number` | `NUMBER` | Native binary64 number; no implicit decimal-exactness claim |
| `integer` (proposed) | `INT64` / `INTEGER` alias | One signed 64-bit integer type, checked overflow; a language-wide feature, not a family of machine-width types |
| `string` | `STRING` | Native Unicode string; explicit collation and ordering rules |
| `boolean` | `BOOLEAN` | Native boolean |
| `money` | `MONEY` / `MONEY(USD)` | Native tagged exact money; currency-constrained variant; retain guard digits and existing rounding/overflow rules |
| temporal values | `DATE`, `TIME`, `DATETIME` (exact names TBD) | Reuse existing gBASIC temporal representation and precision semantics |
| `unknown` | `UNKNOWN` / `NULL` | Exactly one missing-value sentinel with two spellings |
| `nothing` | No persisted SQL type | On parameter binding as a SQL value, normalize to `unknown`; never return `nothing` for a stored NULL |

### 3.1 Null and absence

- Make `NULL` and `UNKNOWN` aliases, ideally in both SQL and ordinary gBASIC (the latter is a language change requiring compatibility tests).
- An explicitly bound `nothing` becomes `unknown`/`NULL`; a `NOT NULL` constraint rejects it.
- Omitting a column in `INSERT` invokes its declared default if present. Explicitly binding `nothing` **does not** invoke the default; it stores NULL or fails the constraint.
- Preserve established gBASIC `unknown` behavior. Specify SQL predicate truth tables, `IS NULL`/`IS UNKNOWN`, joins, grouping, and aggregates with tests rather than assuming every conventional SQL coercion is appropriate.
- Statistical reductions should preserve their documented missing-data policies; SQL aggregates should document how they ignore or propagate `unknown`.

### 3.2 Integers

- Introduce **one** signed 64-bit `integer` type in gBASIC, with `INT64` and optionally `INTEGER` as SQL spellings.
- Checked addition, subtraction, multiplication, conversion, and overflow; specify division and mixed integer/number arithmetic before shipping.
- Preserve existing numeric-literal semantics unless a separate language change is approved.
- If the database ships before the new language type, do not silently round large integer keys when returning them as binary64; fail safely or defer exposing such values until native integers exist.
- Primary keys and indexes must compare full 64-bit integer values exactly.

### 3.3 Money

- Reuse the native gBASIC money representation and arithmetic, including currency metadata and precision; never route monetary storage or aggregation through binary64 without an explicit, documented conversion.
- `MONEY(USD)` enforces currency on writes; `MONEY` accepts native currency-tagged money values.
- Currency-incompatible comparisons or aggregations must be rejected or explicitly converted with a dated rate; no implicit FX conversion.
- Define return units/types for `AVG`, `STDDEV`, `VARIANCE`, and other money-related statistics. Variance has squared monetary units and cannot naively return `MONEY`.

### 3.4 Persistence subset

Persist stable, serializable value kinds, not executable/process-bound values such as functions, watchers, handles, or actors. Arrays and records as column values are future extensions, not prerequisites for the relational core. Disk representation must be versioned and independent of process pointers and in-memory layout.

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
- Expression indexes are later work: function versions and library changes must not silently corrupt index meaning.
- Show residual predicates and index choices in `EXPLAIN`; never imply an index was used when it was not.

## 6. Transactions, concurrency, and durability

These are non-negotiable architectural requirements even if the first implementation is modest.

- ACID transactions with explicit `BEGIN`, `COMMIT`, `ROLLBACK` semantics and well-defined SQL-block behavior on error.
- Crash-safe recovery using a documented WAL or equivalent journal, checksums/validation as appropriate, atomic commit markers, and startup recovery.
- Explicit `fsync`/durability policy and tests for process termination during commits.
- Initial concurrency recommendation: **one writer, multiple readers** with consistent read snapshots; decide whether readers can overlap a writer before selecting page/version architecture.
- Isolation recommendation: start with a clear snapshot or serializable behavior that is implementable and testable; document transaction visibility, locking, and busy/timeouts. Do not claim MVCC or serializable guarantees until implemented.
- Consider savepoints later; define deadlock/busy/error semantics.
- Treat database files as a versioned format with migrations, integrity checks, backups, and recoverability.

**Open design decision:** exact journal/page layout and concurrency model require a focused storage-engine design review before coding.

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

## 11. Implementation roadmap

### Phase 0: Semantic contracts and test harness

Freeze type mapping, `NULL`/`UNKNOWN` aliasing, `nothing` binding, money and temporal comparisons, integer policy, collation, persistence encoding, and transaction guarantees. Create differential/fixture tests against gBASIC runtime behavior and crash/recovery tests.

### Phase 1: Durable relational core

File format, table storage, WAL/recovery, transactions, basic schema, DML, row iteration, primary keys/B-tree indexes, typed parameter binding, initial `SELECT`/`WHERE`/`ORDER BY`, and `EXPLAIN`. One-writer concurrency model documented and tested.

### Phase 2: Useful relational SQL

Joins, grouping/HAVING, common aggregates, unions, subqueries, nonrecursive CTEs, views, composite indexes, query-plan caching, statistics, and efficient hash operators. Introduce spill-to-disk where workloads require it.

### Phase 3: Statistical integration and analytics

Shared statistical aggregate kernels, safe scalar gBASIC function calls, direct SQL-to-frame conversion, window functions including `LAG`/`LEAD`, and explainable residual predicate handling.

### Phase 4: Advanced SQL and scaling

ROLLUP/GROUPING, grouping sets/CUBE, richer windows, sophisticated optimizer, improved concurrency, expression indexes with function versioning, optional partitioning, and advanced query plans. Add only when justified by real workloads.

**Phasing note:** exact placement of a feature is negotiable. The architectural contracts and testability are not. A minimally useful G-SQL release should be intentionally smaller than this full target.

## 12. Decisions already aligned

1. Native G-SQL is an embedded relational engine integrated with gBASIC, not a generic SQLite clone.
2. Keep PostgreSQL, SQL Server, ODBC, and other external adapters.
3. Share native gBASIC value semantics; avoid an independent SQL type system.
4. `NULL` and `UNKNOWN` are aliases for one value; SQL-bound `nothing` becomes `unknown`.
5. Add a single signed `INT64` integer type to gBASIC; no proliferation of machine-width types.
6. Native money and temporal types remain native end to end.
7. Support joins, unions, grouping, transactions, keys, indexes, and views.
8. Include statistical aggregates and a SQL-to-frame bridge; preserve gBASIC stats compositions.
9. Permit carefully controlled pure scalar gBASIC functions inside SQL.
10. Protect SARGability through access/residual predicate separation and transparent `EXPLAIN` output.
11. Defer costly advanced features where a clear initial benefit has not been demonstrated.

## 13. Questions requiring a final engineering decision

1. What are the exact gBASIC integer literal, declaration, division, and mixed-arithmetic semantics?
2. What is the precise truth table for `unknown` in SQL predicates, equality, grouping, and join conditions?
3. Which persistent value kinds and date/time variants are supported in the first release?
4. What are the page format, journal strategy, reader/writer snapshot model, and durability guarantees?
5. What are the first-release collation and comparison rules?
6. Do foreign keys and `CHECK` constraints ship in the first release or the next?
7. How are schema changes, function versions, and prepared plans invalidated?
8. How are large SQL results converted to frames without exhausting memory?
9. Which SQL-callable function contract is safest and simplest to implement first?
10. Which actual workloads (G-Dash, WorkSplicer, Transcend, gProperties) define the initial performance benchmarks?

## 14. Acceptance criteria and representative tests

- Money values round-trip exactly with currency and guard-digit semantics intact.
- `NULL` and `UNKNOWN` behave identically; bound `nothing` becomes SQL NULL; omitted columns apply defaults.
- Full signed `INT64` values round-trip without floating-point loss; overflow is reported.
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
