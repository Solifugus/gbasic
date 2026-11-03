# G-SQL: a comparison reads text as the other side's kind

**Status:** Proposal, 2026-11-02. Asked for by gdash, planning its move from
SQLite to G-SQL (its S3 and S4), and ruled by Matthew the same day: *"I find
it very convenient in T-SQL to be able to compare numbers in strings to actual
numbers. Therefore, I strongly feel like G-SQL should do the same."*

## 1. The gap

`gsql_sql_design.md` §2 made no implicit conversion anywhere, so every one of
these was refused as `EXPRESSION`:

```sql
SELECT ... WHERE day >= '2026-10-01'      -- a date column against text
SELECT ... WHERE amount > 1000            -- money(USD) against a number
SELECT ... WHERE id = '7'                 -- an integer against text
SELECT ... WHERE id = $1                  -- ... and a form value, which is text
```

Storing already converts, because the column says what it holds
(`gsql_store_conversion_design.md`). A comparison has the same information:
one side has a kind, and the text on the other side can be read as it.

## 2. The rule

When the two sides of a comparison (`=`, `!=`, `<`, `<=`, `>`, `>=`,
`BETWEEN`, `IN`) are of different kinds:

- **One side is text** and the other a number, money, a date, a time or a
  date-time: the text is read as the other side's kind, **with the same readers
  storing uses** -- trimmed, a number written plainly, a date as `{date}` reads
  it, money in the other side's currency. Text that does not read as that kind
  is **refused**, naming both: T-SQL raises a conversion error there too.
- **One side is a number and the other money:** the number is read as money in
  that money's currency, as storing a number into a money column does.
- **Everything else stays refused:** a boolean against anything else, money in
  two currencies, a date against a number, a record against anything.

A number read from text is a number, not an integer: `id = '7.5'` against an
integer column is false, not refused, since `7.5` is a number and is not `7`.

Which side is text does not matter, and neither does what made it text: a
literal, a parameter, a string column, an expression. A string column compared
with an integer column reads each string as a number, which is what T-SQL does.

## 3. Index seeks must agree with scans

A comparison of an indexed column with a constant may be answered by a seek,
whose key is built from the constant (`bound_key`). The same conversion is
applied there against the column's kind, so `WHERE id = '7'` seeks the integer
7. A bound that does not convert does not seek; the query falls back to the
scan, whose comparison then refuses with the reason. A differential tier
answers each case both ways and requires the same rows.

## 4. Not done

- **`ORDER BY`, `GROUP BY`, `DISTINCT`, `min`, `max`** do not convert: they
  order values of one column, which are of one kind already.
- **Arithmetic** does not convert: `'2' + 3` is still refused, and text is
  joined with `||`.
- **gBASIC's own `=`** is a separate design (gdash S4), brought to Matthew
  before it changes.
