# G-SQL: a SQL dump, and restoring one

**Status:** Shipped 2026-11-01 (`DUMP TO` in `src/gsql/sql_exec.c`, `gsql.dump` and `gsql.restore` in `src/modules/gsql.c`; the `dump` tier of `tests/run_gsql_module.sh`, the key tier of `run_gsql_key.sh`, cookbook recipe 15). What building it changed is §7. Matthew asked for a SQL dump, schema-only as well as full, for analyzing a database's design and for upgrading it. It sits beside `gsql.backup` (`gsql_backup_design.md`), which is physical. A dump is logical: text a person can read and edit, and that a newer build of G-SQL can load whatever its storage format has become.

## 1. What it is

```basic
r = gsql.dump(db, "/exports/shop.sql")                       ' schema and rows
r = gsql.dump(db, "/exports/shop-schema.sql", { schema_only: true })
print r.tables, r.rows, r.bytes

r = gsql.restore(db2, "/exports/shop.sql")                   ' into an empty database
print r.statements, r.rows
```

**The statement underneath** is `DUMP TO 'file' [SCHEMA ONLY]`, returning progress rows, one per slice, the arrangement `BACKUP TO` already has. A restore is the program running the file's statements (§7). A cursor keeps its snapshot between fetches, and the owner serves other clients between them, so a dump of a large table stalls nobody. The file is written and read by the owner, so its path is the owner's machine's, which today is the program's.

**A dump is consistent:** it reads one snapshot from start to end, and rows committed while it runs are not in it.

## 2. What the file holds, in order

1. A header comment: the database, the moment of the snapshot, the build that wrote it, and whether it is schema-only.
2. **Tables**, in name order: `CREATE TABLE` with every column's kind, `NOT NULL`, default, generator (`SERIAL`, `SERIAL OF (...)`, `RANDOM(...)`), the primary key and the table's `CHECK`s. The text is the SQL layer's canonical print, the form views are already stored in.
3. **Indexes**, as `CREATE [UNIQUE] INDEX`, before the rows. Building an index over existing rows is the slow path (`gsql_practices.md` §2); filling an indexed table is not.
4. **Rows**, table by table in primary-key order (row-id order for a table with none), as multi-row `INSERT ... VALUES`, 1 000 rows a statement; a restore commits 10 statements a transaction, the batching `gsql_practices.md` §1 recommends.
5. **Serial counters**, one statement per counter (§4). Inserting rows that carry their serial values moves each counter to the largest value present, but a counter is higher than that when the last rows were deleted, and a number is never issued twice.
6. **`CHECK`s and foreign keys**, as `ALTER TABLE ... ADD CONSTRAINT`, with their names, after every row, so the order the tables were filled in cannot refuse a child before its parent. Adding one checks the rows already there, which is what a restore wants.
7. **Views**, in dependency order (a view reading a view comes after it). A view's stored text is already canonical SQL.

A schema-only dump is the same file without steps 4 and 5.

## 3. Every stored value must have a literal that reads back as the same value

A dump is only as good as its literals. Today's printer (`print_lit` in `sql_parse.c`, written for `EXPLAIN` and views) prints booleans, numbers and strings and writes `<value>` for everything else. Measured against the parser on 2026-11-01:

| stored value | literal the dump writes | reads back today? |
|---|---|---|
| unknown | `NULL` | yes |
| boolean | `TRUE`, `FALSE` | yes |
| exact integer | `42`, `-9007199254740993` | yes: digits alone are exact |
| exact decimal | its digits, always with a point (`6331.475`, `2.0`) | yes: digits with a point are an exact decimal (`exact_decimal_design.md`, 2026-11-08) |
| inexact number | its shortest round-trip decimal **with an exponent** (`1e-1`, `2000e0`), which is what keeps it a double now that `0.1` reads as an exact decimal; `-0.0` | yes. Until 2026-11-08 this was the digits with `.0`, which a restore now reads as a decimal: measured, run_gsql_module's dump tier saw 3.0 and 0.3333333333333333 come back with different bytes |
| NaN, infinity | `CAST('nan' AS NUMBER)`, `CAST('inf' AS NUMBER)`, `CAST('-inf' AS NUMBER)` | yes. A bare `nan`, which the printer writes now, reads as a column name |
| text | `'...'`, a quote doubled | yes. The bytes are written as they are, newlines included; text holding a NUL is written `U&'...'` (§7), since SQL text may not hold one |
| money | `CAST('1.234567' AS MONEY(USD))`, at the currency's **storage** scale, `money.text`'s form | yes, exactly: the text path is exact (`money_design.md`), measured 1.234567 kept. Printing at the minor unit would lose the guard digits |
| date, time, date-time | `DATE '2026-01-05'`, `TIME '10:30'`, `TIMESTAMP '2026-01-05 10:30:00'`, a time written **at its own precision** | yes. Measured: a date-time reads back byte-identical from `'... 10:30'` and `'... 10:30:00'`, so writing it to the second is exact. A time of day keeps its precision, and `TIME '10:30' = TIME '10:30:00'` is false, so a time is written with exactly the fields it has |
| record | `CAST('<encode text>' AS RECORD)` | yes, since this increment (§4); a record holding a date or money is refused (§5) |

**The rule is checked, not trusted:** every kind above goes through the round trip in the tests (§6).

## 4. Two statements the dump needs that G-SQL lacks

- **Setting a serial counter.** `ALTER TABLE t ALTER COLUMN id RESTART WITH 42`, PostgreSQL's spelling for an identity column, plus `FOR (scope values)` for a `SERIAL OF` column, one statement per scope. It sets the high-water mark, so the next number issued is 43. It refuses a value below the largest present, which would let a number be issued twice.
- **Reading a record from text.** `CAST(text AS RECORD)` decodes gBASIC's `encode` text with gBASIC's own `decode`, so a record column's values have a literal.

## 5. Decisions, and what is refused

- **A record holding a typed value** (a date, money) cannot be written as `encode` text, since `encode` refuses those. **The dump refuses it**, naming the table, the row's key and the column, rather than writing something that restores differently. Record columns are in practice JSON-shaped, and a typed-text literal can be added if a real one needs it. *(My default; Matthew's call.)*
- **Money in a registered currency** (`money.register`) restores only where that currency is registered too. The dump writes it as any other money, and the header lists the registered currencies used, so the failure on restore names the cause.
- **Restoring writes only to tables the restore created** (§7 says why this replaced "into an empty database").
- **Restoring commits batch by batch.** A restore that fails partway leaves the batches before the failure committed, and its error names the statement and its line in the file. Rerunning means starting from an empty database again.
- **A restore runs a dump's statements and nothing else**: `CREATE TABLE`, `CREATE INDEX`, `INSERT ... VALUES`, `ALTER TABLE ... ADD CONSTRAINT`, `ALTER COLUMN ... RESTART` and `CREATE VIEW`. A `DROP`, `DELETE`, `UPDATE` or anything else is refused when it is reached, naming its line.

## 6. How it is tested

- **The oracle is the stored values, not the dump's text.** "Dump, restore, dump, and require the two dumps to be identical" passes on a printer that loses information: a value printed with lost precision restores as the lost value and prints the same way the second time. So the tier compares the **source database and the restored one** table by table, row by row, as `serialize` bytes. That is what a column holds, so an exact 3 and an inexact 3.0, or two precisions of one time of day, are told apart.
- **The text identity is asserted as well,** since a restore that reorders or renames would still hold the same values: dump, restore, dump, and the two files must be byte-identical apart from the header.
- **A corpus of awkward values**, one per kind and per edge: 2^53 + 1 exact, -0.0, NaN, infinities, the shortest-decimal cases, a string with a quote, a newline, a NUL and non-ASCII text, money at the storage scale in two currencies in one untyped column, every precision of time and date-time, `NULL` in every kind, records.
- **Schema**: every column option, composite and descending indexes, a unique index holding several `NULL`s, `CHECK`s, both kinds of foreign key, views on views, a renamed table, and a table that gained a column after its rows were written. After a restore the schema-only dumps of the two databases must be identical, and a serial must issue the same next number in both.
- **Refusals**, each beside its legal neighbour: a record holding a date; restoring into a database that is not empty; a file holding a `DELETE`; a counter set below its largest value.
- **Consistency**: a dump taken while another client commits holds the snapshot's rows exactly.
- **Size**: a table larger than one slice, so a dump crosses slices and a restore crosses batches.

## 7. What building it changed and found

- **Restoring is not limited to an empty database.** A restore writes only to tables it created itself: an `INSERT`, `CREATE INDEX`, constraint or `RESTART` naming any other table is refused, and so are `CREATE ... IF NOT EXISTS`, `CREATE OR REPLACE VIEW` and `INSERT ... SELECT`. A file therefore cannot change data that was there before it, which is what "into an empty database" was for, and a restore can go into a database holding other tables. Each statement is checked when it is reached, not in a pass first, so a restore never reads a large file twice; one refused part way keeps the batches before it.
- **The file holds no `BEGIN` or `COMMIT`.** Those are protocol messages, not SQL, so `gsql.restore` batches the rows itself, ten `INSERT`s a transaction, and runs every other statement in its own. It reads the file a megabyte at a time and splits it with G-SQL's own lexer (`sql_statement_end`), so a `;` in a string or a comment ends nothing.
- **Text holding a NUL is written `U&'...'`**, standard SQL's Unicode escape string, which G-SQL reads now: `\0000` is the NUL, `\\` a backslash, `\XXXX` and `\+XXXXXX` any code point. SQL text may not hold a NUL byte, and nothing else could carry one. Every other byte is written as it is.
- **A scope's counter is written back as values**, which needed the key encoding read back (`gsql_key_value`): its tier in `run_gsql_key.sh` requires every value of the battery to key the same after the round trip, and 276 to come back as the very bytes gBASIC wrote. A money key holds no exponent, so **a serial numbered within money is refused**, naming the column.
- **`RESTART WITH` refuses any value below the counter**, not only below the largest row: a number issued is never issued again, and the counter is what knows which were.
- **A view that no longer runs and a foreign key whose parent is gone** cannot be created, so they are written as `-- not written:` lines and counted as `skipped`, never dropped silently.
- **The smallest int64 has no literal:** its digits after the minus are one past the largest, so they read as an inexact number. It is written `(-9223372036854775807 - 1)`. Found by the value oracle on its first run; the text identity could not have seen it.
- **Money's exponent is the minor unit**, and a value is stored four guard digits below it. The first printer wrote 1.234567 USD as 12345.67, and the dump of its restore was consistently wrong in the same way.
- **An owner defect, older than the dump:** a fetch that failed freed its cursor twice, once in the SQL layer and again when the owner dropped it, killing the owner. Any `SELECT` failing in a later batch would have done it; a dump refused part way was the first thing to. The owner now honours the service clearing `out->cursor`.
- **Not fixed, recorded:** valgrind, following the owner as a child, reports an uninitialised read in `lsm_flush` (`src/gsql/lsm.c`) when an owner exits. It happens on the tree before this work too, and no suite's valgrind tier follows children, which is why nothing saw it.

**Perturbations proven red:** money at the minor unit, a time losing its precision, the smallest int64 written bare, counters not written, a restore that allows anything, and a NUL written raw. Each is caught by the dump tier; the key decoder's three (a date field off by one, a NUL read back as another byte, a double's fraction shifted) by the key tier.
