# G-SQL views (Phase 2)

**Status:** Decided 2026-11-01 (Matthew), not yet built. Views are the first Phase 2 item that writes something new into the database file. `gsql_design.md` §10I asks for a review before a storage format is committed, and §8 asks for "explicit dependency and version invalidation semantics". Both are settled in §3. They depend on two increments that come first: warnings carried to the program (`gsql_warning_design.md`) and `ALTER TABLE ... RENAME TO`.

## 1. What it would be

```sql
create view big_spenders (customer, spent) as
    select customer, sum(total) from orders group by customer having sum(total) > 100;
select c.name, b.spent from customer c join big_spenders b on b.customer = c.id;
drop view big_spenders;
```

A view is a named `SELECT`, used in `FROM` and `JOIN` like a table. It is read-only: there is no `INSERT`, `UPDATE` or `DELETE` through a view. `CREATE [OR REPLACE] VIEW name [(cols)] AS select`, and `DROP VIEW [IF EXISTS] name`.

**How it runs is already built.** A use of a view is a derived table whose query is the view's text, parsed and planned where it is used, exactly as `(select ...) AS name` is (`gsql_derived_design.md` §3, §4). Since it is planned at each use, it composes with the outer query as §8 asks, and it always sees the catalog as it is now.

## 2. What is decided without a ruling

- **A view reads no enclosing query** and is planned like a `WITH` query: only tables and other views.
- **Tables and views share one namespace.** `CREATE TABLE` refuses a view's name, and `CREATE VIEW` refuses a table's.
- **A view may read other views. A cycle is refused** at `CREATE` time, when the view is first planned.
- **`CREATE VIEW` plans the query before storing it,** so a view that does not bind is refused when it is created, not on first use.

## 3. The two decisions

### Storage: a new tree `\0view` (decided)

The tree maps a view's name to a `serialize` v3 record `{ format: 1, text, declared, columns, ok }`:

- `text` is the canonical print of its `SELECT`, which parses again;
- `declared` is the column list written in `CREATE VIEW v (a, b)`, empty if none;
- `columns` and `ok` are what the view's query produced, and whether it ran, at its last check.

- **No collision with tables:** every tree already begins with NUL (`gsql_table_design.md` §2).
- **Older binaries are unaffected:** one that opens such a database does not see the views, and its tables are untouched.
- **The storage engine's format does not change** (`gsql_storage_design.md` §3.1.5), since a tree is only keys inside it. What changes is the table layer's layout, which gains one row, and the independent layout reader, which gains a decoder.

The rejected alternative was an entry in `\0catalog` with a kind field. An older binary would read a view as a table schema and refuse the database.

### A change under a view: never blocked, always reported (decided)

Matthew, 2026-11-01. Views that fail long after something changed under them are a common real-world pain. So is being blocked from dropping and rebuilding a table, or renaming another into its place. A refusal fixes the first by causing the second. A **warning** fixes it at the moment of the change.

- **Nothing is blocked.** `DROP TABLE`, `ALTER TABLE` (`ADD COLUMN`, `RENAME TO`), `CREATE TABLE`, `DROP VIEW` and `CREATE OR REPLACE VIEW` all go through, whatever reads what they change.
- **Every such change re-checks every view.** Views read views, so a dropped table can break a view two levels up, and there are rarely many views. So no list of what a view reads is kept: after each schema change every view is planned again on the spot, which is cheap.
- **A warning is about what this change did:**
  - a view that ran before and does not now;
  - a view whose columns changed.

  A view that was already broken is not reported again, and one this change mended is silent. Each check's result is stored (`ok`, `columns`) so the next change can tell.
  - A view that no longer binds is reported in a **warning** naming the view and the reason, such as ``view `big_spenders` cannot run: table `orders` has no column `total` ``.
  - A view whose columns changed is reported in a warning naming the old and new lists.
- **Views bind by name.** The drop-and-rebuild pattern works: drop `t`, create a new `t` or rename another table to `t`, and the views run again. The re-check after that `CREATE` or `RENAME` says whether they fit the new shape.
- **Warning 2118,** source `view`, for both.
- **A view reading itself,** possible through `OR REPLACE` since views bind by name, is refused, naming the chain of views.
- **A broken view fails clearly when used:** ``view `v` cannot run: table `t` does not exist``, never a puzzling error from inside the view's own query.
- **`SELECT *` in a view reflects the table as it is now.** Its stored `columns` exist only so a change can be reported as a difference.
- **The program decides how strict to be,** through gBASIC's own warning modes, since the warning reaches the program as a gBASIC warning:
  - by default it prints and the work goes on;
  - under `on warning stop` it becomes an error. Inside `begin ... commit` that error arrives before the change is committed, so the program can roll back.
  - Under autocommit the change has already committed by the time the warning arrives. There it informs and cannot prevent.

## 4. How it would be tested

- **parse:** each form, and each refusal.
- **semantics:** a view used, joined and grouped; a view of a view; a cycle refused; `DROP TABLE` allowed with a warning naming each broken view; the view failing clearly afterwards; the drop-and-rebuild pattern working again with no warning; a column change under `*` reported as a difference; `OR REPLACE`; the shared namespace.
- **warnings as data:** each warning above reaches the program, and `on warning stop` inside a transaction lets the program roll the change back.
- **a reopen tier:** views survive closing and reopening the database, through the owner.
- **the independent layout reader** (`tests/gsql/table_layout.py`) gains a view, so the stored record is checked from the design's text, not only by our own reader.
- **the derived tier:** it carries over unchanged, since a view is a derived table at use.
