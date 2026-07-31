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
./gbasic --line-buffered program.bas # flush stdout per completed line (pipes block-buffer by default)
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
./tests/run_process.sh      # general process API: process.run (NAP-6) + PLAT-PROC live child control (process.start/poll/read/wait/stop/release). GI-independent, never skips. PLAT-PROC cases are deterministic by construction, not timing: children that must be observed mid-run block on a gate file the parent creates. Covers incremental non-blocking reads, nonzero exit, >64KB on both streams without deadlock, stop-by-escalation (SIGTERM-only vs force_after->SIGKILL, incl. a child that ignores SIGTERM), interleaved read/poll, byte fidelity across a mid-codepoint chunk boundary, use inside a spawned actor, and an abandoned-handle fd/zombie audit (SKIPs the zombie tier if `ps` is absent)
./tests/run_stream.sh       # PLAT-STREAM `--line-buffered`: the opt-in prompt-stdout flag. Headless, GI-independent, never skips (bar the valgrind tier). Gate-file determinism throughout — child fixtures publish a READY file only after the print under test has executed, so a read at that instant is provably post-print. Covers: streaming mid-run WITH the flag and provably not without it, a partial line (`input` prompt — already fflushed by the interpreter, so identical both ways) plus output that ends with no trailing newline, a 20 000-line child checked byte-identical against the unflagged run with every line verified, composition with `--json-diagnostics` in either flag order (stderr byte-identical three ways), a child killed by SIGTERM mid-output (flushed bytes survive; unflagged, nothing does), opt-in proof, and that a flag-looking argument after FILE still reaches the program
./tests/run_try_decode.sh  # PLAT-JSON `try_decode(text)`: decode that reports failure as a VALUE ({ok,value,message,offset,line,column}) instead of raising. Shares decode's parser -- no second JSON implementation -- so both accept the same dialect and diagnose an input identically; a PARITY tier asserts the reported text equals the raised text across 9 malformed classes, and a RAISE-INTACT tier asserts decode itself is unchanged. Exists because gBASIC cannot catch a raise, which forced pure-gBASIC pre-validation, which is QUADRATIC (`mid` is O(i) on codepoint-indexed strings: 64KB 16s, 128KB 69s, 256KB 291s). Also caps nesting at 10 000 levels for BOTH entry points -- unbounded parser recursion used to segfault the interpreter around 45 000 levels on an 8MB stack. Headless, no python3, valgrind tier over every malformed path
./tests/run_stderr.sh      # PLAT-STDERR `print to error`: a program's route to standard error. Headless, GI-independent, never skips (bar the valgrind tier); determinism from redirection, process exit and a gate file, never a clock. Covers: bytes reaching fd 2 and not fd 1 from every statement position the grammar admits (top level, function body, inline-if consequent, loop body), full rendering parity with `print` proved by emitting every value shape twice and requiring the two captures byte-identical (352 bytes, 15 value kinds), byte-level edges (empty argument, embedded newline, interior NUL, multi-byte UTF-8), interleaving at a shared destination with and without `--line-buffered` (source order vs stderr-first), stderr arriving mid-run in BOTH configurations while stdout does so only when flagged, `--json-diagnostics` JSON byte-identical whether or not the program also writes to stderr and still parseable, plus shell-level redirect/discard/pipeline cases
./tests/run_pre_registration.sh # PLAT-GUARD tripwire on the set of declarations `eval_program` pre-registers before running a `program` block. Sited with the platform, next to the code, because gBASIC Studio's STU-4B declaration-hoisting rule is defined AS that set and nothing in either file references the other. Asserts it two ways: structurally (reads the BEGIN/END PRE-REGISTRATION SET markers in `src/eval.c` — catches a kind added or removed even with no behavioural test for it) and behaviourally (a program block reaching a function, modifier, library and function value all declared below `end program`, plus the negative that a top-level statement there does NOT run). Fails with a message naming the hoisting rule as what must move with it
./tests/run_nap_fs.sh       # NAP-10 filesystem metadata + atomic_replace; GI-independent; xdev case gated on a distinct /dev/shm, opt-in NAP_FS_STRESS=1 concurrency stress (structural coverage also in examples/nap_fs_test.gb + run_negative)
./tests/run_native_editor.sh # NAP-7 SourceEditor/gtk.bas/gbasic.lang (GBASIC_PATH=stdlib); headless tier always, display smoke when a display exists; skips if GtkSource typelib absent
./tests/run_native_workbench.sh # NAP-8 platform spike (examples/native_workbench); inspect/process always, async gated on libgirepository, full-UI smoke gated on GTK4/GtkSource typelibs + a display
./tests/run_gtkui.sh        # NAP-11 gtkui reconciler (stdlib/gtkui.bas): headless diff-logic tier always; display smoke (mount/update/insert/remove/reorder/replace/nested/native-embed/signal/unmount) under G_DEBUG=fatal-criticals, gated on GTK4 typelib + a display
./tests/run_datagrid.sh     # NAP-12 DataGrid (stdlib/datagrid.bas over native rowmodel adapter): headless native-model tier always (1M-row virtualization proof, no display); logic + lifetime + display smoke (datagrid.cell correctness, COW snapshot, selection, refresh, factory/callback ownership with nothing retained outside the registry, real GtkColumnView) under G_DEBUG=fatal-criticals, gated on GTK4 typelib + a display
./tests/run_persist.sh      # persist (stdlib/persist.bas): crash-safe versioned persistence -- atomic temp-file+rename write, and a read that reports missing/corrupt/loaded as a VALUE via try_decode rather than raising. Four cases inherited from Studio's STU-STORE tier when Studio moved to its own project (~/development/gbasic-studio): the three states unchanged, a corrupt file reporting WHY with the parser's reason and position, every value shape round-tripping byte-identically, and a 115KB/240-record index opening in under 5s -- the pure-gBASIC validator it replaced took 92s on the same input
./tests/run_outline.sh      # PLAT-OUTLINE source_outline(text) builtin (general in-process structural outline over the reentrant gb_parse): headless, GI-independent, path-free goldens. 12 fixtures (empty/one/program/functions/modifiers/nested/consider/multiline/comments/invalid/unmatched/unicode) dump kind/name/half-open BYTE range/line:col/flags + a byte-exact slice of every node + containment self-check + diagnostics; plus large-file (exact 25000-node count + timing ceiling), 50x repeated-call stability, and a valgrind tier (SKIPs if valgrind absent)
./tests/run_stridx.sh       # PLAT-STRIDX string access that is not quadratic in the string's size. Two costs made every per-character loop O(n^2), and both are gone: reading a string VARIABLE deep-copied the buffer (env_get -> value_copy), so even the O(1) `byte_at(s,i)` cost O(n) per call (1 MB loop: 30 s); and `len`/`mid`/`left`/`right` each re-walked the whole string to count codepoints (256 000-char forward scan: 249 s). Fixed in the StringHeader that already sat in front of every string: `refs` (share, don't copy -- sound because string values are immutable, `as.string` being assigned in exactly one place), a memoized `cp_count` + forward cursor, and a lazily-built sparse cp->byte index (one sample per 64 codepoints) for multibyte strings accessed out of forward order, without which BACKWARD scans stay quadratic (256 000 units: 152 s -> 0.31 s). Costs 40 bytes per string value, measured. Tiers: CORRECTNESS (tests/stridx_test.bas -- forward/backward/random/alternating access, ASCII and multibyte, invalid UTF-8, interior NULs, rebinding between accesses, aliasing, empty/single-unit; golden captured BEFORE the change and byte-identical after, so a moved line means semantics moved), SHAPE (7 cases assert the RATIO across a 4x size step, gate 8x where quadratic is ~16x, never an absolute time), CEILING (one very generous absolute bound), VALGRIND. Headless, GI-independent, never skips (bar valgrind)
./tests/run_arridx.sh       # PLAT-ARRIDX arrays: aliasing + complexity class. Arrays became a shared refcounted store with copy-on-write on 2026-07-23 (docs/array_cow_design.md), so `arr[i]` in a while loop and `append` are LINEAR — the O(n^2) warnings that survived in docs until 2026-07-29 were stale. This suite exists because nothing guarded that: examples/array_cow_test.bas pins SEMANTICS, nothing pinned COST. Tiers: ALIASING (tests/arridx_test.bas — the cases array_cow_test does not reach: an element assigned FROM THE SAME ARRAY, where the RHS is read from the store the write is about to free; mutation during `for each`; capture-then-mutate both directions; plus every in-place mutator against a live alias, one per detach call site), SHAPE (7 patterns, ratio across a 4x step, gate 8x), CONTROL (a NEGATIVE control — these tiers cannot be proven red since there is no defect left, so the same harness is pointed at still-quadratic string concatenation and REQUIRED to exceed the gate; reports ~20x. If it stops failing, the shape tiers measure nothing), VALGRIND. Headless, never skips (bar valgrind)
./tests/run_gui_parse.sh    # parse-only headless smoke for examples/gui + examples/gi + examples/native_editor + examples/native_workbench + examples/native_ui (parse, don't run; no display needed)
./tests/run_docs_gate.sh    # executable-docs gate, two halves. (1) every docs/ai/COOKBOOK.md file reference exists and is wired into a suite. (2) PLAT-DEBT 1 -- every PERFORMANCE CLAIM must cite a tests/run_*.sh that asserts it, so a claim cannot outlive its truth: every bullet in UNLEARN.md's "## Performance traps" section (checked regardless of wording), plus any COOKBOOK bullet tripping a cost keyword. A runner is required rather than a .bas golden because a golden asserts VALUES and only a runner times anything. Claims that something IS slow ride a negative control that fails when it gets fixed; claims that something is NOT slow ride a shape tier that fails on regression. Exists because UNLEARN.md called arr[i]/append O(n^2) for six weeks after they became linear, and that lie scoped a whole phase. Limits stated in the script: it checks the CITATION not the semantics, and COOKBOOK detection is by keyword, which provably misses shapes ("remember `append` copies" sat stale two lines from a cited bullet)
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

## gBASIC Studio

Studio lives in its own project: `~/development/gbasic-studio`. It is not built
here and its libraries are not in this `stdlib`. What DID stay is everything that
was general rather than Studio-specific — `persist` (crash-safe versioned
persistence) and `filetree` (a directory as a value tree), both extracted from
Studio and documented in the cookbook. Studio consumes this repo's platform
surface (`source_outline`, `try_decode`, `process.*`, `--line-buffered`,
`print to error`, `atomic_replace`, the `gi`/`gtk`/`sourceeditor` libraries) and
finds it via `GBASIC_PATH`; nothing here depends on Studio.

## EDGAR suite work
Governed by docs/edgar_suite_development_plan.md. One WP per session,
assigned by Matthew. Follow its §1 session protocol exactly: read only
the WP's Read list, stop at the WP boundary, record verbatim evidence in
docs/PROGRESS.md. Never mark anything `verified`. No network in tests.
