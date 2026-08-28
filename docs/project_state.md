# gBASIC Project State

Last updated: 2026-08-26 (0.1.0-rc8)

This file is the compact source of truth for current implementation status.
Detailed language behavior belongs in `docs/reference.md`; completed development
phases are summarized in `docs/historical_development_archive.md`; the full
document list, with a status column, is `docs/README.md`.

## Current Version

- Version: `0.1.0-rc8`
- Implementation: C11
- Front end: hand-written lexer and Bison parser
- Runtime: tree-walking evaluator
- Build entry point: `make`

## Implemented Language

- variables, strict expressions, assignment (including the compound forms
  `+=`, `-=`, `*=`, `/=`), input, and output
- multiline and inline `if`/`else`
- `consider`, `while`, the post-test `do ... until c`, and `break`/`continue` —
  each optionally naming the loop it means (`break x`, `continue x`)
- array iteration with `for each` and compatible `for ... in`; a counted `for`
  closing with `end for`, `next`, or `next <name>`
- arrays, records, dynamic record access, and nested lvalue assignment
  (records are copy-on-write; a keyword may be a field name, in a literal and
  after a dot)
- functions, programs, libraries, `load`, labels, `goto`, and `gosub`
- first-class function values (references) that can be stored, passed, and called
- Policy-Based Inheritance object model (`new`, `constructor`, methods via `this`)
- shared-nothing actors over `spawn`/`send`/`receive` with monitor/link
- assignment and comparison modifiers, written in braces (`p{USD} = 19.95`)
- watchers, locks, and a **frame-scoped error model** — `on error goto next`,
  `on error goto LABEL`, `if error then` (`docs/error_model_design.md`)
- a **warning channel** beside it — `on warning print|ignore|stop|goto next`,
  `if warning then`, `warning(…)` (`docs/warning_model_design.md`)
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- binary-safe, Unicode-aware strings, codepoint operations, and byte builtins
- regular expressions as a **value kind**, overloading `contains`/`replace`/
  `split`, plus `match`/`match_all`
- bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`)
- `mod` (floored), `concat`, `merge`
- three serializers with stated jobs: `json_encode`/`json_encodable` (strict RFC
  8259, for anything leaving gBASIC), `encode`/`decode` (+ non-raising
  `try_decode`) for gBASIC-to-gBASIC round trips, and `serialize`/`deserialize`
  for exact typed round trips
- `server` blocks — a routed HTTP server as a declarative block

## Implemented Runtime Areas

- core type, conversion, string, array, record, and counting helpers
- file read/write/append/overwrite and file metadata
- file copy/move/delete and deterministic directory listing
- non-recursive directory creation/removal, `atomic_replace`, `file_type`
- path manipulation helpers
- `read_lines`, `monotonic()`, `reflect.*`, `source_outline`
- process control — `process.run`, and `process.start`/`poll`/`read`/`wait`/
  `stop`/`release`/`which` for a live child. Every child is bound to the
  interpreter's lifetime by the kernel, so none survives it, even a `kill -9`
- optional synchronous SQLite module backed by sqlite3
- optional synchronous PostgreSQL module backed by libpq
- optional synchronous WebClient module backed by libcurl
- a built-in WebServer: TLS, routing, streaming, hardening, and a process worker
  pool with listener transfer over `LISTEN_FDS`
- optional XML module backed by libxml2 (tree parse, navigation, encode,
  lenient HTML, constant-memory streaming reader)
- optional cryptography builtins backed by libcrypto (hashing, HMAC, AES-GCM,
  Ed25519, and the PBKDF2/scrypt key derivations that turn a passphrase into key
  bytes) plus a `crypto` stdlib library (JWT/HS256, signed cookies, CSRF)
- optional GTK 3 GUI proof of concept through Stage 6A, and a generic
  GObject-Introspection bridge (`gi`, libgirepository) with GTK 4 as the first
  target — what gBASIC Studio is built on
- diagnostics: `--json-diagnostics` emits one JSON object per diagnostic and
  nothing else, and any reported diagnostic is a nonzero exit

## Standard-Library Toolkits (pure gBASIC)

- **statistics** — descriptive/inferential statistics, regression and the GLM
  suite, mediation/moderation, time-series and econometric diagnostics, finance
  metrics, survival analysis (Kaplan-Meier, log-rank, Cox), meta-analysis,
  exploratory factor analysis, event studies, and causal inference (DiD,
  IV/2SLS); verified against reference implementations, published trial results,
  or — where a method can be right in the estimate and wrong in the uncertainty
  — against a second independent derivation rather than a golden.
- **market data** — `market`, daily price history as a frame, with pluggable
  providers and an offline fixture seam so tests never touch the network.
- **spreadsheets** — `xlsx` (read/write plus a formula engine measured against
  15,871 real workbooks; `xlsx.try_open` reports a bad workbook as a value so a
  batch survives one), `grid`, `frame`/`dbframe`, `consolidate`, `chart`.
- **EDGAR securities-analysis suite** — `edgar` (acquisition), `fundamentals`,
  `forensics` (accruals/Beneish/Piotroski/Altman/dilution/flags/events),
  `insiders` and `ownership` (Form 4 / 13F / 13D-G), `mdna` (MD&A + LLM panel),
  `llm` (chat client), and `screener` (whole-market scoring). See
  `docs/edgar_tutorial.md` and `docs/edgar_reference.md`.
- **application platform** — `web`, `gtk`/`gtkui`/`datagrid`/`sourceview`,
  `filetree`, `persist`, `text`, `dates`, `ari`, `matrix`.

## Optional Dependencies

- GTK 3 enables the GUI implementation; libgirepository enables `gi` (GTK 4).
- sqlite3 enables `load sqlite`.
- libpq enables `load pg`.
- libcurl enables `load webclient` (and the EDGAR/LLM network paths).
- libxml2 enables `load xml`.
- libcrypto enables the cryptographic builtins.
- WebServer uses POSIX sockets and has no external HTTP dependency; TLS uses
  libssl.

The interpreter builds without optional dependencies and reports unavailable
modules clearly at runtime.

## Verification

`tests/` holds the suites; each `run_*.sh` is self-contained, builds first, and
prints PASS/FAIL per case. `tests/run_all.sh` discovers every suite by glob and
is the gate; the useful thing to know is which to run while working rather than
a list that goes stale:

```sh
make clean && make
./tests/run_core.sh ./tests/run_examples.sh ./tests/run_negative.sh   # the floor
```

Everything else is topical — `run_error_model.sh`, `run_warning_model.sh`,
`run_parse_exit.sh`, `run_process_lifetime.sh`, `run_web_*.sh`, `run_xlsx*.sh`,
and so on. Suites needing a service or an optional dependency SKIP cleanly
rather than fail. `tests/run_docs_gate.sh` checks the documentation index and
the performance claims. GUI verification beyond `run_gui_parse.sh` is manual.

gBASIC Studio (`~/development/gbasic-studio`) is the largest dogfooding
consumer; its `tests/run_studio.sh` runs against whatever interpreter `GBASIC`
points at, so it doubles as an integration suite for this repository.

## Current Limitations

- experimental, non-optimized interpreter
- evolving diagnostics and module APIs
- non-raising `try_*` twins (`try_decode`, `xlsx.try_open`, `process.which`,
  `has_builtin`) predate the frame-scoped error model and remain, now as an
  ergonomic choice rather than a necessity — they report *where* an input is
  malformed, which a caught raise does not
- SQLite is synchronous and has no prepared-statement API exposed to gBASIC
- PostgreSQL is synchronous and has no pooling or prepared-statement API
- WebClient is synchronous
- GUI (GTK 3) supports existing-widget synchronization but not dynamic tree
  mutation; the `gi`/GTK 4 path does not share that limit
- there is no dedicated map type; a record serves as one (hash-indexed since
  PLAT-RECIDX, so lookup is not linear, but the ergonomics are a record's)
- `DOGFOOD.md`'s "Open — worth fixing" list is **empty** as of 0.1.0-rc8; what
  remains there is the "accepted as documented limitations" section, which is
  doctrine rather than a to-do list

## Current Documents

See `docs/README.md`. It lists every document with a **status** column
(Shipped / Proposal / Partial / Record) and `tests/run_docs_gate.sh` fails if a
document is missing from it — which is the protection this section used to lack:
a second, hand-maintained index here went thirteen documents and seven weeks out
of date without anything noticing.
