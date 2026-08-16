# gBASIC Project State

Last updated: 2026-07-05

This file is the compact source of truth for current implementation status.
Detailed language behavior belongs in `docs/reference.md`; completed
development phases are summarized in `docs/historical_development_archive.md`.

## Current Version

- Version: `0.1.0-rc1`
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
- first-class function values (references) that can be stored, passed, and called
- Policy-Based Inheritance object model (`new`, `constructor`, methods via `this`)
- shared-nothing actors over `spawn`/`send`/`receive` with monitor/link
- assignment and comparison modifiers
- watchers, locks, and runtime error handling
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- binary-safe, Unicode-aware strings, codepoint operations, and byte builtins
- bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`)
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
- optional XML module backed by libxml2 (tree parse, navigation, encode,
  lenient HTML, constant-memory streaming reader)
- optional cryptography builtins backed by libcrypto (hashing, HMAC, AES-GCM,
  Ed25519) plus a `crypto` stdlib library (JWT/HS256, signed cookies, CSRF)
- optional GTK 3 GUI proof of concept through Stage 6A

## Standard-Library Toolkits (pure gBASIC)

- **statistics** — descriptive/inferential statistics, regression and the GLM
  suite, mediation/moderation, time-series and econometric diagnostics, and
  finance metrics; verified against reference implementations.
- **EDGAR securities-analysis suite** — `edgar` (acquisition), `fundamentals`,
  `forensics` (accruals/Beneish/Piotroski/Altman/dilution/flags/events),
  `insiders` and `ownership` (Form 4 / 13F / 13D-G), `mdna` (MD&A + LLM panel),
  `llm` (chat client), and `screener` (whole-market scoring). See
  `docs/edgar_tutorial.md` and `docs/edgar_reference.md`.

## Optional Dependencies

- GTK 3 enables the GUI implementation.
- sqlite3 enables `load sqlite`.
- libpq enables `load pg`.
- libcurl enables `load webclient` (and the EDGAR/LLM network paths).
- libxml2 enables `load xml`.
- libcrypto enables the cryptographic builtins.
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
./tests/run_gbasic_site.sh
GBASIC_SITE_POSTGRES_TEST=1 ./tests/run_gbasic_site_postgres.sh
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

- `README.md`: project overview, quick start, and the documentation index
- `docs/gbasic-design.md`: consolidated language and core-runtime design
- `docs/reference.md`: implemented language and runtime behavior
- `docs/tutorial.md`: guided usage
- `docs/pbi_design.md`: Policy-Based Inheritance object model (implemented)
- `docs/first_class_functions_design.md`: function values (implemented)
- `docs/unicode_design.md`: string/Unicode model (implemented)
- `docs/multiprocessing_design.md`: actors (implemented)
- `docs/bitwise_design.md`: bitwise builtins (implemented)
- `docs/crypto_design.md`: cryptography builtins + library (implemented)
- `docs/sqlite_design.md`: SQLite API and future phases
- `docs/postgres_design.md`: PostgreSQL API and future phases
- `docs/webclient_design.md`: WebClient API and future phases
- `docs/webserver_design.md`: WebServer API and future phases
- `docs/xml_design.md`: XML module (implemented)
- `docs/gui_design.md`: GUI model and remaining work
- `docs/statistics_design.md` + `docs/cookbook_*.md`: statistics library and cookbooks
- `docs/edgar_tutorial.md`, `docs/edgar_reference.md`, `docs/edgar_design.md`: EDGAR suite
- `docs/PROGRESS.md`: per-work-package build ledger with evidence
- `docs/gbasic_dogfood_notes.md`: language/runtime friction found while building examples
- `docs/gbasic_site_plan.md`, `docs/gbasic_site_auth_plan.md`, `docs/gbasic_site_deployment.md`: Postgres-backed sample site
- `docs/historical_development_archive.md`: completed phase history
