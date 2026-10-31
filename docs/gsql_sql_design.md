# G-SQL's SQL front end (Phase 1, increment 3)

**Status:** designed and built 2026-10-30 (`include/gsql_sql.h`, `src/gsql/sql_parse.c`, `sql_plan.c`, `sql_exec.c`, `src/modules/gsql.c`, `tests/run_gsql_sql.sh`). This is the third layer of Phase 1 (`gsql_table_design.md` §1): parser, binder, planner and executor, with `EXPLAIN`, run inside the owner as its service (`gsql_owner_design.md` §5.3). With it, the owner has something to answer, so G-SQL is linked into `gbasic` for the first time: `gbasic --gsql-owner` is the owner process the client starts.

## 1. What this increment is, and what it is not

**It is** single-table SQL over the table layer, in the dialect `gsql_design.md` §4.0 decided (standard SQL, PostgreSQL's choice where the standard is silent):

- `CREATE TABLE`, `DROP TABLE`, `ALTER TABLE … ADD COLUMN`, `CREATE [UNIQUE] INDEX`, `DROP INDEX`;
- `INSERT`, `UPDATE`, `DELETE`;
- `SELECT` with `WHERE`, `ORDER BY`, `LIMIT` and `OFFSET`, from one table or from none;
- `EXPLAIN`, which reports the access path and the residual filter;
- `:name` and `$n` parameters.

**It is not** views, which are Phase 2. Aggregates, `GROUP BY` and `HAVING` are Phase 2's first increment, joins its second, `SELECT DISTINCT` its third, `UNION`/`INTERSECT`/`EXCEPT` its fourth, subqueries in expressions its fifth, and derived tables and `WITH` its sixth. All six are built (`gsql_aggregate_design.md`, `gsql_join_design.md`, `gsql_distinct_design.md`, `gsql_setop_design.md`, `gsql_subquery_design.md`, `gsql_derived_design.md`) (`gsql_design.md` §11). Nor is it the gBASIC surface, the `gsql` module and the `database`/`sql` blocks, which are increment 4.

## 2. Where an expression is evaluated: by gBASIC

**The rule this increment turns on:** `gsql_design.md` §10H and §12 item 3 ask SQL and gBASIC to share one value semantics, and the cheapest way to make two evaluators agree is to have one. So a SQL expression is **evaluated by the interpreter's own operators**: `=`, `<`, `+`, `and`, `not` and the rest mean exactly what they mean in a gBASIC program, including three-valued logic (`unknown_nan_design.md` increment B), exact integers, money with its guard digits and its currency refusals, and date arithmetic.

**How:** the SQL layer is plain C beside the table layer (`src/gsql/sql_*.c`), and it reaches values only through a **host**, an interface of a dozen operations on opaque values (load from v3 bytes, store to them, apply a binary or unary operator, read a field, call a function, cast, test truth, order two values). The product's host is the interpreter (`src/modules/gsql.c`, compiled into `src/eval.c` as the other modules are), which applies each operator by evaluating a two-operand gBASIC expression. A second copy of gBASIC's arithmetic in C would be a second thing that can disagree, and this tree tripwires against exactly that.

**What is SQL's rather than gBASIC's,** each written in C over the host's answers, because SQL defines it and gBASIC has no counterpart:

| construct | meaning |
|---|---|
| `IS [NOT] NULL` / `IS [NOT] UNKNOWN` | never unknown itself |
| `a \|\| b` | text concatenation; `NULL` if either side is, and a non-text side is refused rather than rendered (gBASIC's `+` would render `unknown` into the text, D2) |
| `x [NOT] IN (a, b, …)` | `x = a OR x = b …`, so `1 IN (2, NULL)` is unknown, SQL's answer |
| `x [NOT] BETWEEN a AND b` | `x >= a AND x <= b` |
| `x [NOT] LIKE p` | `%` any run, `_` one character, no escape character yet |
| `CASE WHEN … THEN … [ELSE …] END` | the first branch whose condition is **true**; unknown is not true |
| `COALESCE`, `NULLIF` | standard |
| `WHERE` | keeps a row whose condition is **true**, so unknown drops it; a condition that is not a boolean is refused, naming its kind |

**Deviations from PostgreSQL, from gBASIC's semantics, each deliberate (§4.0's rule):**

- `5 / 2` is `2.5`: there is no integer division, because a gBASIC number is one kind.
- `'a' + 1` and `'1' = 1` are refused (§9: no implicit casts). gBASIC itself answers them, so the SQL layer checks the operand kinds before handing an arithmetic or ordering operator to the host.
- `<>` and `!=` are both accepted and mean gBASIC's `!=`.
- `ORDER BY … DESC` puts `NULL` first and `ASC` puts it last, which is PostgreSQL's default and is also what the index key order gives (`gsql_storage_design.md` §3.2), so a sort and an index scan cannot disagree.

## 3. The pieces

```
text --sql_parse.c--> statement tree --sql_plan.c--> plan --sql_exec.c--> rows
                          (pure)        (catalog)          (host)
```

- **`sql_parse.c`** turns text into a statement tree with a **position** (a 1-based character offset) on every node, so an error names where. It needs nothing but the text, so its tests need no store and no interpreter.
- **`sql_plan.c`** binds names against the catalog (through the table layer) and chooses an access path (§5). It needs a transaction, for the catalog, and no host: constants are evaluated when the statement runs, so a plan is a structure and never holds a value.
- **`sql_exec.c`** runs a plan through the host, and is the owner's service (`writes`, `run`, `fetch`, `close`, `prepare`).

## 4. Results

A `SELECT` answers `ROWS`: `columns` is an array of `{ name, kind }` records (the column's kind for a column reference, `"any"` for a computed value), and each row is an **array** of values in column order, so a batch carries no repeated names. Other statements answer `DONE` with `rows` affected (`CREATE` and `DROP` affect 0).

**A statement either completes or changes nothing,** in autocommit (the owner aborts its transaction). **In an explicit transaction a statement that fails part way** (row 5 of an `UPDATE` meets a duplicate after rows 1 to 4 were changed) cannot be undone, since the engine has no savepoints, so it **fails the transaction**: every later statement answers `FAILED` until `ROLLBACK`, PostgreSQL's rule. A statement refused before it wrote anything (a syntax error, an unknown column, a constant of the wrong kind) leaves the transaction usable, the table layer's promise kept.

## 5. Access paths and `EXPLAIN`

`WHERE` is split into its `AND`ed conjuncts. A conjunct `column op constant`, where the constant uses no column (a literal, a parameter, a cast, or arithmetic on them), may be **consumed by an access path**:

1. **Point read:** equality on every primary-key column.
2. **Index seek:** equality on a prefix of an index's (or the primary key's) columns, optionally followed by one range (`<`, `<=`, `>`, `>=`, `BETWEEN`) on the next column. A unique index with equality on all its columns is a point read through the index.
3. **Scan:** the rows tree in primary-key order.

The path with the most consumed columns wins, the primary key on a tie. `ORDER BY` plain columns that an index scan already yields, forwards or backwards, is not sorted again.

**The seek only narrows; the filter decides.** Every conjunct, consumed or not, is still evaluated on every row the path yields. So a seek may return a superset (an unknown or NaN bound, which key order places among real values) and the answer is still exactly the scan's. This is what makes the acceptance test of `gsql_design.md` §14 ("index seek plus residual filter produces identical results to a full scan") true by construction, and the suite checks it anyway, by running each query both ways.

**Where the scan answers, the path gives the same answer.** The guarantee is one-directional, and the differential tier found why: a path reads fewer rows, so a condition that raises on some row the path never reads (a comparison across two currencies) refuses the statement in the scan and is never evaluated by the path. PostgreSQL behaves the same way. That direction is allowed and counted; a path that refuses where the scan answers is a defect, since the path's rows are a subset of the scan's.

**A seek is used only where key order is gBASIC's `<`.** That holds when the bound's kind is the column's: a number for `number`/`integer`, text for `string`, a date for `date`, and so on. A bound of another kind is left to the filter. **An untyped `money` column gets equality seeks only:** its keys group by currency, so a range seek would silently skip other currencies where the scan raises gBASIC's cross-currency refusal, and the two must not disagree.

**`EXPLAIN`** answers one row per plan line, for example:

```
Index Seek on orders using orders_by_customer (customer = :c)
  Filter: total > 100
  Order: posted DESC (sorted)
  Limit: 10
```

It names the index actually used and lists as `Filter` only the conjuncts the path did not consume, so a reader can see whether a function call kept an index from narrowing (`gsql_design.md` §5.2). It never implies an index was used when it was not.

## 6. Errors

Every refusal answers a code and a message, and a parse or binding error a **position**. Two codes are added to the protocol's set (`gsql_owner_design.md` §2.2):

- **`SYNTAX`**: the text does not parse. It names the token and its position.
- **`EXPRESSION`**: an expression could not be evaluated: a raise inside gBASIC's operator (division by zero, a cross-currency sum), a kind refused by §2's no-implicit-cast rule, or a `WHERE` that is not a boolean. The message is gBASIC's own sentence where gBASIC raised.

An unknown table, column or index is `NOT_FOUND`, and the table layer's codes (`DUPLICATE`, `NOT_NULL`, `TYPE`, …) pass through unchanged.

## 7. How it is tested

`tests/run_gsql_sql.sh`:

- **parse**: a corpus of statements, each printed back in canonical form, and refusals, each with its position, with no store and no interpreter.
- **semantics**: statements run in process against the interpreter's host, each result written by hand, including the three-valued, exactness and money cases, and the refusals beside their legal neighbours.
- **differential**: random rows, random predicates, each query run with access paths on and forced to a full scan; the answers must be identical, and a census floor requires that seeks really happened.
- **explain**: the access path the planner says it chose, for queries whose right answer is known.
- **owner**: `gbasic --gsql-owner` started by the client, end to end.

## 8. What is not done yet

- **A warning from inside SQL** (an overflowing sum, 2112) goes to the owner's stderr, which is its client's, once per distinct message. Carrying it to the client as data, so a program can see it with `on warning`, needs a field in `ROWS` and `DONE`.
- **Plans are not cached.** A statement is parsed and planned on each `QUERY`; a prepared statement saves only sending the text. Plan caching with schema-version invalidation (`gsql_design.md` §13 question 8) comes with the gBASIC surface, which is where repeated statements come from.
- **A sort is in memory.** `ORDER BY` that no index yields collects every row first; spilling to disk is Phase 2 (`gsql_design.md` §5.1).
- **`EXPLAIN` writes a seek's range with inclusive bounds**, so `WHERE id > 5` shows `(5 <= id)` and no `Filter` line, though `id > 5` is still checked on every row and the answer is right. The seek itself is inclusive, a superset the filter narrows (§5); what is wrong is that the consumed conjunct reads as though it were the whole condition. Found 2026-10-30 while adding aggregates' `EXPLAIN` lines, and pinned as it stands by the explain cases.
- **`IN`, `IS NULL` and `LIKE 'prefix%'` never narrow an access path**; only `=`, the ordering operators and `BETWEEN` do.
- **Kinds are checked as values meet**, so `1 IN (1, 'a')` answers before it reaches `'a'`, where PostgreSQL refuses the statement before running it. Checking kinds at bind time needs the parameters' kinds, which `PREPARE` does not yet ask for.
- **Windows:** the owner is fork, poll and a local socket, so `gbasic --gsql-owner` answers that G-SQL's owner is not yet available there.
