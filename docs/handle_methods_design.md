# Methods on handles: `h.verb(args)` is `module.verb(h, args)`

**Status:** Designed and shipped 2026-11-01 for G-SQL connections and blocks and Qt windows, then the same day for every other handle kind (`handle_method_module` and `handle_method_call` in `src/eval.c`; `tests/run_handle_methods.sh` over `tests/handle_methods/`). From Matthew's direction the same day: *"Originally, we didn't really use methods because early on, functions were not first class. I think we should start taking advantage of this."* The `module.verb(handle)` style *"bothered me with qt as well."* His rulings: *"yes, the variable wins, and start with gsql and qt. The whole language will be better for it."*

## 1. What existed

gBASIC already had methods, dispatched by what the receiver is at run time (`docs/reference.md`, *Function values*). A function value in a record field is a method: `account.deposit(100)` binds `this` to the record. For `x.y(args)` the interpreter first asks whether `x` is a variable holding a record whose field `y` is a function, and only then reads `x` as a library, so a variable already won over a library of the same name.

What was missing was a method on a **handle**: a connection, a block, a window. Those were reached only as `module.verb(handle, ...)`.

## 2. The rule

**A handle's methods are its module's verbs.** `h.verb(args)` means `module.verb(h, args)`.

| handle | module | so |
|---|---|---|
| a G-SQL connection (`gsql.connect`, a `database` block) | `gsql` | `db.close()`, `db.begin()`, `db.on(block)` |
| an `sql` block | `gsql` | `add.exec(row)`, `shares.query()` |
| a `window` declaration | `qt` | `app.read("who", "text")`, `app.set("save", "enabled", false)`, `app.open()` |
| a `sqlite`, `pg`, `odbc` or `ldap` connection | that module | `db.query(sql)`, `db.exec(sql, params)`, `dir.bind(dn, password)` |
| an `xml` reader | `xml` | `rd.skip_to("b")`, `rd.subtree()`, `rd.close()` |
| a process | `process` | `p.poll()`, `p.read()`, `p.wait()`, `p.stop()` |
| an `http` handle | `http` | `h.poll()`, `h.read()`, `h.wait()` |
| an `xlsx` workbook | `xlsx` | `wb.sheets()`, `wb.cells("Sheet1")`, `wb.save(path)` |

- **Derived, not listed.** The methods are the module's own verbs, so a verb added to a module is a method at once and there is no second table to keep in step.
- **`module.verb(h)` keeps working.** The two spellings are one call: the method form hands the receiver to the module as its first argument, and the module does everything else, refusals included. A verb whose first argument is not the handle (`db.connect()`) is refused by that verb, in its own words.
- **The receiver is evaluated once**, whether a name (`db.close()`) or an expression (`blocks[0].exec()`, `make_db().close()`).
- **A record's own function field still wins.** A record method is the programmer's and a handle method is the module's.
- **The variable wins.** A variable named `qt` or `gsql` holding a record or a handle is a receiver, as a record variable already was.

A window is a record (`kind: "window"`), not an opaque handle, so its methods come from that kind. The same rule can serve any record a module makes and recognises.

## 3. Not yet

- **A timer** is a record carrying an `id` and no `kind`, so nothing tells it from any other record; `timer.cancel(t)` stays the spelling until a timer carries a kind.
- **`pg` is untested here:** its kind is wired like the others, and testing it needs a server (`run_postgres.sh` is opt-in).

## 4. Tests

- G-SQL: a connection and a block each called both ways, answering alike; a method on a block in an array, evaluated once; a record's own method winning over a verb of the same name; `db.connect()` refused.
- Qt: a window opened, read, set and invoked through methods, against the module form.
- sqlite, an xml reader, a process, an http handle (to a port nothing listens on, so no network) and an xlsx workbook; an odbc connection over the SQLite3 driver and an ldap one answering `unreachable` as a value.
