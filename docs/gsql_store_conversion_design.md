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

- **Where:** `INSERT ... VALUES`, `INSERT ... SELECT`, `UPDATE ... SET`, and a column's `DEFAULT`, the places a value is stored. Each goes through the conversion `CAST` already uses, so what text a column accepts is decided in one place.
- **Text that does not convert is refused,** naming the column, its kind and the text, as the kind refusal does today.
- **Everywhere else stays strict:** a comparison, arithmetic and a function's argument make no implicit casts, so `where name = 1` is still refused rather than guessed.
- **Integer columns stay strict** (Matthew: *"it is usually a mistake to assign a decimal value to an integer"*). A `number` column, text and boolean columns are unchanged.

## 2. `gsql.exec` answers rows

**The gap.** `exec` answered `{ command, rows_affected }` and refused a statement or block that returned rows, so a program had to choose between `exec`, `query` and `for each` by how its SQL ended. Matthew's model was that *the last statement's value is what is returned*.

**The rule.** `gsql.exec` runs anything and answers a record describing the last statement: `command` and `rows_affected` as now, and **`rows`, every row, when that statement returns rows**. A block of several statements also carries `statements`.

- **`gsql.query(x)` stays as the shortcut** for the rows alone, and **`for each row in x`** stays the way to stream a large result a batch at a time; `exec` and `query` hold every row.
- **What it gives up** is a refusal: `exec` of a `SELECT` written by mistake returns rows rather than being refused. The rows are in the answer, so nothing is hidden.

## 3. Tests

- **Storing:** text into each temporal kind and into money, a number into money, through VALUES, INSERT ... SELECT and UPDATE; each stored value compared as the typed value it became. Refusals beside them: text that is no date-time, text that is no amount, text and a decimal into an integer column, and `where name = 1` still refused.
- **exec:** a string `SELECT`, a block ending in a `SELECT`, and a block of several statements ending in one, each answering `rows`; one ending in an `INSERT` answering no `rows` field.
