# The `gsql` module and its blocks: G-SQL from a program (Phase 1, increment 4)

**Status:** built 2026-10-30 (`src/modules/gsql.c`, `tests/run_gsql_module.sh`). This is increment 4 of `gsql_design.md` §11, Phase 1, in two halves. The first is a module a program loads, with the same verbs the `sqlite`, `pg` and `odbc` modules have (§1–§5). The second is the `database` and `sql` blocks of `gsql_design.md` §2, built on the module (§7).

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

- **A block holds one statement.** `gsql_design.md` §2 proposes per-statement results for a block of several, with a `RETURN` to choose one; nothing has needed it yet, and a second statement is refused at load time by G-SQL's parser.
- **A block's plan is not cached.** The owner parses and plans each request; a prepared statement (`PREPARE` is in the protocol) is the obvious next step and is not measured as needed.
- **Two connections in one program can wait on each other.** The owner has one writer, and a second connection's write waits for the first's transaction to end. In one single-threaded program the first cannot end while the program waits on the second, and the client waits without a bound. By reading the owner, the owner's idle-writer timeout (5 minutes) should end the first transaction and release the second; that is **not measured**.
- **The principal is not sent.** `gsql_design.md` §6.5 says each request carries `principal()` for the audit history, and nothing in the SQL layer records one yet.
- **An owner outlives `gsql.close`** while its program runs, serving any other client; it ends with the program. An owner that exits when idle is an open item of the owner (`gsql_owner_design.md`).
- **Windows:** `load gsql` answers that G-SQL is not yet available there.

## 7. The `database` and `sql` blocks

```basic
load gsql

database shop( file: "shop.gsql", create: true )

sql cheap on shop
    select id, name, price,
           case when price < :limit then 'cheap' else 'dear'
           end as band
    from item
    where price < :limit
    order by id
end sql

limit = {USD}"1.00"
for each row, n in cheap
    print row.name
next
```

The design's sketch (`gsql_design.md` §2) is kept, with three decisions the building made.

**The block ends at `end sql`, not `end`.** SQL has an `END` of its own, a `CASE` expression's, and it is written on a line of its own as often as not. A bare `end` would end the block in the middle of the statement. `end sql` is also the shape every other gBASIC block closes with (`end window`, `end server`).

**The body is one token.** The lexer recognises the head (`sql IDENT on IDENT` at the end of a line) and then hands the parser everything up to the `end sql` line as one `SQL_TEXT` token. So SQL's words are never gBASIC's, and the grammar costs nothing: both heads are lexer triggers, as `gsql_design.md` §2.1 priced them, and bison still reports zero conflicts. `database` and `sql` stay ordinary names everywhere else (a tier asserts a function, a variable and a field so named). An `sql` block with no `end sql` is reported as `unclosed 'sql' block opened on line N`, in the words the prompt already reads as "give me the rest".

**Parameters come from the frame the block runs in.** Each `:name` is read from the variable of that name where the block runs, every time it runs. So a block used inside a function reads that function's variable, and re-running a block after changing the variable rebinds it, as §2 asks. This is how a `window` form reads its subject, and gBASIC has no closures, so it is also the only reading a block could have. A parameter with no variable is refused naming it (`PARAMETER`). `$1` is refused at load time, since nothing could supply it.

### 7.1 What each block is

| block | binds | which is |
|---|---|---|
| `database NAME( file: "…"[, create: true] )` | a `gsql_connection` | the kind `gsql.connect` answers, so every verb takes it, dynamic SQL included (`gsql.query(shop, sql_text)`, the §10G requirement that both entry points share one engine) |
| `sql NAME on DB` … `end sql` | a `gsql_statement` | one statement and the name of its database |

**A declared database connects on first use.** Declaring one starts no owner and creates no file. The fixture asserts the file is absent until the first block runs, and that closing a database never used creates nothing either. The head's options are a closed set of literals, the server block's rule and for its reason: the block is registered before anything runs, so there is nothing to evaluate an expression in. A database chosen at run time is `gsql.connect`.

**A block finds its database by name in the global frame**, where the `database` block bound it, each time it runs. So assigning over that name is reported for what it is ("no longer holds a database"), not as a type error inside the module.

### 7.2 Running a block

- **`for each row in block`** runs a `select` and hands over one row per pass. Rows arrive a batch at a time (500, as `gsql.query` fetches them), so a result is never held whole. **The loop owns the cursor**, as `for each line in f` owns its file: `break`, `return`, a named `break` to an outer loop, and a raise all end it, and the owner's cursor is closed. A failure partway, such as the owner lost, raises after the rows already handed over; what the loop did with them stays done.
- **`gsql.query(block)`** answers every row as an array, and **`gsql.exec(block)`** answers `{ command, rows_affected }`. Each takes the block alone: its parameters are its frame's, so a second argument is refused.
- The module's refusals carry over and name the block (`sql cheap: …`). `exec` over a `select` block is refused and says to iterate it. `query` or `for each` over a block that returns no rows is refused and **says it ran**, with its count. That matters more here than for `gsql.query`, because a loop is the natural way to "run" a block.

### 7.3 Checked when the program loads

Everything decidable without a database is checked in the load-time pass (`src/frontend.c`), reported into the same sink as parse errors and under its own code (`GB_DIAG_GSQL_BLOCK`), so it reaches `--json-diagnostics`. That covers:

- **the statement itself**, parsed by G-SQL's own parser, with a syntax error located at its line and column in the program; a place at the statement's end is shown just after its last character, not on the `end sql` line;
- that `on NAME` names a `database` block in the file (declared above or below);
- the head's options;
- one declaration per name;
- `$n` parameters;
- that the block is a declaration, at the top level or directly in `program`.

**Whether the program loads `gsql` is checked at run time, before anything runs**, not at load time. `--add-loads`, the tool that adds a missing load, runs the same load-time pass first, and a file that pass refused could never be fixed by it. The check reads the program's text (a `load gsql` at the top level or in `program`) because without a `program` block declarations bind as they are reached, and a reach-time check alone would let every statement above the block run first. That ordering was found by the tier, which asserts nothing ran.

**The blocks joined the pre-registration set** (`register_hoistable_declarations`), so a block may sit below `program main` and be used inside it. `tests/run_pre_registration.sh` went red naming both sets before anything was committed. gBASIC Studio's hoisting rule mirrors that set and must gain both kinds (it is a separate project).

### 7.4 How it is tested

`tests/run_gsql_module.sh` gained these tiers:

- **blocks:** `tests/gsql/blocks_test.bas`, 51 checks, every answer written by hand.
- **cursor:** asserts the requests a loop sends. No program can see a cursor, so the module writes one line per request under `GBASIC_GSQL_TRACE=1`. 1,200 rows iterated to the end are `QUERY FETCH FETCH` with nothing to close, which is the control. A `break` after one row is `QUERY CLOSE`, and so is a raise out of the loop body.
- **load:** ten refusals, each with the line and column it must name, a phrase of its cause, and nothing run; plus `--json-diagnostics`.
- **need:** the missing `load gsql`, refused before anything runs. `--add-loads` adds it.
- **hoist:** blocks below `program` used inside it.
- **words:** `database` and `sql` as ordinary names.
- **unclosed:** a block with no `end sql`.
- **valgrind** over the blocks fixture.

Each perturbation is recorded in the commit that shipped the tier.
