# G-SQL: a `DURATION` column, stored exactly and never keyed

**Status:** designed and built 2026-11-08 (`GSQL_COL_DURATION` in `include/gsql_table.h`; `src/gsql/table.c`, `v3.c`, `sql_parse.c`, `sql_exec.c`; the reader `gsql_duration_text` in `src/modules/gsql.c`; tier duration of `tests/run_gsql_module.sh`, `tests/gsql/duration_test.bas`), from Matthew's ruling the same day, which reopens a decision of 2026-10-29 (`gsql_design.md` §3.4 and §13 question 4). Asked for by the gBASIC books' volume 3 (Chapter 6, where `dbframe.schema` had no column to advise for a duration), and needed for `dbframe`'s `kind` field.

## 1. What it is

```sql
create table onboarding (id integer primary key, took duration, term duration)
```

A `DURATION` column holds a gBASIC duration as it was given, field for field: `24 hours` comes back as `24 hours`, not `1 day`, and `1 month 14 days` as itself. It is stored as serialize v3 already writes one (`gsql_storage_design.md` §3.3: years, months, weeks, days, hours, minutes and seconds, each a little-endian int32), so nothing new is written to disk except the kind's name in the catalog.

**A duration may also be stored inside a record** now. It was refused there too (*"a duration cannot be stored, and the record holds one"*), for the reason in §2, which was never about storing.

## 2. What the 2026-10-29 decision was about, and what it got wrong

Durations were left out of the first release *"which have no single sort order and so could be stored but never indexed"*. The storage design gave the reason as: gBASIC *"makes two durations equal only field by field (`1 day` and `24 hours` tie in a sort and are not `=`)"*.

**Measured 2026-11-08, that is not the language:** `24 hours = 1 day` is `true`. Equality is the pair (total months, total exact seconds) (`datetime_design.md`, duration equality), and `1 month < 31 days` raises (*"a month has no fixed length"*). So:

- a duration with no months or years **is** totally ordered, by its seconds, consistently with `=`;
- one carrying months is equal to another by the pair but **has no order** against an exact one.

The conclusion stands for a different reason: a column that may hold `1 month` has no byte order that answers `<`, so it cannot be a range-able key. An equality-only key (the pair) would serve `GROUP BY`, `DISTINCT` and `UNIQUE`, and is **not built here**: §3 refuses those, and the refusal is the place to revisit it if a real program asks. §3.2 of the storage design is corrected to say this.

## 3. Never keyed

Each place that needs a key refuses a duration column, naming it, with what to do instead:

- `PRIMARY KEY`, `UNIQUE`, `CREATE INDEX`, and a foreign key's columns (which must be a key);
- `GROUP BY`, `DISTINCT` and `count(DISTINCT …)` over one, which already refuse a kind with no key (`gsql_aggregate_design.md`, `gsql_distinct_design.md`), now reached by a column rather than only by a record's field. These three are refused **when a value is met**, as they already were for a record's field, so over an empty table they answer (`count(DISTINCT d)` of no rows is 0); the key refusals above are refused when the table or index is declared.

The sentence: *"`t.took` is a duration, so it cannot be in a key: a duration has no single order (`1 month` against `31 days` has none); to find or sort by its length, keep its total_seconds in a NUMBER column beside it"*. `total_seconds` is the duration's own field, which raises for one carrying months, so the column beside it can only be filled for durations that have a length. The `GROUP BY`/`DISTINCT` refusal said *"it has no single equality G-SQL can key"*, which §2 shows is false for a duration; all five of those sentences (grouping, the set operations, `CYCLE`) now say *"G-SQL has no key for it"*, true of a duration, an array and a record alike.

**What still works**, because SQL's operators are gBASIC's own (`gsql_sql_design.md` §2): `=`, `<` between exact durations, `+`, `-`, a duration plus a date, `IS NULL`, and `ORDER BY`, which sorts in memory as a sort meeting a duration already does (`gsql_operator_spill_design.md` §2) and raises gBASIC's own sentence if a month-bearing value meets an exact one.

## 4. Into the column

| given | stored |
|---|---|
| a duration | itself |
| text | read as §5 reads it, as a date column reads text (`gsql_store_conversion_design.md` §1) |
| a number | **refused**: a number has no unit, and taking it as seconds is a guess the column would make silently. The sentence names `'90 seconds'` and, from gBASIC, `(1 second) * n` |

## 5. `CAST(text AS DURATION)`, and the dump

The text is the form gBASIC prints a duration in, which is also the literal syntax: an optional sign and digits, then a unit (`year`, `month`, `week`, `day`, `hour`, `minute`, `second`, singular or plural), repeated, each unit at most once, and `0 seconds` for nothing. Each number lands in **its own field**, so `'24 hours'` is 24 hours and the text a duration prints as reads back as the very same fields. Anything else is refused, naming the text: a fraction (`'1.5 hours'`, which gBASIC would round), a unit twice, an unknown word.

`CAST(duration AS TEXT)` is that printed form.

**`DUMP TO` writes a duration as `CAST('<text>' AS DURATION)`**, the shape it already uses for a record (`gsql_dump_design.md` §4), so a restore puts back the same fields. A duration **inside a record** is still refused by the dump, as any typed value inside a record is (§5 there), since `encode` has no form for it.

## 6. Compatibility

The catalog names a column's kind as a word (`table.c`, `kind_from_name`), so a database with no duration column is unchanged. **Measured against bf21cccc**, the build before this one, with no newer owner alive: the database's other tables are served, and the table with a duration column is refused, *"table t's schema does not parse [CORRUPT]"*. The older build's word is wrong, the table is not corrupt, and it cannot be changed now. Recorded so that a report of it is recognised.

**A trap in measuring it:** the first run said the older build read the table, durations and all. It had not: the newer build's owner was still lingering, and the older client was served by it, the wire protocol being the same. A version check across builds has to see no owner alive first.

`INTERVAL` is refused as a type by name, with *"for INTERVAL, write DURATION"*: SQL's `INTERVAL '1' DAY` literal syntax is not taken, since the text form in §5 is the one a duration prints in.

## 7. What was measured, and how

`tests/gsql/duration_test.bas`, 44 checks. The hand-written half states each answer, with each refusal beside an accepted neighbour. **The generated half's oracle is the generator**: 300 durations are drawn as seven fields (a fixed LCG, a third of the fields set, a tenth of those near the int32 range, negatives included, half written smallest unit first), written out as text, stored, and each read back is held against the fields drawn, then again after a `CAST` to text and back. A dump and restore must then give the same `serialize` bytes for every one. Seven perturbations were each red: every part read into seconds, a unit allowed twice, the dump's duration branch removed, the column accepting any value, the primary-key refusal removed, the number refusal removed, and text no longer converted on store.

**A duration inside a record is refused by `DUMP TO`**, as a date there already was, since `encode` has no form for one; the fixture checks the refusal and dumps after clearing it.

**Found on the way, and not changed here:** `-(2 hours)` is refused (*"arithmetic operator '-' expected number but got duration"*) where `0 seconds - 2 hours` works, and `(1 second) * 3.5` is `4 seconds` with no warning. Both are DOGFOOD's (2026-11-08 entry).

## 8. `dbframe`

`dbframe.schema` gains a `kind` per column (`date`, `datetime`, `time`, `duration`, `money(USD)`, `boolean`, `number`, `text`), and `dbframe.gsql_create_sql(table, df)` writes a G-SQL `CREATE TABLE` from it. The existing `type` (the SQLite DDL type) is unchanged.
