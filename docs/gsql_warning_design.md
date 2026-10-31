# G-SQL warnings reach the program

**Status:** designed 2026-11-01. The first of three increments that views depend on (`gsql_view_design.md` §3), and the open item `gsql_sql_design.md` §8 records: "carrying [a warning] to the client as data, so a program can see it with `on warning`, needs a field in `ROWS` and `DONE`."

## 1. The defect it closes

A warning raised inside SQL, such as 2112 for an overflowing sum, is printed on the **owner's** stderr, once per distinct message for the owner's life. The owner is the program's child, so the line does reach the terminal, but nothing more:

- no position;
- no `on warning goto next`, so a program cannot ask;
- no `on warning stop`, so a program cannot make it strict;
- no `--json-diagnostics`, so an editor never sees it;
- a second program's identical overflow is never reported, because the owner already said it once.

## 2. What it is

- **The host keeps a warning until it is taken.** The owner's host still captures each warning a gBASIC operator raises inside SQL, under `WARN_MODE_NEXT`, as it does today (`gsql_sql_design.md` §8). It no longer prints it, and keeps it until the SQL layer takes it.
- **The SQL layer collects warnings per statement.** After each `run` and `fetch` it takes what the host holds, and adds warnings of its own. One message is said once per statement, across all its batches, so a million rows overflowing say it once.
- **The reply carries them.** A `warnings` field in `ROWS` and `DONE` holds an array of `{ code, message }`, present only when there are any, so every reply that has none is byte-identical to today's.
- **The `gsql` module raises each one as a gBASIC warning** at the call that received it: `gsql.exec`, `gsql.query`, a `sql` block, or the `for each` that fetched the batch. It keeps its own code, and `source` is `gsql`.
- **So everything a warning does in a program now holds for these:**
  - by default it prints as `warning: <message> at file:line:col [code]`;
  - `on warning ignore` silences it;
  - `on warning goto next` keeps it in `warning`;
  - `on warning stop` turns it into an error, which inside `begin ... commit` arrives before the commit, so the program can roll back;
  - under `--json-diagnostics` it is JSON.
- **Deduplication is by site and message.** A printed warning is normally reported once per call site. Here one site, a `gsql.exec` in a loop, can carry many different warnings, such as one per view a series of drops breaks. So these are reported once per site **and message**: the same overflow in a loop is still said once, and two different broken views are both said.
- **The client knows the codes it raises.** It raises 2112 and the view warning 2118 by name, so `docs/ai/ERRORS.md`'s table and the docs gate see them. Any other code raises as itself.
- **An `ERROR` reply carries no warnings.** The refusal is what the program must see, and one raised after a warning would bury it.

## 3. How it is tested

- **`run_gsql_sql.sh`:** the semantics tier's overflow case now reaches stderr through the result rather than the host's print. The existing check, `warning: ..., in a G-SQL expression [2112]` with no template position, is kept, and now proves the new path.
- **`run_gsql_module.sh`, end to end through the real owner:**
  - an overflowing `sum` warns at the program's own line;
  - `on warning goto next` reads `warning.code` 2112;
  - `on warning stop` inside a transaction raises at an `UPDATE` whose `SET` overflows, and the program rolls back, which leaves the row unchanged;
  - `on warning ignore` prints nothing;
  - the same overflow in a loop prints once;
  - a reply with no warning is unchanged.
