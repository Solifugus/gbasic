# G-SQL views (Phase 2): a proposal

**Status:** Proposal, written 2026-11-01. **Not built, awaiting Matthew's ruling.** Views are the first Phase 2 item that writes something new into the database file, and `gsql_design.md` §10I asks for a review before a storage format or a grammar is committed. §8 asks for "explicit dependency and version invalidation semantics", which are decisions a reader of the database will live with. Two questions need a ruling (§3). Everything else follows from the increment just built (`gsql_derived_design.md`).

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

## 3. The two questions

### Q1. Where is a view stored?

- **(a) Recommended: a new tree `\0view`**, mapping a view's name to a `serialize` v3 record `{ format: 1, text, columns, reads }`. Every tree already begins with NUL (`gsql_table_design.md` §2), so this cannot collide with a table. An older binary that opens such a database simply does not see the views, and its tables are untouched. The storage engine's format (`gsql_storage_design.md` §3.1.5) does not change at all, since a tree is only keys inside it. What changes is the table layer's layout (`gsql_table_design.md` §2), which gains one row, and the independent layout reader gains a decoder for the view record.
- **(b) An entry in `\0catalog` with a kind field.** This was rejected. An older binary would read a view's record as a table schema and refuse it as `FORMAT` or worse, which turns a feature it does not know about into a database it cannot open.

### Q2. What happens to a view when what it reads changes?

- **(a) Recommended, PostgreSQL's rule:**
  - `DROP TABLE` and `DROP VIEW` are refused while a view reads the thing being dropped, and the refusal names the view. The `reads` list is stored for this.
  - A view's column list is fixed when it is created. Its stored `columns` are what `*` expanded to then.
  - A use whose query now yields a different column list is refused, naming the view and saying to recreate it. That can happen after `ADD COLUMN` under a `SELECT *`.
  - There is no `CASCADE` in this release.
- **(b) Late binding.** A view is only its text: dropping a table is allowed, and the view fails when next used. This is simpler, with no `reads` and no `columns`. But a view's answer can change shape without anyone being told, and a dropped table surfaces as an error far from the cause.

The recommendation in both cases is the one that refuses rather than guesses. It costs one tree and two stored lists.

## 4. How it would be tested

- **parse:** each form, and each refusal.
- **semantics:** a view used, joined and grouped; a view of a view; a cycle refused; `DROP TABLE` refused while a view reads the table; the refusal after `ADD COLUMN` under `*`; `OR REPLACE`; the shared namespace.
- **a reopen tier:** views survive closing and reopening the database, through the owner.
- **the independent layout reader** (`tests/gsql/table_layout.py`) gains a view, so the stored record is checked from the design's text, not only by our own reader.
- **the derived tier:** it carries over unchanged, since a view is a derived table at use.
