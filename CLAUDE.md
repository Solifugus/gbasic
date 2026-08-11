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
./tests/run_regex.sh        # TEXT-0 regex in the core (docs/text_design.md §2-§3): a `regex` VALUE KIND plus OVERLOADS of contains/replace/split, not an re_* family — only `match`/`match_all` take new names, because a literal `find` returns one index and a regex match must return {text,start,length,groups}. `find` is deliberately NOT overloaded. Engine is libc POSIX ERE, always on (no HAVE_* guard). Tiers: GOLDEN (tests/regex_test.bas — every overload run in BOTH modes on input where the literal and pattern answers DIFFER, so an overload that swallowed the literal case moves the golden; plus codepoint offsets fed back through `mid`, unknown-on-miss, the unknown-vs-"" non-participating-group distinction, zero-width match termination, interior-NUL subjects); FLAG MATRIX (the only part not a direct libc mapping — POSIX couples "dot matches newline" and "^/$ per line" into the single REG_NEWLINE bit while gBASIC separates them, so 2 of the 4 combinations need `.` rewritten, and REG_NEWLINE additionally stops `[^x]` matching a newline where PCRE's /m does not; 12 assertions that pass individually under a wrong choice but cannot all pass together); NEGATIVE (8 pinned messages incl. \D inside a bracket and a NUL in a pattern — rejected, while a NUL in a SUBJECT works); ACTOR ROUND-TRIP (ships as pattern+flags and recompiles — a regex_t is meaningless in another address space); VALGRIND. Headless, GI-independent, never skips (bar valgrind)
./tests/run_xlsx.sh         # xlsx Stages 1-2 (docs/xlsx_design.md): ZIP container over zlib (hand-written; libzip/minizip absent on dev + RISC-V), the PART TREE, sparse cells, and write + round-trip. The design's load-bearing claim is that the reader DISCARDS NOTHING -- `libxlsxwriter` was rejected because it generates new workbooks and cannot edit an existing one, so round-trip is only possible if every part survives the read. Tiers: GOLDEN READ (shared strings incl. an XML entity and non-ASCII, a sparse sheet with no row 4 and no column C, a formula cell keeping BOTH <f> and Excel's cached <v> -- that cached value is the recalc engine's future test oracle -- a boolean, an Excel error as its own kind, style indices preserved since only the number format distinguishes a date from 45000, and a second sheet resolved via r:id because part filenames follow creation order not tab order); GOLDEN WRITE (editing ONE sheet changes that part and NOTHING else, incl. the unmodelled vendor part byte-identical; save is byte-DETERMINISTIC across runs because ZIP mod-time fields are fixed rather than clock-derived -- a clock there would look like a successful write while making byte comparison useless; and the output validates under `unzip -t`, i.e. by something other than our own reader); RETENTION (every entry in the ZIP's own central directory must appear in xlsx.parts -- compared against `unzip -Z1`, never a hardcoded list, so growing the fixture cannot leave the check narrower than the file); NEGATIVE (not-a-ZIP, truncated, missing file, overwriting a formula cell REFUSED since the cached value and formula would disagree and Excel would silently revert the edit on recalc, creating a new cell refused rather than placed wrongly); FORMULA (Phase A+B of §13.G -- the expression evaluator plus the durable-core functions, chosen because four of the nine most-used "functions" in the measured Enron corpus are arithmetic OPERATORS, so precedence/refs/ranges carry the largest share of real usage before any function library exists. `xlsx.check` is THE ORACLE: an xlsx stores both the formula and Excel's own cached result for every formula cell, so each formula is checkable IN ISOLATION with no dependency graph -- the graph is only needed once something changes. Asserts zero disagreements, not just a stable golden, since a golden would happily record a regression as the new expected output. VOLATILE functions (NOW/TODAY/RAND) are skipped rather than judged because their cached value dates from whenever the workbook last calculated; UNSUPPORTED functions are reported BY NAME rather than defaulted to a plausible zero, asserted separately. CAVEAT recorded in the fixture generator: this fixture's cached values are hand-written, so check measures self-consistency here and becomes a true oracle only against a workbook Excel actually wrote); RECALC (dependency-ordered recalculation. The ordering tier is the one that matters: on the fixture's Ledger sheet D7 = B5*2 sits ABOVE B5 = SUM(B2:B3) in sheet order, so an engine evaluating top-to-bottom hands D7 a stale B5 and prints a plausible wrong number -- changing B2 to 1000, the only correct transitive answer is 1801.5 and a stale read gives 2302.5, so the test names both. Also: recalculated values PERSIST through save while the formulas survive, only the edited sheet's part changes, and a circular reference is REPORTED rather than iterated toward a fixed point -- with healthy cells on the same sheet still evaluating, since one cycle must not sink a sheet); MACRO SHEET (a sheet declared with an empty `r:id` -- a VBA module/macro sheet, genuinely in the workbook with no worksheet part behind it. `xlsx.sheets` listed it and `xlsx.cells` then rejected it as "no such sheet", which is self-contradictory AND false, and it broke the one loop every caller writes. Found by scanning the whole 15,871-workbook Enron corpus one process per file: 400 workbooks (2.5%) carry one and all 400 scan failures were this single cause -- zero ZIP, XML or cell-parsing failures in the entire corpus. Reads now answer as the empty sheet it is; a genuinely absent name still raises, and a WRITE is still refused but with the real reason rather than the false one -- both pinned as negatives, since blanket leniency would let a typo'd sheet name silently return nothing. Separate fixture, so the byte-exact read/write/retention goldens are not rebaselined for an unrelated concern; it includes the trailing space in `"VBACode "` that seven corpus files carry, which a name-trimming reader would pass on `Module1` and still fail. Corpus measurements, incl. the build order by fully-recalculable workbooks, in docs/xlsx_design.md §13.I); SHARED FORMULAS (a formula filled down a column is stored ONCE -- the anchor carries the text, every other cell in the run carries an empty `<f t="shared" si="n"/>`. Read naively those cells have "a formula whose text is empty" -> `#VALUE!`, and since recalc writes values back it CORRUPTED them, measured at 171 cells on the first corpus file tried. 61.0% of formula-bearing workbooks use them and 13.2M of the corpus's 20.7M formula cells are continuations, so two thirds of every formula cell was being read as empty -- which also means the §13.I function ranking is measured on a biased sample, since the scanner never saw them. Resolution TRANSLATES the anchor by the cell offset; the fixture is one column per rule -- relative shifts, absolute `$` does not, a ref inside a string literal is text, both range endpoints move -- because a wrong offset yields a plausible number from the wrong cells rather than an error); NOW/TODAY (the Excel date serial, epoch 1899-12-30 -- Lotus treated 1900 as a leap year and Excel kept the bug. `GBASIC_XLSX_NOW` pins the clock for this tier only; expected serials cross-checked against LibreOffice, and the day and seconds-of-day are asserted SEPARATELY because `print`'s ~6 significant digits render a full serial as 46237.6, see DOGFOOD 2026-08-09. Also asserts local-time handling across two zones and that the UNPINNED clock agrees with date(1), so the tier cannot pass with only the test seam working); POST-2001 (examples/xlsx_modern_test.bas over examples/fixtures/xlsx/modern.xlsx, whose cached values LIBREOFFICE computed via tools/make_xlsx_modern_fixture.sh -- so `xlsx.check` there compares against an INDEPENDENT implementation instead of restating our own output, which is the weakness every hand-written fixture has. Evidence ranks Excel-authored corpus > LibreOffice-authored > hand-written; the arithmetic is also small enough to check by eye. The structural finding is bigger than any function: anything newer than the original ECMA-376 list is stored PREFIXED -- `_xlfn.XLOOKUP`, `_xlfn._xlws.SORT`, `_xlpm.x` for a LET parameter -- so until those are stripped every modern function is an unknown name however well implemented, while `_xll.` (add-ins) and `_xludf.` (VBA) must NOT be stripped since they are unevaluable in principle. basic.xlsx's unsupported-function case is now `_xll.HPVAL` because it used to be XLOOKUP and that test broke the moment XLOOKUP was implemented -- a fixture pinned to a gap that closed. 62 formulas, zero disagreements. Refused rather than approximated: YEARFRAC basis 1, XLOOKUP non-exact modes, AGGREGATE 14-19. Deferred: dynamic arrays, which SPILL and so are structural, and LET/LAMBDA, which need a binding environment); CROSS-SHEET (examples/xlsx_crosssheet_test.bas over crosssheet.xlsx, values again computed by LibreOffice via tools/make_xlsx_crosssheet_fixture.sh -- the sheet XML is hand-written so the shapes are guaranteed present but carries NO cached values, so LibreOffice must compute them to convert the file. Closes §13.J's largest cause: 3.09M cells, 54% of all disagreements. 42% of the corpus population is the QUOTED form ('Nymex hist.'!A:B, quoting being forced by a space in the name), 30% plain, 28% EXTERNAL ([4]Book!A1) which names a different workbook and is REPORTED UNAVAILABLE rather than read from Excel's stale cached copy -- which is why `unsupported` rises as disagreements collapse. Brought VLOOKUP/HLOOKUP/INDEX/MATCH and the IS* predicates with it, since VLOOKUP is the dominant consumer; note VLOOKUP's 4th arg and MATCH's 3rd DEFAULT TO APPROXIMATE, and guessing either way is silent. Whole-column ranges (A:B) clamp to the sheet's real extent. Three defects found building it, all recorded in §13.L: a column off-by-one that COUNTA(A:A) CONCEALED by reading the neighbouring column which happened to hold the same count; a realloc-dangling XlsxSnap* that segfaulted on a real corpus workbook; and per-lookup range materialisation that cost 116s on one file, fixed by giving the four position-addressing functions an unmaterialised descriptor -> 12s. Corpus agreement 66.1% -> 94.9%); SHAPE (a 40k-row sheet must evaluate under a 1200ms CEILING, not a ratio -- measured: with the pre-fix linear cell lookup the 4x-size step reports 7.7-8.0x, which an 8x ratio gate would have PASSED, while the ceiling separates 312ms indexed from ~2500ms scanned. Fixture generated in-test with awk+zip, so no python3 and nothing large committed); VALGRIND. Skips cleanly when built without zlib or libxml2. No python3 in the test path -- the fixtures are committed and tools/make_xlsx_fixture.py + tools/make_xlsx_macro_fixture.py are only how they were authored
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
