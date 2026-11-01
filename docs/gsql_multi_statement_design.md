# G-SQL: a block of several statements

**Status:** Shipped 2026-11-01 (`gsql_block_split`, `gsql_block_prelude` and `gsql_block_start` in `src/modules/gsql.c`; the load-time check in `src/frontend.c`; `tests/gsql/blocks_test.bas` and `run_gsql_module.sh`'s load tier). Designed with Matthew the same day. It settles what `gsql_design.md` §2 left open ("multiple statements need defined result behavior") and `gsql_module_design.md` §7 deferred ("a block holds one statement").

## 1. The gap

A block holds one statement. A program's schema, a migration, or "record this, then show me" is several statements, so it is several blocks, each named and each run by its own call. The rewritten cookbook declares two to five blocks per recipe for setup alone.

## 2. The rule

**A block may hold several statements, separated by `;`. Its answer is its last statement's, and a `select` may only be last.**

| statement | its result |
|---|---|
| `create`, `drop`, `alter` | `{ command }` |
| `insert`, `update`, `delete` | `{ command, rows_affected }` |
| `select`, `explain` | rows |

- **Every statement before the last changes something and returns no rows.** A `select` or `explain` anywhere but last is refused when the program loads, since its rows would go nowhere.
- **A block ending in a `select` is a query:** `for each row in block` streams it and `gsql.query(block)` answers its rows, after the statements before it have run.
- **`gsql.exec(block)` answers the last statement's `{ command, rows_affected }`, plus `statements`,** one `{ command, rows_affected }` per statement in order, so the earlier ones' counts are not lost. *Since the same day* it also answers the last statement's `rows` when it returns them, rather than refusing (`gsql_store_conversion_design.md` §2).
- **The block is one transaction.** It runs all of its statements or none. If the connection already has a transaction open, the block runs inside it and leaves committing to the program; otherwise it begins its own and commits it.
- **`:name` parameters work across all of them,** each read where the block runs.

```basic
sql schema on shop
    create table customer (id integer primary key, email text not null unique);
    create index customer_email on customer (email)
end sql
gsql.exec(schema)

sql record_and_show on shop
    insert into visit (page) values (:page);
    select page, count(*) as n from visit group by page order by n desc
end sql
for each row in record_and_show
    print row.page + " " + string(row.n)
next
```

## 3. Decisions

- **The last statement answers, rather than a collection.** A collection makes every caller dig out the one result it wanted. `gsql_design.md`'s proposal added a `RETURN` to choose one; "a select only last" does the same job with no new SQL.
- **Whether a transaction is open is asked of the owner, not remembered by the program:** the block sends `BEGIN`, and a refusal saying one is already open means it joins it. A flag kept in the program goes wrong when the owner ends an idle transaction on its timeout.
- **A failure stops the block.** Inside its own transaction, the block rolls back and raises, naming the statement (its number and its text). Inside the program's, it raises and the program decides, as for any statement in a transaction.
- **Each statement is still one request** to the owner, so a long block shares the owner fairly with other clients, as any transaction does.
- **A string still holds one statement.** `gsql.exec(db, text)` is unchanged; a script of several statements is what `gsql.restore` is for.

## 4. Later: several results by name

Matthew, 2026-11-01: a future release may let a block **return several results by name**, its value then being a record, one field per result:

```sql
return deposits as select * from deposit where day = :day;
return withdrawals as select * from withdrawal where day = :day
```

Nothing here closes that off. Under this design a `select` may only be last; named returns would allow `return NAME as select ...` anywhere, and a block holding any would answer a record. A block without them answers exactly as in §2.

## 5. Tests

In `tests/gsql/blocks_test.bas` and `run_gsql_module.sh`'s load tier:

- **A schema in one block:** two tables and an index, then each used.
- **`exec` answers the last statement and `statements` in order,** with each count.
- **A block ending in a select** streams after its insert ran, and `gsql.query` answers the same rows.
- **All or none:** a block whose second statement fails leaves the first undone, in its own transaction; inside the program's transaction, the program's rollback undoes it and its commit keeps it.
- **Refused before anything runs:** `query` of a block ending in no rows, checked to have changed nothing. (`exec` of one ending in a select was refused too, until `exec` learned to answer rows.)

**What building it decided:** a block whose last statement is a `select` keeps its changes (commits its own transaction) **before** its rows are read, so the rows stream as any query's do rather than holding a write transaction open for the life of a loop. **Perturbations proven red:** the block's own transaction taken away, never joining the program's, and the up-front refusal removed.
- **Load-time refusals:** a `select` that is not last, naming its place; an empty statement (`;;`).
- **Parameters across statements,** read from a function's own variables.
