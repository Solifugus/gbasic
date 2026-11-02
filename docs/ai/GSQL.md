# GSQL — writing G-SQL code as an AI agent

G-SQL is gBASIC's own database (`load gsql`). It looks like ordinary SQL behind
an ordinary client library, and in several ways it is neither: values keep
their gBASIC kinds, SQL is declared in blocks rather than built as strings, and
nothing is ever converted for you. Read `UNLEARN.md` first for the language
itself; this page is the database layer. The human pages are
`docs/gsql_overview.md` (current state, one page), `docs/gsql_cookbook.md`
(18 recipes, every one run by the gate) and `docs/gsql_practices.md`.

## The shape of a program

```basic
load gsql

database shop( file: "shop.gsql", create: true )

sql make on shop
    create table item (name string primary key, price money)
end sql

sql add on shop
    insert into item (name, price) values (:name, :price)
end sql

sql cheap on shop
    select name, price from item where price < :limit order by price
end sql

make.exec()
add.exec({ name: "apple", price: {USD}"0.50" })
add.exec({ name: "melon", price: {USD}"3.25" })

limit {USD}= 1.00
for each row in cheap
    print(row.name + " costs " + string(row.price))
end for
shop.close()
```

That is the README's example, run by the gate. The fuller forms are
`examples/gsql_cookbook/01_first_database.bas` and
`04_database_and_sql_blocks.bas`; copy from them, not from memory.

## What will surprise you

- **SQL is written in `sql` blocks, not strings.** A block's SQL is parsed when
  the program LOADS, so a syntax error is reported at its line and column before
  anything runs. A string is for SQL assembled at run time only, and then every
  value still goes in as a parameter. Never build SQL by concatenating values.
- **`:name` reads the variable called `name` where the block runs**, each time it
  runs. Given a record, `add.exec(row)` reads each `:name` from the record's field
  first and falls back to the variable. A SQL string takes a record for `:name` or
  an array for `$1`, `$2`. There is no `?` placeholder.
- **A block's SQL cannot be built with `+` across lines.** A line continues only
  inside an unclosed `(`, `[` or `{`; a string ending in `+` at the end of a line
  does not parse. Multi-line SQL goes in a block.
- **`database shop( file: ... )` takes a literal path.** The block is registered
  before anything runs, so its options cannot be expressions. A path chosen at run
  time is `db = gsql.connect(path, { create: true })`, or a `database` block with no
  `file:` (a role) whose blocks are given a connection when used: `db.exec(add)`,
  `db.query(items)`, `db.on(items)` for a `for each`
  (`11_practice_a_database_per_tenant.bas`).
- **A library may hold its own blocks**, written directly in `library ... end
  library` beside its functions, which reach them by bare name. Its `on shop` is
  its own `shop`, and a program's block of the same name is a different one.
  From outside: `store.cheap`. A library holding blocks does its own `load gsql`.
- **Handles have methods:** `db.query(...)`, `db.exec(...)`, `db.begin()`,
  `db.commit()`, `db.close()`, `add.exec()`, `cheap.query()`. Each is the same call as
  `gsql.verb(handle, ...)`.
- **`query` answers rows, `exec` answers what happened.** `x.query()` is an array of
  records, fields in select-list order. `x.exec()` runs anything and answers
  `{ command, rows_affected }`, plus `rows` when the statement returns rows. `query`
  on a statement that returns no rows is refused, saying the statement ran.
  `for each row in block` streams a large result a batch at a time.
- **A declared block that nothing runs is warning 2121.** A block runs only when
  `.exec()`, `.query()` or `for each` reaches it.
- **Nothing converts in a comparison or in arithmetic.** `where id = '1'` against
  an integer column is refused as `EXPRESSION` ("= compares a number with text").
  `'a' + 'b'` is refused; text joins with `||`. CAST one side explicitly.
- **Storing does convert, into the column's declared kind.** Text stored into a
  `date`, `time`, `datetime` or `money(USD)` column is read as that kind, and a number
  into `money(USD)` takes the column's currency. A bare `money` column has no
  currency to give, so it refuses text: declare `money(USD)`.
- **An `integer` column refuses an inexact number,** including `6 / 2`, which is
  3.0 in gBASIC. It is refused rather than rounded.
- **A refusal is checked when a row reaches it.** `where name = 1` over an empty
  table answers nothing; the same query refuses once a row exists. Test with rows.
- **Text order is gBASIC's**: `'A' = 'a'` is false, and the order is not a
  collation.
- **NULL sorts last ascending and first descending**, as an index stores it.
  `NULLS FIRST` and `NULLS LAST` are refused.
- **A block of several statements is one transaction.** Separate them with `;`.
  The block runs all of them or none, joining the program's transaction if one is
  open. Only the last may be a `select`, and it answers.
- **The database is served by an owner process**, started by the first program to
  connect and stopping by itself once nobody is connected (3 seconds unless
  `linger:` says otherwise). If it exits mid-request, the request fails with
  `LOST`, and nothing is retried. The next call starts a new owner.

## Errors: catch by code

A refusal raises an ordinary gBASIC error, with G-SQL's code in
`error.details.code` and, where the statement has one, a position in
`error.details.position`. The codes you will meet: `SYNTAX`, `NOT_FOUND`,
`EXPRESSION`, `TYPE`, `DUPLICATE`, `NOT_NULL`, `FOREIGN_KEY`, `CHECK`,
`SERIAL`, `UNSUPPORTED`, `STATE`, `LOST`. Catch in the frame that can act:

```basic
function insert_once(id)
    on error goto next
    db.exec("insert into t (id) values ($1)", [id])
    e = error            ' the snapshot claims it: code, message, details, trace
    if e then
        if e.details.code = "DUPLICATE" then
            return false
        end if
        on error stop
        error e          ' anything else is not ours to swallow: raise it again
    end if
    return true
end function
```

Only bare `error` acknowledges: `if error then`, or `e = error`, which also keeps
a snapshot to look at after. Reading `error.message` alone leaves the error
pending. `error e` raises a snapshot again with its code and trace, after
`on error stop` so this frame does not catch it a second time (`UNLEARN.md`,
error handling; `tests/error_model/snapshot_reraise.bas`). A worked set of refusals and
how to read them is `10_when_sql_says_no.bas`; a write that is safe to retry is
`12_practice_a_retry_that_is_safe.bas`.

## The SQL you have

Tables with primary keys, `UNIQUE`, `NOT NULL`, defaults, `CHECK`, foreign keys
onto a primary key, `SERIAL` and `RANDOM` keys (`05_keys_serial_and_random.bas`);
`ADD COLUMN`, `RENAME TO` and views (`08_views_and_changing_the_schema.bas`);
`INNER`, `LEFT` and `CROSS` joins; aggregates with `GROUP BY` and `HAVING`;
`DISTINCT`; `UNION`/`INTERSECT`/`EXCEPT` with `ALL`; subqueries; derived tables
and `WITH`; `INSERT ... SELECT`; `ORDER BY`/`LIMIT`/`OFFSET` (`LIMIT NULL` is no
limit); indexes and `EXPLAIN` (`09_indexes_and_explain.bas`); and, from
2026-11-02:

- **Window functions**, `row_number() OVER (PARTITION BY k ORDER BY d DESC)` and
  the rest (`16_window_functions.bas`). A window cannot stand in `WHERE`: to keep
  the latest row per key, number the rows in a derived table and filter outside
  it. That form is in the recipe.
- **`WITH RECURSIVE`**, with `CYCLE col SET looped USING route` to stop and report
  a loop (`17_recursive_queries.bas`). A recursion still adding rows after
  1 000 000 rounds is refused; a program whose recursion should run longer sets
  `db.max_recursion = 5000000` on the connection first.
- **`ROLLUP`, `CUBE`, `GROUPING SETS` and `GROUPING()`** for subtotals
  (`18_subtotals_with_rollup.bas`).

Large sorts, groupings, `DISTINCT`, set operations, derived tables, `WITH`,
`INSERT ... SELECT` and windows spill to disk on their own; you do not size
them.

## What is not there (each refused by name, so you will see a sentence)

`RIGHT JOIN` (write a `LEFT JOIN` with the tables swapped) and `FULL JOIN`;
`ON CONFLICT` / upsert, and `RETURNING`; `CREATE TEMPORARY TABLE` (use a scratch
database); `stddev`, `variance` and median (fetch the rows and use the `stats`
library); `ILIKE` (compare `lower(x)`); `NULLS FIRST`/`NULLS LAST`; named windows
(`WINDOW w AS`, `OVER w`; write the window in each `OVER`); `SEARCH` on a
recursive query; a foreign key onto anything but a primary key.

## Practices that matter

Load in batches of thousands of rows per transaction, not a commit per row
(`13_practice_load_in_batches.bas`). Back up with `db.backup(dir)` while the
database is in use, and run reports on the copy
(`14_practice_backup_and_a_reporting_copy.bas`). One database per tenant,
combined in gBASIC (`11_practice_a_database_per_tenant.bas`). The reasons and
the measurements are in `docs/gsql_practices.md`.
