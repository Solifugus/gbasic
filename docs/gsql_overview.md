# G-SQL today

**Status:** Shipped. G-SQL as it stands on 2026-11-01, in one page: what a program writes, the rules it meets, and which document holds each rule's reasons and tests. The design documents record how each piece was decided and are not rewritten when a later ruling changes something. This page is, and where the two differ, this page and `docs/reference.md` are current.

## 1. What it is

G-SQL is gBASIC's own relational database. A stored value is a gBASIC value, so it comes back as what it was: an exact integer past 2^53, money with its currency, a date, `unknown` for SQL `NULL`. Every operator in its SQL is gBASIC's own, applied by the interpreter, so `=`, `+` and `<` mean in a query what they mean in a program (`gsql_design.md`, `gsql_sql_design.md`).

A database is a directory. It is served by one **owner** process, which the first program to connect starts, which serves every program connected to it, and which stops by itself a short **linger** after the last one leaves (`gsql_owner_lifetime_design.md`). It is part of every build on Linux and other POSIX systems, with nothing to install.

## 2. A program

SQL is written in **blocks**. A `database` block names the database and a `sql` block names a statement. Each `:name` is read from the variable of that name where the block runs, or from a record handed to the call. A connection and a block are **handles**, and a handle's verbs are its methods:

```basic
load gsql

database shop( file: "shop.gsql", create: true )

sql make on shop
    create table item (name string primary key, price money(USD), added datetime);
    create index item_price on item (price)
end sql

sql add on shop
    insert into item (name, price, added) values (:name, :price, :added)
end sql

sql cheap on shop
    select name, price from item where price < :limit order by price
end sql

make.exec()
add.exec({ name: "apple", price: "0.50", added: "2026-11-01 09:30" })
add.exec({ name: "melon", price: 3.25, added: "2026-11-01 09:31" })

limit {USD}= 1.00
for each row in cheap
    print row.name + " costs " + string(row.price)
next
shop.close()
```

The text `"0.50"` and the number `3.25` are stored as money, and the text `"2026-11-01 09:30"` as a date-time, because the columns say so. `make` holds two statements and runs as one transaction.

## 3. The rules a program meets

| rule | in short | where |
|---|---|---|
| **Blocks are the normal form** | Write SQL in blocks. A string is for SQL assembled at run time, with every value bound as a parameter. A block's SQL is parsed when the program loads, so a syntax error is located in the program. | Matthew's ruling; `gsql_module_design.md` §7, `gsql_practices.md` |
| **Methods** | `h.verb(args)` is `gsql.verb(h, args)`: `db.close()`, `add.exec(row)`, `db.begin()`. Both spellings are one call. | `handle_methods_design.md` |
| **Parameters** | A `:name` is read from the variable of that name where the block runs, each time it runs. Given a record, `add.exec(row)` reads each `:name` from the record's field, falling back to the variable for a field it lacks. A string takes a record for `:name` or an array for `$1`. | `gsql_module_design.md` §7; Matthew's ruling on records |
| **Several statements** | A block may hold several, separated by `;`. The last one answers; a `select` may only be last. The block runs all of them or none, joining the program's transaction if one is open. | `gsql_multi_statement_design.md` |
| **`exec` answers rows** | `x.exec()` runs anything and answers `{ command, rows_affected }`, plus `rows` when the last statement returns rows, plus `statements` for a block of several. `x.query()` is the shortcut for the rows; `for each row in x` streams a large result a batch at a time. | `gsql_store_conversion_design.md` §2 |
| **Storing converts** | Text stored into a `date`, `time`, `datetime` or `money` column is read as that kind, and a number into `money` takes the column's currency. Comparisons and arithmetic make no implicit conversion, an integer column takes text that is a whole number in digits (`'25'`, `' 25 '`, `'007'` is 7) and refuses a decimal, in text or as a number, and a number column takes text that is a number (`'2.5'`). | `gsql_store_conversion_design.md` §1, Matthew: *"the table already knows what they are"* |
| **Integers are exact** | An integer column refuses an inexact value whatever it holds, so `6 / 2`, which is 3.0, is refused. | `gsql_design.md`, the first-release rulings |
| **A database chosen at run time** | `database tenant()`, with no `file:`, is a role. Its blocks are given a connection when used: `db.exec(add)`, `db.query(items)`, or `db.on(items)` for a `for each`. | `gsql_block_connection_design.md` |
| **Refusals are codes** | Every refusal raises with `error.details.code` naming G-SQL's code (`DUPLICATE`, `NOT_NULL`, `FOREIGN_KEY`, `LOST`, ...) and a position where the statement has one. | `gsql_module_design.md` |
| **Warnings are gBASIC warnings** | A warning raised inside SQL reaches the call that ran it, so `on warning` applies; under `stop` inside a transaction it becomes an error before anything is committed. | `gsql_warning_design.md` |
| **A lost owner is not retried** | If the owner exits, the request in flight fails with `LOST` and nothing is retried; the next call starts a new owner. | `gsql_module_design.md` |

## 4. The SQL

Tables, indexes and their constraints: primary keys, `UNIQUE`, `NOT NULL`, defaults, `CHECK`, foreign keys onto a primary key, `SERIAL` (gapless, optionally per scope) and `RANDOM` keys, `ADD COLUMN`, `RENAME TO`, and views. Queries: joins (`INNER`, `LEFT`, `CROSS`), aggregates with `GROUP BY` (including `ROLLUP`, `CUBE`, `GROUPING SETS` and `GROUPING()`) and `HAVING`, `DISTINCT`, `UNION`/`INTERSECT`/`EXCEPT`, subqueries, derived tables, `WITH` and `WITH RECURSIVE` (with `CYCLE`), window functions (`row_number`, `rank`, `lag`, `lead`, aggregates `OVER`, with frames), `INSERT ... SELECT`, `ORDER BY`/`LIMIT`/`OFFSET` and `EXPLAIN`. Each has its design: `gsql_table_design.md`, `gsql_check_design.md`, `gsql_fkey_design.md`, `gsql_generated_design.md`, `gsql_rename_design.md`, `gsql_view_design.md`, `gsql_join_design.md`, `gsql_aggregate_design.md`, `gsql_rollup_design.md`, `gsql_distinct_design.md`, `gsql_setop_design.md`, `gsql_subquery_design.md`, `gsql_derived_design.md`, `gsql_recursive_design.md`, `gsql_window_design.md`, `gsql_insert_select_design.md`.

**Not yet:** a block returning several results by name (`gsql_multi_statement_design.md` §4).

## 5. Running it

| what | in short | where |
|---|---|---|
| **One owner on any storage** | On a disk shared between machines whose file locks do not reach across, a lease decides; a second machine is refused with `ELSEWHERE`. | `gsql_lease_design.md` |
| **Commits share a sync** | Concurrent commits wait for one round's sync, so eight clients commit 6.6 to 8.2 times one client's rate on a slow disk. | `gsql_group_commit_design.md` |
| **Long reads are sliced** | A sort, `GROUP BY` or aggregate runs 1 024 rows at a time, and the owner serves every other client between slices. | `gsql_slicing_design.md` |
| **Join order from row counts** | The engine estimates each table's rows, and an all-inner join over tables is read in the cheapest order it finds; `EXPLAIN` shows the order and each estimate. | `gsql_rowcount_design.md` |
| **Hash joins** | An inner side read whole is grouped once by its join column and probed per outer row. | `gsql_hashjoin_design.md` |
| **Spill** | An index build, `ORDER BY`, `GROUP BY`, `DISTINCT`, the set operations, derived tables, `WITH`, a hash join's inner side and `INSERT ... SELECT` and window functions stay within a 64 MB budget each, spilling to disk past it and answering as they do in memory. `WITH RECURSIVE` is refused past 1 GiB. An owner starting removes the spill files a crashed process left. | `gsql_spill_design.md`, `gsql_operator_spill_design.md` |
| **Backup, dump, restore** | `db.backup(dir)` copies a snapshot while the database is in use and refreshes a copy in place; `db.dump(file)` writes it as SQL, and `db.restore(file)` reads that back. | `gsql_backup_design.md`, `gsql_dump_design.md` |
| **Measured** | Reads, writes, memory and commits at 10⁵ to 10⁸ rows, and the work order that followed. | `gsql_scale_measurements.md` |

## 6. Where to start

- **Using it:** `gsql_cookbook.md`, every recipe run by the gate.
- **Using it well:** `gsql_practices.md`: a database per tenant, retries made safe, loading in batches, and the current limits.
- **Every verb and block:** `docs/reference.md`, *G-SQL Module*.
- **How it is built:** the storage engine (`gsql_storage_design.md`), the table layer (`gsql_table_design.md`), the owner and its protocol (`gsql_owner_design.md`), the SQL layer (`gsql_sql_design.md`), and the module (`gsql_module_design.md`).
