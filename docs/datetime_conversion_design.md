# Converting between dates, times and datetimes

**Status:** designed and built 2026-11-01 (`datetime_convert_value` in `src/eval.c`, `gsql_h_cast` in `src/modules/gsql.c`; `tests/run_datetime_convert.sh`, `tests/run_gsql_sql.sh`), from Matthew's direction the same day: turning a datetime into a date "is obviously important", and the other directions belong in the same pass. It completes `datetime_design.md` §2, whose modifiers read only text, and with it G-SQL's `CAST` (`gsql_sql_design.md`).

## 1. The gap, measured

`{date}`, `{time}` and `{datetime}` read **text only**. Given a datetime **value**, every one of them refused, even `{date}` given a value that already is a date:

| given | `{date}` | `{time}` | `{datetime}` |
|---|---|---|---|
| the text `"2026-01-05"` | `2026-01-05` (day) | refused | `2026-01-05 00:00:00` (second) |
| the text `"2026-01-05 10:30"` | `2026-01-05 10:30` (minute) | refused | `2026-01-05 10:30:00` (second) |
| the text `"10:30"` | refused | `10:30` (a time) | refused |
| any datetime **value** | refused | refused | refused |

The lenses (`{day}`, `{minute}` and the rest) did take a value, and truncate it, so `t {day}= dt` already gave a datetime's date. But nothing gave a datetime's **time of day**, and nothing took a date **up** to a datetime, except by printing it to text and reading it back.

G-SQL's `CAST` is built on these three modifiers (`src/modules/gsql.c`), so `CAST(t AS DATE)`, `CAST(t AS TIME)` and `CAST(d AS DATETIME)` all refused a temporal value. `SERIAL OF (CAST(request_time AS DATE))`, `gsql_design.md` §10E's own example, could therefore not be written (`gsql_generated_design.md` §1).

## 2. The rule: a value becomes that kind

Given a datetime value, each modifier converts it. The text forms are unchanged.

- **`{date}`** gives the value's **date**: the `{day}` lens.
  - A time of day has no date, and is refused.
  - So is a year or a month, which has no day.
- **`{time}`** gives the value's **time of day**, at the value's own precision: `10:30` stays minutes, and `10:30:45` keeps its seconds.
  - A time is itself.
  - A date, a month or a year has no time of day, and is refused.
- **`{datetime}`** gives a **date and time to the second**, as the text form already does: a date becomes midnight, and `10:30` becomes `10:30:00`.
  - A time of day has no date, and is refused.
  - So is a year or a month.

Each refusal names what the value lacks (``a time of day has no date to take``), not the text the modifier would have liked.

**Why `{date}` truncates a value when its text form does not.** `{date}= "2026-01-05 10:30"` keeps the minutes. It reads text at the precision written, which is `datetime_design.md` §2's own example, and is left alone. A value handed to `{date}` is a different request: a conversion, which is what every SQL dialect means by a cast to `DATE` and what a `DATE` column holds. A value already at day precision is unchanged either way.

**Not here:**

- a layout form given a value (`{date "DD/MM/YYYY"}`): a layout describes text, and stays text-only;
- time zones: a gBASIC datetime has none (`datetime_design.md`).

## 3. G-SQL's `CAST`

`CAST(x AS DATE)`, `AS TIME` and `AS DATETIME` take a temporal value by the rule above. So SQL and a program convert the same way, through one implementation.

`CAST(text AS DATE)` reads the text as a datetime, then takes its date. So `CAST('2026-01-05 10:30' AS DATE)` is `2026-01-05`. That is PostgreSQL's answer and what a `DATE` column can hold, where `{date}` alone would keep the minutes and the column would refuse them. `AS TIME` and `AS DATETIME` read text as their modifiers already do.

## 4. How it is tested

**the language** (`tests/run_datetime_convert.sh`, self-checking):

- every kind of value, at every precision, through each of the three modifiers;
- each answer written by hand: its kind, precision and fields. The check reads all three, since `string()` of a day and of a midnight datetime are different text, but two wrong conversions could print alike;
- each refusal naming what is missing;
- **the text forms unchanged** (the control): the table in §1 still answers as it did;
- an oracle the conversions do not share: each answer's fields (`.year` through `.second`) are compared against the fields of the value it came from.

**G-SQL:**

- `CAST` of each kind, and the refusals;
- text cast to `DATE`, taking its date;
- `SERIAL OF (CAST(request_time AS DATE))`: two times on one day share a scope, and the next day starts at 1;
- a cast value fitting its column, which is what a `DATE`, `TIME` or `DATETIME` column accepts (`gsql_table_design.md` §3.1).

## 5. What building it found

- **A time given as an hour prints as `10`,** not `10:00`, because that is how gBASIC renders a time at hour precision read from text. The fixture's expectation was wrong, not the conversion.
- **Eight perturbations, all red in the end, one only after a check was added.** A `{time}` that kept the value's date fields beside its time compared **equal** to a clean time, since `=` on two times reads only their time. It therefore passed every check written. `serialize` writes every field, and is what G-SQL stores a row in, so the fixture now compares the two times' bytes. G-SQL's index key for a time encodes only the time, so no key could see this one. The `UNIQUE` case added for it stays as a statement of true behaviour, not as its guard.
- The other seven, each red in the language suite, the SQL corpus, or both:
  - `{date}` of a value refused again;
  - `{date}` keeping the time;
  - `{time}` of a date allowed;
  - `{datetime}` keeping its precision;
  - `{datetime}` of a time allowed;
  - `CAST` of text to `DATE` keeping its minutes (the SQL corpus alone);
  - the text form of `{date}` made to truncate too (the language suite's control alone).

