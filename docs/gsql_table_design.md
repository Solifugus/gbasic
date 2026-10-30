# G-SQL's table layer (Phase 1, increment 1)

**Status:** designed and built 2026-10-29 (`include/gsql_table.h`, `src/gsql/table.c`, `tests/run_gsql_table.sh`). It is the first layer of Phase 1 (`gsql_design.md` §11) and the one every later layer stands on: the SQL front end, the owner and the gBASIC surface all reach rows through it.

## 1. Where it sits, and why it works on bytes

Phase 1 is built bottom-up:

1. **the table layer** (this document): a catalog, typed rows, primary keys, unique and secondary indexes, `NOT NULL` and defaults, over the storage interface (`gsql_storage_design.md` §1);
2. **the owner and the protocol** (`gsql_design.md` §6.1, §6.5), from the prototype in `research/gsql/roundtrip.c`;
3. **the SQL front end**: parser, binder, plan and executor, with `EXPLAIN`;
4. **the gBASIC surface**: a `gsql` module first, then the `database` and `sql` blocks (§2).

**The table layer works on `serialize` version 3 bytes and never on interpreter values,** which is the arrangement the key encoder already has (`src/gsql/key.c`). Everything it decides can be decided from a value's tag and payload: whether a number is exact, which currency money is in, whether a date-time is a date. So it compiles alone, its tests need no interpreter, and the gBASIC values it is tested with are bytes gBASIC itself wrote. Expressions, which do need gBASIC's semantics (three-valued logic, exactness, money arithmetic), belong to the SQL layer, which runs inside the owner, a `gbasic` process (§6.5).

## 2. The trees

Every tree the layer creates begins with a NUL byte, so no table name can collide with one (a table name may not begin with NUL). The version store's two trees follow the same rule (`include/gsql_version.h`).

| tree | key | value |
|---|---|---|
| `\0catalog` | the table's name | its schema (§3) |
| `\0meta` | `be64(table id)` | `le64(next row id)`, for a table with no primary key |
| `\0r` + `be64(table id)` | the row key (§4) | the row (§5) |
| `\0i` + `be64(index id)` | an index entry (§6) | the row key, or nothing |
| `\0meta` | `"next"` | `le64(next id)`, shared by tables and indexes |

**Trees are named by id, not by table name,** so dropping a table and creating another of the same name cannot meet the first one's leftovers, and a rename later will touch one catalog entry.

## 3. The schema

A schema is stored as one `serialize` version 3 **record**, so `tests/serialize/decode_v3.py`, which was written from the format's text, can read it, and so can gBASIC's own `deserialize` once the four-byte header is put in front. Its fields:

| field | value |
|---|---|
| `format` | 1. A newer one is `FORMAT`, never guessed at |
| `id` | the table's id |
| `columns` | an array of records: `name`, `kind` (§3.1), `currency` (text, `MONEY(CUR)` only), `not_null` (boolean), `default` (a value, absent if none) |
| `key` | an array of column positions (0-based), empty when there is no primary key |
| `indexes` | an array of records: `name`, `id`, `columns` (positions), `descending` (booleans, one per column), `unique` (boolean) |

### 3.1 Column kinds

The first release stores what `gsql_design.md` §3.4 decided (Matthew, 2026-10-29), checked from the bytes:

| kind | accepts |
|---|---|
| `number` | any number, exact or not |
| `integer` | an **exact** number only. An inexact one is refused, whatever its value: `6 / 2` is 3.0 and is refused, `3` is accepted (§3.2, "don't silently replace the value -- refuse"). An overflowing integer reaches the layer already inexact (gBASIC degrades it with warning 2112), so it is refused by the same rule |
| `string` | a string, any bytes |
| `boolean` | a boolean |
| `money` | money in any currency |
| `money(CUR)` | money in `CUR`, matched on the alphabetic code |
| `date` | a date-time that is not time-only, at day precision |
| `time` | a time-only value |
| `datetime` | a date-time that is not time-only, at hour precision or finer |
| `record` | a record whose fields, at any depth, hold numbers, strings, booleans, money, date-times, records, arrays or absences |

**What is refused, everywhere, including inside a record:** durations (no single sort order, `gsql_storage_design.md` §3.2), files, directories, functions, regexes and actors (process-bound, §3.4). Arrays are refused **as a column's value** (§3.4) and accepted **inside a record**, which §3.5 allows.

**A date-time is matched exactly, never coerced** (§9: no implicit casts): a `date` column refuses a datetime and a `datetime` column refuses a date. A year-or-month-precision value fits neither, and is refused rather than placed in one.

**Absence.** `unknown` is accepted in every column unless it is `NOT NULL`. A bound `nothing` arrives as `serialize`'s null tag and is **stored as `unknown`**, because "never return `nothing` for a stored NULL" (§3.1). Inside a record the bytes are the application's and are kept as written.

### 3.2 Declarations refused at creation

A name that is empty, longer than 255 bytes or begins with NUL; two columns of one name; no columns; a key or index naming a column twice or a column that is not there; a primary key on a `record` column (it has no index order); a key column that is not `NOT NULL` is **made** `NOT NULL`, which is what SQL does; a default that its own column would refuse (a default is checked as an insert would check it, so a bad one fails at `CREATE` rather than at the first insert that omits the column).

## 4. The row key

- **With a primary key:** the key encoding (`gsql_storage_design.md` §3.2) of the key columns in order, so a rows tree is in primary-key order and a point read is one `get`. A `money(CUR)` column is keyed without its currency (`GSQL_KEY_MONEY_TYPED`), an untyped `money` column with it.
- **Without one:** a hidden row id, an exact integer from `\0meta`, keyed the same way. Ids are never reused.

Equality for a primary key or a unique index is therefore **key equality**, which is gBASIC's "same value": exact 5 and 5.0 are one key, as `5 = 5.0` says, and NaN equals NaN (§3.2's rule, and PostgreSQL's).

## 5. The row

A format byte (1), a LEB128 column count, then each column's value in declared order (`gsql_storage_design.md` §3.3). **A row shorter than its schema** -- written before `ADD COLUMN` -- reads the missing columns as their defaults (`unknown` where there is none), so adding a column never rewrites a table. A row longer than its schema, or one whose values do not parse, is `CORRUPT`.

## 6. Indexes

An entry is the key encoding of the index's columns in order, each inverted when descending.

- **A unique index** keys the entry by its columns alone and stores the row key as the value, so a duplicate is a `NO_OVERWRITE` refusal in the same transaction.
- **SQL's `UNIQUE` lets several rows hold `NULL`** (PostgreSQL's default, `NULLS DISTINCT`): an entry with an absent column is written as a non-unique one, so it never conflicts.
- **A non-unique index** appends the row key to the entry and stores nothing. A scan over a range of the index reads the row key back from the entry's tail, which is self-delimiting because every key component is.
- **A `record` column cannot be indexed** in this increment; a field index (§3.5) is the SQL layer's, since it names a field by an expression.

Creating an index over a table that has rows builds it in the same transaction, and a unique index the rows already violate is refused.

## 7. Errors

Every operation answers a code and fills a message naming the table, the column and the reason. The codes: `OK`, `NOT_FOUND` (no such table, row or index), `EXISTS` (a table or index of that name), `DUPLICATE` (a primary key or unique index already holds the value, naming which), `NOT_NULL`, `TYPE` (the column's kind refuses the value, saying what it is and what it wanted), `SCHEMA` (a declaration refused, §3.2), `CORRUPT`, `STORAGE` (the engine's own status, named), and `MISUSE`.

**A refused write changes nothing,** however far it got: the layer checks every column, then every unique index, before it writes, so a row is never half in. The transaction stays usable after a refusal, which is what lets a SQL statement report a constraint violation and the client carry on.

## 8. The invariant check

`gsql_table_check` reads every table and requires: every row parses and fits its schema; every row's key is what its key columns encode to; every index holds exactly one entry for every row, and nothing else; and every unique index's entries are unique. The model test runs it after every reopen, so a write that left an index behind its rows is found however it happened.

## 9. How it is tested

`tests/run_gsql_table.sh`:

- **build**, under `-Wpedantic -Werror`.
- **kinds**: the type rules of §3.1 over values **gBASIC serialized** (`tests/gsql/table_values.bas`), each with its verdict written by hand, so the checker is held to the encodings the language really produces rather than to a test's own writer.
- **model**: a random workload of creates, inserts, updates, deletes, index creations, column additions and reopens against an in-memory model, on the simulated disk and the real filesystem, under ASan and UBSan, with the invariant check after every reopen.
- **layout**: the catalog and the rows dumped and decoded by `tests/serialize/decode_v3.py`'s reader, which was written from the format's text, so a schema or row encoding that our writer and reader agree on wrongly is still caught.
