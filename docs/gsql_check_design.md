# G-SQL `CHECK` constraints

**Status:** designed 2026-11-01. `gsql_design.md` §13 question 7 (Matthew, 2026-10-29): "`FOREIGN KEY` and `CHECK` follow in the next [release], each with its own tests." Foreign keys shipped first (`gsql_fkey_design.md`); this is the other half.

## 1. What it is

```sql
create table account (
    id integer primary key,
    balance money(USD) check (balance >= cast('0' as money(USD))),
    opened date,
    closed date,
    constraint closes_after_opening check (closed is null or closed >= opened)
);
alter table account add constraint positive_id check (id > 0);
alter table account drop constraint positive_id;
```

- **A column's `CHECK (expr)`,** and a table's `[CONSTRAINT name] CHECK (expr)`, which may read any of the table's columns.
- **`ALTER TABLE ... ADD [CONSTRAINT name] CHECK (expr)`;** dropping one is the `DROP CONSTRAINT` foreign keys already have.
- **Named like a foreign key, in the same namespace,** since `DROP CONSTRAINT` must be able to say which: an unnamed check is `<table>_<column>_check`, or `<table>_check` for a table's, PostgreSQL's spelling.
- **The expression is gBASIC's,** as every G-SQL expression is (`gsql_sql_design.md` §10H): its comparisons, three-valued logic, money and dates are a program's.

**It is not:**

- a check that reads another table, or a subquery (refused: that is a foreign key's job or a trigger's);
- one that calls a function that is not deterministic (G-SQL has none yet);
- `NOT ENFORCED` (a check that is never checked documents nothing a comment cannot);
- deferred checking.

## 2. Semantics, which are SQL's

- **A row passes unless the check is FALSE.** `NULL`, meaning unknown, passes, as the standard requires: `balance >= 0` does not refuse a row whose balance is unknown. `NOT NULL` is how to refuse that.
- **A check whose expression raises,** such as a cross-currency comparison, refuses the row with the expression's own sentence, as a refusing `WHERE` does.
- **Checked before the row is written,** for `INSERT`, `UPDATE` and `INSERT ... SELECT`, in declaration order. A refusal is **`CHECK`**, naming the constraint.
- **`ADD CONSTRAINT ... CHECK` checks the rows already there,** and is refused, with how many fail, while any does.
- **The expression must be boolean-shaped where that is knowable:** a literal that is not a boolean is refused when the check is declared.

## 3. Storage, and changes under a check

- **A check is stored in the `\0fkey` tree**, which this increment renames in its documentation to the constraint tree. The record is `{ format: 1, kind: "check", table, text }`: `text` is the expression's canonical print, parsed and bound against the table each time it is used, as a view's text is. The tree's name stays `\0fkey`, since it is on disk; foreign-key records gain `kind: "foreign key"`, and one without a kind is read as a foreign key.
- **A check reads only its own table,** so the only changes under it are to that table:
  - `DROP TABLE` drops its checks;
  - `RENAME TO` carries them;
  - `ADD COLUMN` cannot break one.
- **The only check that can stop binding** is one of a rebuilt table without a column it reads: drop `t`, then rename a `t_new` lacking that column into its place. Following Matthew's rule for views and foreign keys, that is never blocked. It is reported (**2120**), and the check is not applied until it binds again.

## 4. How it is tested

- **parse:** each spelling printed back canonically; a subquery in a check refused.
- **semantics, hand-written answers:**
  - an insert and an update refused by a check, and the transaction still usable;
  - `NULL` passing;
  - a table check over two columns;
  - a raising check refusing with gBASIC's sentence;
  - `ADD CONSTRAINT` refused over existing rows, then accepted;
  - `DROP CONSTRAINT`;
  - a generated name;
  - `INSERT ... SELECT` checked per row;
  - a rebuilt table without a checked column, reported once.
- **the code:** `CHECK` reaches a program as `error.details.code`.
