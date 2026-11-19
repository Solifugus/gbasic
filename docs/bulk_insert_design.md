# Bulk insert into PostgreSQL and ODBC databases

**Status:** Shipped 2026-11-18 on master, for 0.13.0 (§6 says what building it found). It is G2 of
`docs/enterprise_data_review.md`, ruled for 0.13.0 the same day.

## 1. The gap

`pg` and `odbc` insert one row per statement, so loading 10⁶ rows is 10⁶
round trips. Neither uses its database's bulk path:

- PostgreSQL's `COPY ... FROM STDIN` (`PQputCopyData`);
- ODBC's parameter arrays (`SQL_ATTR_PARAMSET_SIZE`).

That was checked in `src/eval.c` on 2026-11-18. Loading a table is the most
ordinary job in enterprise data work.

## 2. The verbs: `gsql.insert`'s contract, in each module

```basic
r = pg.insert(conn, "invoice", rows)       ' also conn.insert("invoice", rows)
r = odbc.insert(conn, "invoice", rows)
```

**`rows` is an array of records keyed by column name**, as a frame's rows
are. The answer is `{ command: "INSERT", rows_affected }`, as from
`gsql.insert`.

**Every row names the same columns.** This is the one difference from
`gsql.insert`, which lets a row leave out a column so it takes its default.
`COPY` and a parameter array both take one column list for the whole batch.

- A row naming other columns than the first row is **refused before anything
  is sent**, naming both rows' columns.
- A column every row leaves out still takes its default.
- A row wanting NULL says `unknown`.

Guessing NULL for a column a row left out would turn a typo into a silent
NULL. Splitting the batch by column set would hide a 10x slowdown.

**All or nothing.** One refused row undoes the whole call. The error names
the caller's row, `pg.insert: rows[1234]: ...`, with `error.details.index`
1234 (0-based, as `gsql.insert` counts). **Inside the caller's transaction:**

- `pg.insert` is a savepoint (`SAVEPOINT`, then `RELEASE` or
  `ROLLBACK TO`), so only the call is undone.
- `odbc.insert` cannot assume a dialect's savepoint syntax, so a refused call
  leaves the caller's transaction for the caller to roll back, as a refused
  `odbc.exec` does. It says so in the message.

**The table name** is an identifier, quoted by the module. It may be
`schema.table`. It is never pasted into SQL unquoted.

**An empty array** inserts nothing and sends nothing.

## 3. PostgreSQL: `COPY`

1. **The column types.** The module describes
   `SELECT <cols> FROM <table> LIMIT 0` (`PQprepare` and `PQdescribePrepared`,
   which send no rows). That also refuses an unknown table or column, by
   name, before any data moves.
2. **Each value's text is the parameter text `pg.exec` already sends**
   (`pg_parameter_text`). An array column gets a Postgres array literal; JSON
   gets JSON; a date or instant gets its UTC text. `unknown` and `nothing`
   become `\N`. Then come `COPY`'s text-format escapes: backslash, tab,
   newline and carriage return. So a value means the same thing through
   `insert` as through `exec`, and a text holding a NUL is refused as `exec`
   refuses it.
3. **`COPY <table> (<cols>) FROM STDIN`**, its rows in buffers of about 1 MiB,
   then `PQputCopyEnd`. `COPY` is one statement, so outside a transaction it
   is atomic by itself.
4. **Which row failed.** Postgres's error context reads
   `COPY invoice, line 1235, column amount: ...`. Line N is
   `rows[N - 1]`. If that field is missing, the message says so rather than
   guessing a row.

## 4. ODBC: parameter arrays

1. **`INSERT INTO <table> (<cols>) VALUES (?, ...)`**, prepared once.
2. **Column-wise binding.** Each column has one C type and one SQL type,
   taken from its first value that is not `unknown`, using `odbc_bind_value`'s
   choices:
   - BIT for a boolean;
   - DECIMAL with a scale for money and exact decimals, the scale being the
     largest any row needs;
   - DOUBLE for other numbers;
   - WVARCHAR or WLONGVARCHAR for text, so Unicode survives FreeTDS;
   - the date and time texts.

   A later row whose value is of another kind (text where the column had
   numbers) is refused before sending, naming the row and the column.
3. **Batches** of up to 1 000 rows (`SQL_ATTR_PARAMSET_SIZE`), with
   `SQL_ATTR_PARAM_STATUS_PTR` saying which rows failed.
4. **Atomicity.** With autocommit on, the call turns it off, runs every
   batch, then commits, or rolls back at the first failed row, and restores
   autocommit.
5. **A driver without parameter arrays.** Some refuse `PARAMSET_SIZE` above 1
   with `HYC00`. Then the same prepared statement runs once per row, in the
   same transaction. That is still atomic, and still saves re-preparing; it
   is only slower. **That is measured, not assumed, per driver** (§5).

## 5. How it is tested

- **The contract, both modules.** Round trips of every kind: number, exact
  decimal, money, text with tab, newline, backslash and Unicode, boolean,
  date, instant, `unknown`, and a Postgres array.
- **Refusals before sending:** different column sets, a mixed-kind column,
  not a record, an unknown table or column.
- **A refused row.** The error names its index, and nothing of the call is
  kept. Inside a transaction, the savepoint (pg) keeps the caller's earlier
  work.
- **The oracle is the database read back with `query`**, never the module's
  own encoding.
- **Speed, as a ratio.** The same 20 000 rows through `insert` and through a
  loop of `exec`, in one process. The ratio is asserted, not the time, so a
  slow machine scales both sides.
- **Drivers.**
  - `odbc`: the hermetic SQLite3 driver in `run_odbc.sh` (the gate);
    PostgreSQL's, MariaDB's and FreeTDS's ODBC drivers by
    `GBASIC_ODBC_CONNECTION` by hand.
  - `pg`: the opt-in `run_postgres.sh` (`GBASIC_POSTGRES_TEST=1`), which the
    gate skips. That is a stated limit: `pg.insert` is proven by a run here
    before each push that touches it.

## 6. What building it found

- **No driver could name the failed row, two ways.** SQLite3's ODBC driver
  leaves `SQL_ATTR_PARAM_STATUS_PTR` untouched and counts as processed only
  the rows before the failure, where the specification counts the failed
  one. psqlODBC marks every row of a failed batch as an error. So a failed
  batch in a transaction of the call's own is rolled back and run again a
  row at a time; the failure then names its row exactly. Inside the
  caller's transaction, a row is named only when the status array marks
  exactly one. Held by `odbc_insert_test`'s third-batch row (2100), which a
  perturbation removing the rerun turned red.
- **Money and numbers share a column.** The first rule refused a money
  value beside an exact decimal in one column. Both travel as decimal text,
  so a column holding either is declared DECIMAL with its largest scale.
- **`exec` refuses `unknown` as a parameter** in `pg` and `odbc`, though
  G-SQL and these verbs take it as NULL. That predates this work; it is
  recorded in DOGFOOD (2026-11-18) for the sweep.
- **Measured, 20 000 rows against a loop of `exec` in one transaction:**
  - `pg.insert` over `COPY`: 14x to 22x (`run_postgres.sh` asserts at least
    3x);
  - `odbc.insert`: 2.1x on SQLite3 (in-process, so there is no round trip to
    save) and 8x on psqlODBC over the local socket.

  MariaDB and FreeTDS were not run: no credentials here. Valgrind finds no
  invalid access in either fixture.

