# G-SQL: a value stored into a typed column takes its kind; exec answers rows

**Status:** Shipped 2026-11-01 (`store_convert` in `src/gsql/sql_exec.c`, the rows path of `gsql_eval_exec` in `src/modules/gsql.c`; `tests/gsql/module_test.bas`, `tests/gsql/blocks_test.bas`). Designed the same day, from Matthew's rulings, from his first hand-written block program. His principle: *"much of what gBASIC should be is to reduce the work necessary for programmers. The less they have to think or remember, the better."*

## 1. Storing converts

**The gap.** G-SQL made no implicit conversion anywhere (`gsql_sql_design.md` §2), so an insert had to spell every typed value:

```sql
INSERT INTO shares VALUES (1, 'Checking', cast('2026-11-01 04:31:00' as datetime), cast('199.50' as money(USD)))
```

Matthew expected the column to do it: *"the table already knows what they are"*. SQL Server and PostgreSQL both do (PostgreSQL's "assignment cast").

**The rule.** A value stored into a column is converted to that column's kind when it is text and the column is a date, time, date-time or money, or a number and the column is money:

| column | takes | as |
|---|---|---|
| `date`, `time`, `datetime` | text | gBASIC's own `{date}`, `{time}`, `{datetime}` reading of it |
| `money(CUR)` | text, or a number | gBASIC's own `{CUR}`, so text is exact and a number takes its shortest decimal |
| `integer` | text that is a whole number written in digits *(2026-11-02, §1a)* | that integer, exact |

- **Where:** `INSERT ... VALUES`, `INSERT ... SELECT`, `UPDATE ... SET`, and a column's `DEFAULT`, the places a value is stored. Each goes through the conversion `CAST` already uses, so what text a column accepts is decided in one place.
- **Text that does not convert is refused,** naming the column, its kind and the text, as the kind refusal does today.
- **Everywhere else stays strict:** a comparison, arithmetic and a function's argument make no implicit casts, so `where name = 1` is still refused rather than guessed.
- **Integer columns refuse a decimal** (Matthew: *"it is usually a mistake to assign a decimal value to an integer"*), in text or as a number, and since 2026-11-02 take text that is exactly an integer (§1a). A `number` column, text and boolean columns are unchanged.

### 1a. Text that is exactly an integer (2026-11-02)

Asked by the gBASIC books' volume 3 (Q1): every field `csv.records` reads is text,
so every integer column in every import needed `cast(:x as integer)`. Matthew
ruled for converting, under a narrow rule: *"We want gBASIC to 'just work' in as
many situations as is safely possible. If '007' and we want to retain the
prefixed zeroes then we should use a proper text field."*

**The rule:** an optional sign, at least one digit, nothing else, within 64
bits. `"25"`, `"-7"`, `"+25"` and `"007"` (which is 7) are stored, exact. Each
of the rest is refused as `TYPE`, naming the column, with its own reason:
`"2.5"`, `"2.0"` and `"1,000"` are not a whole number; `"1e3"`, `"0x1A"`,
`"inf"`, `"1_000"` and digits from another script are not a number written in
digits; `" 25"` and `"25 "` have a space (text is never trimmed for you);
`""`, a sign alone, a NUL, and one past the 64-bit range. A NUMBER that is not
exact (`6 / 2`) is refused as before.

**Stricter than CAST, on purpose, and measured first:** the conversion `CAST`
uses (`{number}`) reads `" 25"` as an exact 25, so the store path checks the
text against the rule before the cast rather than inheriting that. `CAST` itself
is unchanged.

**Two messages moved, and one was a gap of its own.** A conversion refusal in a
statement storing several rows now says which row (`row 2 of VALUES: ...`, `row
2 of the query: ...`), as a refusal at the insert always did; it did not for
text that was no date or amount either, and integer text would have made that
common. And `INTEGER DEFAULT 'x'` is refused as `TYPE` by the conversion, as a
date column's bad default already was, where it was `SCHEMA` from the table
layer; a default that is not text (`DEFAULT 2.5`) is still `SCHEMA`. Held by `tests/gsql/integer_text_test.bas` (30 checks, every
answer by hand, each refused shape beside an accepted one) in
`run_gsql_module.sh`.

## 2. `gsql.exec` answers rows

**The gap.** `exec` answered `{ command, rows_affected }` and refused a statement or block that returned rows, so a program had to choose between `exec`, `query` and `for each` by how its SQL ended. Matthew's model was that *the last statement's value is what is returned*.

**The rule.** `gsql.exec` runs anything and answers a record describing the last statement: `command` and `rows_affected` as now, and **`rows`, every row, when that statement returns rows**. A block of several statements also carries `statements`.

- **`gsql.query(x)` stays as the shortcut** for the rows alone, and **`for each row in x`** stays the way to stream a large result a batch at a time; `exec` and `query` hold every row.
- **What it gives up** is a refusal: `exec` of a `SELECT` written by mistake returns rows rather than being refused. The rows are in the answer, so nothing is hidden.

## 3. Tests

- **Storing:** text into each temporal kind and into money, a number into money, through VALUES, INSERT ... SELECT and UPDATE; each stored value compared as the typed value it became. Refusals beside them: text that is no date-time, text that is no amount, text and a decimal into an integer column, and `where name = 1` still refused.
- **exec:** a string `SELECT`, a block ending in a `SELECT`, and a block of several statements ending in one, each answering `rows`; one ending in an `INSERT` answering no `rows` field.
