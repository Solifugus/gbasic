# The `gsql` module: G-SQL from a program (Phase 1, increment 4)

**Status:** built 2026-10-30 (`src/modules/gsql.c`, `tests/run_gsql_module.sh`). This is the first half of increment 4 (`gsql_design.md` §11, Phase 1): a module a program loads, with the same verbs the `sqlite`, `pg` and `odbc` modules have. The second half, the `database` and `sql` blocks of `gsql_design.md` §2, is built on it and is not done yet.

## 1. The surface

```basic
load gsql

db = gsql.connect("shop.gsql", { create: true })
gsql.exec(db, "create table item (id integer primary key, name string not null, price money)")
gsql.exec(db, "insert into item (id, name, price) values (:id, :name, :price)",
          { id: 1, name: "apple", price: {USD}"0.50" })
for each row in gsql.query(db, "select id, name, price from item where price < $1", [{USD}"1.00"])
    print row.name
next
gsql.close(db)
```

| call | answers |
|---|---|
| `gsql.connect(path [, { create: true }])` | a `gsql_connection`. Without `create`, a database that is not there is refused, saying how to make one |
| `gsql.query(db, sql [, parameters])` | an array of records, one per row, fields in select-list order |
| `gsql.exec(db, sql [, parameters])` | `{ command, rows_affected }`, the shape `sqlite`, `pg` and `odbc` answer; `rows_affected` is `nothing` for a statement with no count, such as `CREATE` |
| `gsql.begin(db [, "read" or "write"])` | `true`. `"write"` is the default |
| `gsql.commit(db)`, `gsql.rollback(db)` | `true` |
| `gsql.close(db)` | `true`. A closed connection is refused by every verb, `close` included |

**Parameters** are a record for `:name` and an array for `$1`, `$2`, …, the SQL layer's two forms (`gsql_sql_design.md` §1). Anything else is refused before anything is sent.

## 2. How a call works

`gsql.connect` reaches the database's owner, or starts one: the owner is this interpreter run as `gbasic --gsql-owner`, a child of the program, so it ends with the program (`gsql_owner_design.md` §6). Every other call is one request over the protocol, or for a long result a request and its `FETCH`es.

**Requests and replies are gBASIC values on both sides.** A request body is a record the interpreter builds and `serialize`s; a reply is `deserialize`d back into one. So a parameter and a column value cross by the same codec rows are stored in (`serialize` version 3), and an exact integer, a money amount with its currency, a date, a boolean and `unknown` arrive as what they were. There is no conversion table, because there is nothing to convert.

**A result is fetched whole.** Rows come in batches of 500 and `query` asks for every batch before it answers, so a program holds the whole result at once. The lazy iterator `gsql_design.md` §2 asks for belongs to the `sql` block.

**`NULL` comes back as `unknown`**, G-SQL's rule (`gsql_design.md` §3.1): a bound `nothing` is stored as unknown, so that is what a read returns. (`sqlite` answers `nothing` for its own NULL.)

## 3. Refusals

Every refusal from G-SQL raises with `error.source = "gsql"`, `error.code = 2005`, and **`error.details.code` naming G-SQL's own code** (`SYNTAX`, `EXPRESSION`, `NOT_FOUND`, `DUPLICATE`, `NOT_NULL`, `TYPE`, `READ_ONLY`, `STATE`, `LOST`, …), so a program can tell a duplicate key from a lost owner without reading the message. A refusal with a place in the statement adds `error.details.position`, a 1-based character offset. The message names the statement, through the formatter `sqlite`, `pg` and `odbc` share.

Three refusals are the module's own:

- **`exec` over a statement that returns rows** is refused, and its cursor closed. Nothing was changed.
- **`query` over a statement that returns none** is refused, and **the message says the statement ran**, with its row count. In autocommit the change is committed by then, so a refusal that read as "nothing happened" would be false.
- **Two result columns with one name** are refused, since a row is a record and a record would keep one of them silently. The message names the remedy, `AS`.

**When the owner exits** under a program, the request in flight fails with `LOST`, saying nothing was retried; any transaction is gone with it. The next call reconnects, starting a new owner, which recovers what was committed. Retrying is the program's decision, the only safe one for a write (`gsql_design.md` §6.5).

## 4. What building it found

**A counted loop's counter was not an exact integer.** `for i = 1 to 3` rebuilt its counter from a double every pass, so the first program that put a loop counter into a table was refused by the table layer, which refuses an inexact value in an integer column whatever it holds (the 2026-10-29 ruling). It was a defect in the language and not in G-SQL: `i + 2^53` came out one short, and a loop starting at 2^53 never ended, because in doubles 2^53 + 1 is 2^53 and the counter stopped moving. The counter is exact now whenever its start and step are (`tests/exact/exact_test.bas`).

## 5. How it is tested

`tests/run_gsql_module.sh`, self-checking, every answer written by hand in `tests/gsql/module_test.bas`:

- **semantics:** values in both directions, `exec`'s answer shape, a 1,234-row result across three batches, transactions and a second connection's isolation, and each refusal with its code;
- **lost:** the runner kills the owner with SIGKILL while the program waits, and the program must see `LOST`, then reach a new owner that recovered the committed rows;
- **valgrind** over the semantics phase;
- **leftovers:** no owner outlives the program that started it.

## 6. Not done yet

- **The `database` and `sql` blocks** of `gsql_design.md` §2, with a lazy row iterator and per-statement results.
- **Two connections in one program can wait on each other.** The owner has one writer, and a second connection's write waits for the first's transaction to end. In one single-threaded program the first cannot end while the program waits on the second, and the client waits without a bound. By reading the owner, the owner's idle-writer timeout (5 minutes) should end the first transaction and release the second; that is **not measured**.
- **The principal is not sent.** `gsql_design.md` §6.5 says each request carries `principal()` for the audit history, and nothing in the SQL layer records one yet.
- **An owner outlives `gsql.close`** while its program runs, serving any other client; it ends with the program. An owner that exits when idle is an open item of the owner (`gsql_owner_design.md`).
- **Windows:** `load gsql` answers that G-SQL is not yet available there.
