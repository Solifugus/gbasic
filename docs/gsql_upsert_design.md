# G-SQL `INSERT ... ON CONFLICT`

**Status:** designed and built 2026-11-07 (`plan_conflict` in `src/gsql/sql_plan.c`; `conflict_probe` and `upsert_row` in `src/gsql/sql_exec.c`; `gsql_row_conflict` in `src/gsql/table.c`; `tests/run_gsql_sql.sh`). Added for Transward, whose `setting_put` is an upsert and whose three SQLite `INSERT OR IGNORE/REPLACE` statements are upserts spelled SQLite's way (`docs/gsql_transward_corpus.md`).

## 1. What it is

PostgreSQL's spelling, which SQLite also accepts:

```sql
insert into settings (key, value, updated_at) values (:key, :value, :at)
    on conflict (key) do update set value = excluded.value, updated_at = excluded.updated_at;

insert into idempotency_keys (key, job_id, run_id) values (:key, :job, :run)
    on conflict do nothing;

insert into counters (k, n) values (:k, 1)
    on conflict (k) do update set n = counters.n + 1 where counters.n < 1000;
```

- **`ON CONFLICT (cols)` names the constraint it answers.** That is the primary key's columns, or a unique index's, in any order. Columns that are neither are refused (`SCHEMA`). `ON CONFLICT ON CONSTRAINT name` and a `WHERE` on the conflict columns are refused by name.
- **`DO NOTHING`** leaves the row already there as it is, and the proposed row is not written. With no columns named it answers a conflict in any unique key.
- **`DO UPDATE SET c = v, ... [WHERE cond]`** changes the row already there. It needs the columns named, as PostgreSQL does.
  - **Two rows are in view.** The row already there is read by the table's own name, and the row the `INSERT` proposed as `excluded`, with its defaults and generated values filled in.
  - **A bare column could be either, so it is refused,** as PostgreSQL refuses it. The planner's sentence says what to write: ``column `n` is in both settings and excluded: write settings.n or excluded.n``. SQLite reads a bare name as the existing row. A statement written for SQLite that leaves one bare gets that sentence, not a different answer.
  - **`SET c = DEFAULT`** is the column's default.
  - **A `WHERE` that is not true** leaves the row as it was. Unknown counts as not true.
- **The count** is rows inserted plus rows updated. A row left by `DO NOTHING`, or by a `WHERE` that was not true, is not counted. This is PostgreSQL's count and SQLite's.
- **It works with `VALUES` and with `INSERT ... SELECT`.**
- **SQLite's own spellings are refused with the translation:** `INSERT OR IGNORE` is `ON CONFLICT DO NOTHING`. `INSERT OR REPLACE` is `ON CONFLICT (key) DO UPDATE SET ...`, which changes the row there rather than deleting it and inserting another, so a cascading child is not deleted by it.

## 2. What happens to one proposed row

1. Its values are converted into the columns, its defaults resolved, and its `RANDOM` and `SERIAL` values drawn, as for any insert. Then its `CHECK` constraints are tested.
2. **The named constraint is probed before the row goes in:** `gsql_row_conflict` reads the row a key would collide with, as PostgreSQL's arbiter check does. So when the proposed row repeats a value in the named key and also in another, the named one decides. A conflict only in a key it did not name is refused as a plain `INSERT` would refuse it (`DUPLICATE`).
3. **On no conflict** the row is inserted as any row is, its foreign keys checked.
4. **On a conflict,** `DO NOTHING` moves on. `DO UPDATE` evaluates its `WHERE` and its values against the two rows. The changed row is then checked as an `UPDATE`'s is: `CHECK`, the foreign keys of the columns it changes, and unique indexes. Then it is written.
5. **The proposed row is not written, so its serials are not taken.** A `SERIAL` stays gapless across upserts (`gsql_generated_design.md`): the next inserted row takes the number the skipped one would have had.

## 3. What is refused

- **Changing one row twice in one statement.** That happens when two proposed rows hold one key value, or when one collides with a row the statement itself just inserted. PostgreSQL refuses it. SQLite applies both in order, so the last proposal wins. G-SQL refuses (`DUPLICATE`), since which proposal should win is the statement's to say and it did not. `DO NOTHING` has no such question: a second proposal meets the first and is left.
- **`DO UPDATE` setting a primary-key column or a `SERIAL` column.** Not in this release. The row a conflict found keeps its key, so no row moves and no counter's scope changes under an upsert.
- **A subquery, an aggregate or a window function in `DO UPDATE`.** Not in this release. The `SET` values and `WHERE` read only the two rows.
- **`RETURNING`** was refused here until later the same day; it now returns each row the upsert wrote (`docs/gsql_returning_design.md`).

## 4. How it is built

- **The parser** keeps the conflict columns, the `SET` list (shared with `UPDATE`'s) and the `WHERE` on the `INSERT`.
- **The planner** resolves the named constraint to `-1` (the primary key) or `i + 1` (unique index `i`). It plans the `SET` values and the `WHERE` as one `SELECT` over `FROM t, t AS excluded`, so names bind and are refused by the ordinary path and nothing new is needed for qualification or ambiguity. The executor evaluates that plan's outputs with both scans pointed at rows in hand, as a `CHECK` is evaluated against a row about to be written.
- **The rows a statement writes are remembered by key,** only for `DO UPDATE`, to find a second change of one row.

## 5. How it is tested

- **parse:** the canonical print of each form, and each refusal (`RETURNING`, `ON CONSTRAINT`, MySQL's `ON DUPLICATE KEY`, a missing action, SQLite's two spellings).
- **semantics, hand-written answers:**
  - `excluded` and the table's name;
  - parameters;
  - a `WHERE` true and not true;
  - `DEFAULT`;
  - `DO NOTHING` with no columns, and over a row the same statement wrote;
  - one row changed twice, refused and undone;
  - the ambiguity sentence;
  - each planning refusal;
  - a composite unique index named in another order;
  - a conflict in a key not named;
  - a `CHECK` and a unique index refusing the update;
  - `INSERT ... SELECT`;
  - a serial staying gapless.
- **a SQLite differential** (`tests/gsql/gen_dml_diff.py`, shared with `ON DELETE CASCADE`): random upserts on the primary key and on a unique column, `DO NOTHING` and `DO UPDATE` with and without `WHERE`, each table read back after each statement. Where the two engines differ by design, the generator stays out, and its header says where: a bare column, two proposals for one key, and statements SQLite refuses part way.
- **Perturbations,** each turning a tier red:
  - the probe removed;
  - the two rows swapped;
  - `WHERE` ignored;
  - the second-change check removed.

  The last is caught only by the hand-written cases, since the differential avoids that shape by design.
