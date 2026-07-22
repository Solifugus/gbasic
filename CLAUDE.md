# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

gBASIC is an experimental BASIC-family language implemented as a tree-walking
interpreter in C11. The single binary `gbasic` lexes, parses, and evaluates
`.bas`/`.gb` source files (both extensions are used interchangeably). Version is
`0.1.0-dev`. See `README.md` for the language feature surface and `docs/` for
design/reference documents.

## Build & run

```sh
make                       # build ./gbasic (runs bison, then cc)
make clean && make         # full rebuild
./gbasic program.bas       # run a program
./gbasic --tokens program.bas   # dump lexer tokens
./gbasic --ast program.bas      # dump parsed AST
./gbasic --add-loads program.bas # print source with suggested `load` lines
./gbasic --json-diagnostics program.bas # emit parse/runtime diagnostics as JSON on stderr
./gbasic --version
sudo make install          # install to /usr/local (binary + stdlib); PREFIX overridable
```

Build requires a C11 compiler, `make`, and `bison`. Optional native modules are
detected at build time via `pkg-config` and gated behind `HAVE_*` macros in the
`Makefile` (`HAVE_GTK`, `HAVE_LIBPQ`, `HAVE_SQLITE3`, `HAVE_LIBCURL`,
`HAVE_LIBXCRYPT`, `HAVE_LIBCRYPTO`, `HAVE_LIBXML2`, `HAVE_GIR`). The interpreter always builds; a missing dependency turns the
affected feature into a clean runtime error rather than a build failure. When
adding code that touches an optional library, guard it with the matching
`#if HAVE_*` block (the existing module code in `src/eval.c` is the pattern).

`GBASIC_PATH=stdlib` tells the interpreter where to resolve `load`ed standard
libraries (`stdlib/*.bas`); set it when running from the source tree, e.g.
`GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas`. After `make install` this is
unnecessary: the install path (`$(PREFIX)/share/gbasic/stdlib`, default
`/usr/local/share/gbasic/stdlib`) is compiled in as `GBASIC_DEFAULT_STDLIB` and
searched as a fallback when `GBASIC_PATH` is unset or doesn't resolve a library.
`GBASIC_PATH` still wins when set, so it remains the dev override.

## Architecture

The pipeline is the classic lex → parse → AST → walk, but note where the weight
sits:

- `src/lexer.c` (`include/lexer.h`) — hand-written lexer. Has stateful modes
  (`modifier_content_mode`, `lens_content_mode`, `consider_depth`) because
  modifier `(...)=` clauses and `consider` blocks need context-sensitive
  tokenization. Token list lives in `lexer.h`.
- `src/parser.y` — Bison grammar (`-d` generates `parser.tab.c`/`.h`). Builds the
  AST. Regenerated automatically by `make` when `parser.y` changes.
- `src/ast.c` (`include/ast.h`) — AST node definitions and constructors.
  `AstStmtKind`/`AstExprKind` enumerate every node type.
- `src/eval.c` — **the entire runtime, ~18k lines.** This is the file you will
  most often edit. It contains the `Value` model, environment/scope handling,
  watchers, locks, error handling, the date/time/money/file value types, AND the
  full implementation of every optional module — `sqlite`, `pg` (PostgreSQL),
  `webclient`, `webserver`, `xml` (libxml2), the GTK 3 `gui`, the `gi`
  GObject-Introspection bridge (GTK 4), and the libcrypto crypto builtins — each
  inside `#if HAVE_*` guards. Module dispatch (`sqlite.query`, `pg.exec`,
  `gi.new`, etc.) is resolved here, not in separate files.
- `src/builtins.c` (`include/builtins.h`) — **only a name registry.** A flat list
  of builtin function names so the parser/evaluator can recognize them. The
  actual implementations live in `src/eval.c`. To add a builtin: register the
  name here, implement the behavior in `eval.c`.
- `src/main.c` — CLI entry point and flag handling.

## Tests

Tests are golden-file based: a source file plus a sibling `.out` holding the
expected stdout. Output is compared verbatim (string equality), so update the
`.out` when intended output changes. Negative tests pair a `.bas` with a `.err`.

```sh
./tests/run_examples.sh     # baseline positive suite (runs `make clean && make` first)
./tests/run_negative.sh     # error/diagnostic suite
./tests/run_sqlite.sh       # skips cleanly if sqlite3 dev files absent
./tests/run_webclient.sh    # loopback fixture; skips if env unavailable
./tests/run_webserver.sh
./tests/run_gbasic_site.sh
bash tests/run_bag_smoke.sh
./tests/run_json_diagnostics.sh  # --json-diagnostics golden (also asserts default byte-exact)
./tests/lsp/run_lsp.sh      # gbasic-lsp: position-transcode unit + framed JSON-RPC handshake golden
./tests/run_gi.sh           # gi.* GObject-Introspection bridge (headless Gio types); skips if libgirepository-2.0 absent
./tests/run_native_platform.sh # Native Application Platform (headless; GTK4/GtkSource via gi); skips if those typelibs absent
./tests/run_process.sh      # process.run general process API (NAP-6); GI-independent, never skips
./tests/run_native_editor.sh # NAP-7 SourceEditor/gtk.bas/gbasic.lang (GBASIC_PATH=stdlib); headless tier always, display smoke when a display exists; skips if GtkSource typelib absent
./tests/run_native_workbench.sh # NAP-8 platform spike (examples/native_workbench); inspect/process always, async gated on libgirepository, full-UI smoke gated on GTK4/GtkSource typelibs + a display
./tests/run_gui_parse.sh    # parse-only headless smoke for examples/gui + examples/gi + examples/native_editor + examples/native_workbench (parse, don't run; no display needed)
./tests/run_docs_gate.sh    # executable-docs gate: every docs/ai/COOKBOOK.md file reference exists and is wired into a suite
GBASIC_POSTGRES_TEST=1 PGDATABASE=my_test_db ./tests/run_postgres.sh   # opt-in
```

`gbasic-lsp` is deliberately kept out of the default `all` target, so a routine
test pass must build it explicitly or it will silently rot. Use `make dev`
(builds every binary: `gbasic`, `libgbasic.a`, and `gbasic-lsp`) as the developer/
CI entry point; `tests/lsp/run_lsp.sh` also runs `make gbasic-lsp` itself.

`run_examples.sh` runs a hardcoded list of cases (it does not auto-discover), so
when adding an example test, add its filename to that list as well as creating
the `.out`. To run one case by hand: `./gbasic examples/foo_test.bas` and diff
against `examples/foo_test.out`. Module runners skip rather than fail when their
native dependency is missing. GUI testing is manual (needs a display).

## Conventions

- Optional-dependency code must stay behind its `#if HAVE_*` guard and degrade to
  a runtime error when the feature is compiled out.
- Design docs in `docs/` may describe unimplemented future work; check the
  implementation-status documents (and `eval.c`) before treating a proposal as a
  shipped feature. `docs/historical_development_archive.md` records completed
  phases.
- The `use`/`--add-uses` syntax is legacy and retained only for compatibility;
  prefer `load`/`--add-loads`.

## House rules

Identical to `AGENTS.md` — the two entry files carry the same rules:

- **Before writing gBASIC code**, read `docs/ai/START-HERE.md` and follow it
  (`UNLEARN.md` first). gBASIC diverges from QBasic/VB intuition in ways that fail
  silently.
- **When you work around a gBASIC limitation or surprise**, append an entry to
  `/DOGFOOD.md` using its template *before continuing*.
- **Evidence standards:** tests-first where feasible; keep goldens byte-exact (a
  behavioral change that moves a golden is a deliberate, listed rebaseline);
  measure, don't assume; report what you could not verify. Never mark anything
  "verified".

## EDGAR suite work
Governed by docs/edgar_suite_development_plan.md. One WP per session,
assigned by Matthew. Follow its §1 session protocol exactly: read only
the WP's Read list, stop at the WP boundary, record verbatim evidence in
docs/PROGRESS.md. Never mark anything `verified`. No network in tests.
