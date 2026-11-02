# G-SQL: which table, column and constraint a refusal is about

**Status:** Shipped 2026-11-02, held by `run_gsql_module.sh`'s details tier. Asked for by the gBASIC books' volume 3
(`gbasic-books/docs/volume-3/gap-ledger.md`, G2).

## 1. The gap

A refused row raises with `error.details.code` (`CHECK`, `TYPE`, `NOT_NULL`,
`DUPLICATE`, `FOREIGN_KEY`) and a message that names everything:
*check `price_positive` refuses this row of `item`*, or *item.eff is date, and
the text given does not read as one*. But the names exist only inside the
text. A reject report that groups by reason, or a rule such as "a missing SKU
goes to Purchasing, a bad price back to the supplier", has to parse English to
route a row, and the English is not a contract.

## 2. What it does

Beside `code`, `error.details` carries, where they apply:

| field | when |
|---|---|
| `table` | every refusal of a row: the table the row was for |
| `column` | `TYPE` and `NOT_NULL`: the column the value was for |
| `constraint` | `CHECK` and `FOREIGN_KEY`: the constraint's name; `DUPLICATE`: the unique index's name, or `"primary key"` |

A field is **absent** where it does not apply, not empty, so
`has(e.details, "column")` is the question to ask. The message is unchanged.

## 3. How

`GsqlError` gains the three names. Both of the layers' fail helpers clear them,
so a name can never survive from an earlier refusal, and each refusal site that
already put a name in its message sets the field beside it. The owner sends
each one that is set as a field of the error reply, after `code`, `message`
and `position`; the module copies each into `details`. An older owner sends
none, and an older client ignores fields it does not read, so the wire
changes compatibly in both directions.

## 4. Evidence

`tests/gsql/error_details_test.bas`, 12 checks: `TYPE` (a date that is not one,
an inexact integer, money in another currency), `CHECK`, `NOT_NULL`,
`DUPLICATE` on the primary key and on a unique index, `FOREIGN_KEY` both ways
(a child naming no parent is about the child's table; deleting a parent still
referenced is about the parent's), an sql block's refusal, and an `EXPRESSION`
and a `SYNTAX` refusal naming none. A foreign key declared without a name is
named `TABLE_COLUMN_fkey` by G-SQL, and `constraint` gives that name.

What was planned:

A fixture refusing one row of each kind, asserting each field by value and
each inapplicable one absent, and a perturbation per layer: the owner not
sending the fields, and a fail helper not clearing them (a stale `column` from
a `TYPE` refusal carried onto the `CHECK` after it).
