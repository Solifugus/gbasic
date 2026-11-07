# The `gsql` module and its blocks: G-SQL from a program (Phase 1, increment 4)

**Status:** built 2026-10-30 (`src/modules/gsql.c`, `tests/run_gsql_module.sh`). This is increment 4 of `gsql_design.md` §11, Phase 1, in two halves. The first is a module a program loads, with the same verbs the `sqlite`, `pg` and `odbc` modules have (§1–§5). The second is the `database` and `sql` blocks of `gsql_design.md` §2, built on the module (§7).

**Current state:** `gsql_overview.md` says how G-SQL works today, in one page, with the later rulings folded in. This document records how it was designed, and is amended in place only where a later ruling reversed it.

## 1. The surface

```basic
load gsql

db = gsql.connect("shop.gsql", { create: true })
db.exec("create table item (id integer primary key, name string not null, price money)")
db.exec("insert into item (id, name, price) values (:id, :name, :price)",
          { id: 1, name: "apple", price: {USD}"0.50" })
for each row in db.query("select id, name, price from item where price < $1", [{USD}"1.00"])
    print row.name
next
db.close()
```

| call | answers |
|---|---|
| `gsql.connect(path [, { create: true }])` | a `gsql_connection`. Without `create`, a database that is not there is refused, saying how to make one |
| `db.query(sql [, parameters])` | an array of records, one per row, fields in select-list order |
| `db.exec(sql [, parameters])` | `{ command, rows_affected }`, the shape `sqlite`, `pg` and `odbc` answer; `rows_affected` is `nothing` for a statement with no count, such as `CREATE` |
| `db.insert(table, rows)` | `{ command: "INSERT", rows_affected }`; rows are records, all or nothing (§8) |
| `db.columns(sql [, parameters])` | `[{ name, kind }]`, the result columns from the plan, nothing run (§9) |
| `db.close({ wait })` | whether the owner had stopped within `wait` (§10) |
| `gsql.begin(db [, "read" or "write"])` | `true`. `"write"` is the default |
| `db.commit()`, `db.rollback()` | `true` |
| `db.close()` | `true`. A closed connection is refused by every verb, `close` included |

**Parameters** are a record for `:name` and an array for `$1`, `$2`, …, the SQL layer's two forms (`gsql_sql_design.md` §1). Anything else is refused before anything is sent.

## 2. How a call works

`gsql.connect` reaches the database's owner, or starts one: the owner is this interpreter run as `gbasic --gsql-owner`, detached from the program, and it stops by itself once nobody has been connected for its linger (`gsql_owner_lifetime_design.md`). Every other call is one request over the protocol, or for a long result a request and its `FETCH`es.

**Requests and replies are gBASIC values on both sides.** A request body is a record the interpreter builds and `serialize`s; a reply is `deserialize`d back into one. So a parameter and a column value cross by the same codec rows are stored in (`serialize` version 3), and an exact integer, a money amount with its currency, a date, a boolean and `unknown` arrive as what they were. There is no conversion table, because there is nothing to convert.

**A result is fetched whole.** Rows come in batches of 500 and `query` asks for every batch before it answers, so a program holds the whole result at once. The lazy iterator `gsql_design.md` §2 asks for belongs to the `sql` block.

**`NULL` comes back as `unknown`**, G-SQL's rule (`gsql_design.md` §3.1): a bound `nothing` is stored as unknown, so that is what a read returns. (`sqlite` answers `nothing` for its own NULL.)

## 3. Refusals

Every refusal from G-SQL raises with `error.source = "gsql"`, `error.code = 2005`, and **`error.details.code` naming G-SQL's own code** (`SYNTAX`, `EXPRESSION`, `NOT_FOUND`, `DUPLICATE`, `NOT_NULL`, `TYPE`, `READ_ONLY`, `STATE`, `LOST`, …), so a program can tell a duplicate key from a lost owner without reading the message. A refusal with a place in the statement adds `error.details.position`, a 1-based character offset. The message names the statement, through the formatter `sqlite`, `pg` and `odbc` share.

Three refusals are the module's own:

- **`exec` over a statement that returns rows** is refused, and its cursor closed. Nothing was changed. *Since 2026-11-01* `exec` answers the rows instead (`gsql_store_conversion_design.md` §2).
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
- **lifetime:** a second program keeps its connection when the program that started the owner exits; `linger: 0` stops the owner at the last close; a program's output is not held open by its owner.
- **leftovers:** no owner outlives the suite.

## 6. Not done yet

- **A block may hold several statements** since 2026-11-01 (`gsql_multi_statement_design.md`): the last one answers, a `select` may only be last, and the block is one transaction. `gsql_design.md` §2's `RETURN` to choose a result is not needed for that; several results by name are recorded there as later work.
- **A block's plan is not cached.** The owner parses and plans each request; a prepared statement (`PREPARE` is in the protocol) is the obvious next step and is not measured as needed.
- **Two connections in one program can wait on each other.** The owner has one writer, and a second connection's write waits for the first's transaction to end. In one single-threaded program the first cannot end while the program waits on the second, and the client waits without a bound. By reading the owner, the owner's idle-writer timeout (5 minutes) should end the first transaction and release the second; that is **not measured**.
- **The principal is not sent.** `gsql_design.md` §6.5 says each request carries `principal()` for the audit history, and nothing in the SQL layer records one yet.
- **An owner outlives `gsql.close`** for its linger, serving any other client, and outlives the program that started it while another program uses it (`gsql_owner_lifetime_design.md`).
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
| `database NAME( file: "…"[, create: true] )` | a `gsql_connection` | the kind `gsql.connect` answers, so every verb takes it, dynamic SQL included (`shop.query(sql_text)`, the §10G requirement that both entry points share one engine) |
| `sql NAME on DB` … `end sql` | a `gsql_statement` | one statement and the name of its database |

**A declared database connects on first use.** Declaring one starts no owner and creates no file. The fixture asserts the file is absent until the first block runs, and that closing a database never used creates nothing either. The head's options are a closed set of literals, the server block's rule and for its reason: the block is registered before anything runs, so there is nothing to evaluate an expression in. A database chosen at run time is `gsql.connect`.

**A block finds its database by name in the global frame**, where the `database` block bound it, each time it runs. So assigning over that name is reported for what it is ("no longer holds a database"), not as a type error inside the module.

### 7.2 Running a block

- **`for each row in block`** runs a `select` and hands over one row per pass. Rows arrive a batch at a time (500, as `gsql.query` fetches them), so a result is never held whole. **The loop owns the cursor**, as `for each line in f` owns its file: `break`, `return`, a named `break` to an outer loop, and a raise all end it, and the owner's cursor is closed. A failure partway, such as the owner lost, raises after the rows already handed over; what the loop did with them stays done.
- **`block.query()`** answers every row as an array, and **`block.exec()`** answers `{ command, rows_affected }`, with `rows` when its last statement returns rows (since 2026-11-01, `gsql_store_conversion_design.md` §2). Each takes the block alone or **with a record of its `:name` values** (since 2026-11-01, Matthew's ruling): a field supplies its name, and a name the record lacks is read from the frame, which "has real utility".
- **On a database chosen at run time** (*since 2026-11-01*, `gsql_block_connection_design.md`): `connection.exec(block)` and `connection.query(block)` run a block on that connection, and `connection.on(block)` answers a copy bound to it, for `for each`. `database NAME()` with no `file:` declares a role, whose blocks must be given a connection.
- The module's refusals carry over and name the block (`sql cheap: …`). `exec` over a `select` block is refused and says to iterate it. *Since 2026-11-01* it answers the rows. `query` or `for each` over a block that returns no rows is refused and **says it ran**, with its count. That matters more here than for `gsql.query`, because a loop is the natural way to "run" a block.

### 7.3 Checked when the program loads

Everything decidable without a database is checked in the load-time pass (`src/frontend.c`), reported into the same sink as parse errors and under its own code (`GB_DIAG_GSQL_BLOCK`), so it reaches `--json-diagnostics`. That covers:

- **the statement itself**, parsed by G-SQL's own parser, with a syntax error located at its line and column in the program; a place at the statement's end is shown just after its last character, not on the `end sql` line;
- that `on NAME` names a `database` block in the file (declared above or below);
- the head's options;
- one declaration per name;
- `$n` parameters;
- that the block is a declaration, at the top level, directly in `program`, or directly in a `library` (whose blocks are its own: `docs/gsql_library_blocks_design.md`).

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

## 8. `gsql.insert`: many rows in one call (2026-11-06)

Asked for by gdash (its A3): a dashboard refresh inserts a whole dataset, one
`INSERT` a row, and at a million rows that took 48 seconds on its build.
Matthew ruled the shape: `db.insert(table, rows)` with rows as **records**
keyed by column name (as a frame's rows are, and a misordered positional row
would be a silent wrong column), and **all or nothing**.

**How it works.** The client builds one statement per chunk, `insert into
"t" ("a", "b") values ($1, $2), ($3, DEFAULT), ...`, from the union of the
rows' names in the order first given. A name is quoted, a `"` in it doubled,
so a reserved word or a space works. `DEFAULT` stands for a column a row
leaves out. A chunk holds at most 4096 parameters or about 4 MiB of values,
well under the 16 MiB frame. The chunks run inside a `begin`, which is a
transaction, or a savepoint when the caller is in one, and the first refusal
rolls back and is raised. Reusing `INSERT` means every conversion, default,
generated column, CHECK and foreign key is the one `INSERT` already has.

**Naming the caller's row.** The SQL layer now carries the refused row of a
multi-row `INSERT` as a number in its error (`GsqlError.row`, sent as `row` in
the owner's ERROR), set for every refusal of a row. Before, only a conversion
or the insert itself said "row N of VALUES" (a CHECK, NOT NULL or foreign key
said nothing of the row). `gsql.insert` turns that into the caller's own
index: chunk start plus row minus one, in the message as `rows[N]` and in
`error.details.index`. `gsql.exec` keeps the owner's message, which now says
the row for every kind of refusal in a multi-row `INSERT`.

**What measuring it first found** (the profile is callgrind's, of the owner
process, since perf is not permitted here):

- **A statement's parameters were read once per reference.** `param_bytes`
  read the whole parameter array into a stack buffer for each `$n`, so a
  statement with n parameters cost n squared: 100,000 rows took 1.9 seconds
  at 10 rows a statement and 4.0 at 100. And the buffer held 1024 items: past
  that, the read failed and EVERY parameter was reported missing ("no value
  was given for the parameter $1"), for a statement that gave them all. The
  parameters are now indexed once per execution context and shared with the
  contexts made from it (a CHECK's, a subquery's); 1000 rows a statement then
  took 1.39 seconds.
- **Every statement was parsed twice**, once by `svc_writes` to decide whether
  it writes and again by `svc_run`. The first parse is now kept for the second
  when the text is the same, and the reserved-word lookup, a linear `strcmp`
  over 56 words for every name the parser met, is a binary search (the order
  it needs is checked by `run_gsql_sql.sh`'s reserved tier). Together, the
  owner's instructions for 3,000 one-row inserts went from 542 to 423 million.
- **Not done:** the table's schema is still decoded from the catalog for every
  statement, 22% of the owner's time for a one-row insert after the above. A
  cache must respect each transaction's snapshot and the ALTER paths that
  modify a table they opened; `gsql.insert` pays it once a chunk. And the
  currency of a money value is found by a linear search over 178 currencies
  (`currency_find_alpha`), about 5% of a chunked insert.

**Measured on this machine, 2026-11-06**, building the rows in gBASIC first:
100,000 rows took 0.54 seconds past building them with `gsql.insert`, and
2.8 seconds with one `exec` a row; a million took 5.2 seconds past building
them (2.6), and the client's memory did not grow with the insert. These are
one run each, not a gate; nothing asserts them.

**Tested** by `run_gsql_module.sh`'s insert tier (`tests/gsql/insert_test.bas`,
28 checks, and under valgrind): defaults, conversion, the answer's shape, each
refusal's row and code, 5001 rows across several statements with the
duplicate in the last undoing the 5000 before it, the savepoint inside a
caller's transaction, quoted names, the method form, and an `exec` with 1500
parameters. Perturbed, each red alone: parameters capped at 1024 again, the
rollback on refusal removed, the chunk offset dropped from the index, and the
row set only for statements of several rows.

**`dbframe.to_gsql` was not changed.** It reports each refused row and undoes
it alone, which is the other answer to Matthew's question; moving it onto
`gsql.insert` would change what it promises.

## 9. `gsql.columns`: a query described, not run (2026-11-06)

Asked for by gdash (its A2, carried over from the `sqlite.columns` ask of its
design §9): a dashboard checks every encoding channel against a real result
column before it is served, and its dataset listing needs each column's kind,
without running the query.

A QUERY carrying `describe: true` is parsed and planned, and the plan's
columns are the answer; svc_run returns before LIMIT is evaluated, a table is
opened for reading, or a row is touched. The owner treats such a request as a
read whatever the statement is (it takes no writer slot and is allowed in a
`begin("read")`), and the SQL layer refuses anything that returns no rows as
STATE, so describing an INSERT runs nothing either. A parameter need not be
given, since nothing is evaluated.

**What a kind can say.** The planner types a column it reads (`number`,
`string`, `money(USD)`, `date`, ...) and the aggregates it knows (`count` is
`integer`); for most expressions, `amt * 2` or `upper(name)`, it answers
`any`. That is the planner's knowledge today, not a choice made here, and it
limits what a channel check can conclude from a computed column.

Tested by `run_gsql_module.sh`'s columns tier (`tests/gsql/columns_test.bas`,
12 checks): a table's columns and kinds, aliases, a parameter absent and
given, a query that raises when run (`1 / 0`) still described while `query`
refuses it, an INSERT refused and not run, a missing column refused as the
query would be, and both inside a read transaction. Perturbed, each red: the
describe flag ignored by the SQL layer, and the owner counting a described
INSERT as a write (refused READ_ONLY in a read transaction).

## 10. `gsql.close(db, { wait })`: knowing the owner has stopped (2026-11-06)

Asked for by gdash (its A4): after a million-row load with `linger: 0` the
program exited and its database directory could not be removed for a moment,
because the owner was still writing a sorted file. A program deleting a
database, or a test asserting its scratch directory is clean, has to wait for
that, and gdash was matching `--gsql-owner ... <path>` in the process table.

`close` already knows which process the owner is: the client's handshake
carries its pid. With `wait`, the close then polls that pid, every 20 ms, until
it is gone or the time is spent, and answers whether it is gone. The owner is
nobody's child, so `kill(pid, 0)` answers for it and there is no zombie to
reap. **It does not guess** whether the owner will stop: an owner another
connection uses, or one within its linger, simply has not stopped when the
time runs out, and the answer says so.

Tested by `run_gsql_module.sh`'s close tier (`tests/gsql/close_test.bas`, 10
checks): `linger: 0` waited for and the directory removed at once, a second
connection keeping the owner (false after a second, true at its own close),
the default linger not waited for, close without `wait` unchanged, and three
refusals that leave the connection open. Perturbed, each red: no waiting
(which is gdash's report: the owner is still running when `close` returns),
and an owner always reported gone.

**`gsql.wait_for_owner(path, seconds)`** (2026-11-07) asks the same question
of a database by its path, for a program that never held the connection: gdash's
test cleanup waits for owners that server workers, refresh children and CLI
calls started and have since left. It probes the store's lock, not the socket:
an owner removes its socket first and releases the lock last, after its final
write, so only the lock says that nothing is writing. The probe is a shared,
non-blocking `flock`, released at once; an owner starting in that moment finds
the lock busy and retries, which it does for a racing owner anyway. `flock` is
local, so an owner on another machine sharing the storage is not seen; the
lease is that case's answer. Tested in the close tier: held while a connection
is open, free once it closes, a lingering owner not free at once but free within
ten seconds, a path with no database, a refusal; red with the probe answering
"free" always.

