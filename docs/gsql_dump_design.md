# G-SQL: a SQL dump, and restoring one

**Status:** designed 2026-11-01, not built. Matthew asked for a SQL dump, schema-only as well as full, for analyzing a database's design and for upgrading it. It sits beside `gsql.backup` (`gsql_backup_design.md`), which is physical. A dump is logical: text a person can read and edit, and that a newer build of G-SQL can load whatever its storage format has become.

## 1. What it is

```basic
r = gsql.dump(db, "/exports/shop.sql")                       ' schema and rows
r = gsql.dump(db, "/exports/shop-schema.sql", { schema_only: true })
print r.tables, r.rows, r.bytes

r = gsql.restore(db2, "/exports/shop.sql")                   ' into an empty database
print r.statements, r.rows
```

**The statements underneath** are `DUMP TO 'file' [SCHEMA ONLY]` and `RESTORE FROM 'file'`. Both return progress rows, one per slice, the arrangement `BACKUP TO` already has. A cursor keeps its snapshot between fetches, and the owner serves other clients between them, so a dump of a large table stalls nobody. The file is written and read by the owner, so its path is the owner's machine's, which today is the program's.

**A dump is consistent:** it reads one snapshot from start to end, and rows committed while it runs are not in it.

## 2. What the file holds, in order

1. A header comment: the database, the moment of the snapshot, the build that wrote it, and whether it is schema-only.
2. **Tables**, in name order: `CREATE TABLE` with every column's kind, `NOT NULL`, default, generator (`SERIAL`, `SERIAL OF (...)`, `RANDOM(...)`), the primary key and the table's `CHECK`s. The text is the SQL layer's canonical print, the form views are already stored in.
3. **Indexes**, as `CREATE [UNIQUE] INDEX`, before the rows. Building an index over existing rows is the slow path (`gsql_practices.md` §2); filling an indexed table is not.
4. **Rows**, table by table in primary-key order (row-id order for a table with none), as multi-row `INSERT ... VALUES`, 1 000 rows a statement and 10 statements a transaction (`BEGIN` ... `COMMIT`), the batching `gsql_practices.md` §1 recommends.
5. **Serial counters**, one statement per counter (§4). Inserting rows that carry their serial values moves each counter to the largest value present, but a counter is higher than that when the last rows were deleted, and a number is never issued twice.
6. **Foreign keys**, as `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY`, after every row, so the order the tables were filled in cannot refuse a child before its parent. Adding one checks the rows already there, which is what a restore wants.
7. **Views**, in dependency order (a view reading a view comes after it). A view's stored text is already canonical SQL.

A schema-only dump is the same file without steps 4 and 5.

## 3. Every stored value must have a literal that reads back as the same value

A dump is only as good as its literals. Today's printer (`print_lit` in `sql_parse.c`, written for `EXPLAIN` and views) prints booleans, numbers and strings and writes `<value>` for everything else. Measured against the parser on 2026-11-01:

| stored value | literal the dump writes | reads back today? |
|---|---|---|
| unknown | `NULL` | yes |
| boolean | `TRUE`, `FALSE` | yes |
| exact integer | `42`, `-9007199254740993` | yes: digits alone are exact |
| inexact number | its shortest round-trip decimal, with `.0` when it would otherwise read as exact; `-0.0` | yes (`0.1` stays 0.1) |
| NaN, infinity | `CAST('nan' AS NUMBER)`, `CAST('inf' AS NUMBER)`, `CAST('-inf' AS NUMBER)` | yes. A bare `nan`, which the printer writes now, reads as a column name |
| text | `'...'`, a quote doubled | yes. The bytes are written as they are, newlines and any byte at all included, since G-SQL has no escape syntax and gBASIC strings are counted |
| money | `CAST('1.234567' AS MONEY(USD))`, at the currency's **storage** scale, `money.text`'s form | yes, exactly: the text path is exact (`money_design.md`), measured 1.234567 kept. Printing at the minor unit would lose the guard digits |
| date, time, date-time | `DATE '2026-01-05'`, `TIME '10:30'`, `TIMESTAMP '2026-01-05 10:30:00'`, a time written **at its own precision** | yes. Measured: a date-time reads back byte-identical from `'... 10:30'` and `'... 10:30:00'`, so writing it to the second is exact. A time of day keeps its precision, and `TIME '10:30' = TIME '10:30:00'` is false, so a time is written with exactly the fields it has |
| record | `CAST('<encode text>' AS RECORD)` | **no**: `CAST ... AS RECORD` is refused today (§5) |

**The rule is checked, not trusted:** every kind above goes through the round trip in the tests (§6).

## 4. Two statements the dump needs that G-SQL lacks

- **Setting a serial counter.** `ALTER TABLE t ALTER COLUMN id RESTART WITH 42`, PostgreSQL's spelling for an identity column, plus `FOR (scope values)` for a `SERIAL OF` column, one statement per scope. It sets the high-water mark, so the next number issued is 43. It refuses a value below the largest present, which would let a number be issued twice.
- **Reading a record from text.** `CAST(text AS RECORD)` decodes gBASIC's `encode` text with gBASIC's own `decode`, so a record column's values have a literal.

## 5. Decisions, and what is refused

- **A record holding a typed value** (a date, money) cannot be written as `encode` text, since `encode` refuses those. **The dump refuses it**, naming the table, the row's key and the column, rather than writing something that restores differently. Record columns are in practice JSON-shaped, and a typed-text literal can be added if a real one needs it. *(My default; Matthew's call.)*
- **Money in a registered currency** (`money.register`) restores only where that currency is registered too. The dump writes it as any other money, and the header lists the registered currencies used, so the failure on restore names the cause.
- **Restoring is into an empty database.** `RESTORE FROM` refuses a database that holds any table or view, since merging a dump into existing data is a different operation with its own questions.
- **Restoring commits batch by batch,** as the file says. A restore that fails partway leaves the batches before the failure committed, and its error names the statement and its line in the file. Rerunning means starting from an empty database again.
- **A restore runs the file's statements and nothing else**: `CREATE`, `ALTER`, `INSERT`, `BEGIN`, `COMMIT`. A file holding a `DROP`, `DELETE`, `UPDATE`, `BACKUP`, `DUMP` or `RESTORE` is refused before anything runs, so an edited or hostile file cannot destroy a database through `gsql.restore`.

## 6. How it is tested

- **The oracle is the stored values, not the dump's text.** "Dump, restore, dump, and require the two dumps to be identical" passes on a printer that loses information: a value printed with lost precision restores as the lost value and prints the same way the second time. So the tier compares the **source database and the restored one** table by table, row by row, as `serialize` bytes. That is what a column holds, so an exact 3 and an inexact 3.0, or two precisions of one time of day, are told apart.
- **The text identity is asserted as well,** since a restore that reorders or renames would still hold the same values: dump, restore, dump, and the two files must be byte-identical apart from the header.
- **A corpus of awkward values**, one per kind and per edge: 2^53 + 1 exact, -0.0, NaN, infinities, the shortest-decimal cases, a string with a quote, a newline, a NUL and non-ASCII text, money at the storage scale in two currencies in one untyped column, every precision of time and date-time, `NULL` in every kind, records.
- **Schema**: every column option, composite and descending indexes, a unique index holding several `NULL`s, `CHECK`s, both kinds of foreign key, views on views, a renamed table, and a table that gained a column after its rows were written. After a restore the schema-only dumps of the two databases must be identical, and a serial must issue the same next number in both.
- **Refusals**, each beside its legal neighbour: a record holding a date; restoring into a database that is not empty; a file holding a `DELETE`; a counter set below its largest value.
- **Consistency**: a dump taken while another client commits holds the snapshot's rows exactly.
- **Size**: a table larger than one slice, so a dump crosses slices and a restore crosses batches.
