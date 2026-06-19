# gBASIC Project State

Last updated: 2026-06-07

This file is the compact source of truth for current implementation status.
Detailed language behavior belongs in `docs/reference.md`; completed
development phases are summarized in `docs/historical_development_archive.md`.

## Current Version

- Version: `0.1.0-dev`
- Implementation: C11
- Front end: hand-written lexer and Bison parser
- Runtime: tree-walking evaluator
- Build entry point: `make`

## Implemented Language

- variables, strict expressions, assignment, input, and output
- multiline and inline `if`/`else`
- `consider`, `while`, `break`, and `continue`
- array iteration with `for each` and compatible `for ... in`
- arrays, records, dynamic record access, and nested lvalue assignment
- functions, programs, libraries, `load`, labels, `goto`, and `gosub`
- assignment and comparison modifiers
- watchers, locks, and runtime error handling
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- JSON-like `encode` and `decode`

## Implemented Runtime Areas

- core type, conversion, string, array, record, and counting helpers
- file read/write/append/overwrite and file metadata
- file copy/move/delete and deterministic directory listing
- non-recursive directory creation/removal
- path manipulation helpers
- `read_lines`
- optional synchronous SQLite module backed by sqlite3
- optional synchronous PostgreSQL module backed by libpq
- optional synchronous WebClient module backed by libcurl
- built-in loopback WebServer using watcher-driven request/response queues
- optional GTK 3 GUI proof of concept through Stage 6A

## Optional Dependencies

- GTK 3 enables the GUI implementation.
- sqlite3 enables `load sqlite`.
- libpq enables `load pg`.
- libcurl enables `load webclient`.
- WebServer uses POSIX sockets and has no external HTTP dependency.

The interpreter builds without optional dependencies and reports unavailable
modules clearly at runtime.

## Verification

Primary suites:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
```

Integration suites:

```sh
./tests/run_webclient.sh
./tests/run_webserver.sh
./tests/run_sqlite.sh
GBASIC_POSTGRES_TEST=1 ./tests/run_postgres.sh
bash tests/run_bag_smoke.sh
```

GUI verification remains manual.

## Current Limitations

- experimental, non-optimized interpreter
- evolving diagnostics and module APIs
- SQLite is synchronous and has no prepared-statement API exposed to gBASIC
- PostgreSQL is synchronous and has no pooling or prepared-statement API
- WebClient is synchronous and string-body only
- WebServer is single-threaded, loopback-only, and intentionally minimal
- GUI supports existing-widget synchronization but not dynamic tree mutation

## Current Documents

- `README.md`: project overview and quick start
- `docs/reference.md`: implemented language and runtime behavior
- `docs/tutorial.md`: guided usage
- `docs/gBASIC_v0_1_core_design_and_grammar.md`: core grammar/design
- `docs/gbasic_site_plan.md`: Postgres-backed sample site plan
- `docs/sqlite_design.md`: SQLite API and future phases
- `docs/postgres_design.md`: PostgreSQL API and future phases
- `docs/webclient_design.md`: WebClient API and future phases
- `docs/webserver_design.md`: WebServer API and future phases
- `docs/gui_design.md`: GUI model and remaining work
- `docs/historical_development_archive.md`: completed phase history
