# libgbasic extraction — implementation plan (Phases 0–2)

Goal: extract a **reentrant front end** (`lex → parse → AST`) into a `libgbasic`
build target with **structured diagnostics**, keeping the CLI (`src/main.c`) as
its first and only consumer. The interpreter (`src/eval.c`) stays global-state
for now; only its diagnostic *reporting* is routed through the new sink.

Survey that motivated this plan: see the conversation report. Key facts it rests on:
- The lexer (`src/lexer.c`) and `src/ast.c` already have **zero file-scope globals**.
- The parser's own mutable state is just **four variables** in `src/parser.y`
  (`active_lexer`, `lexer_error_reported`, `active_parse_path`, `parsed_program`)
  plus the standard non-reentrant Bison globals.
- Parsing is already **memory-buffer native**: `parse_source(const char *source, ...)`.
- All error/warning reporting today is **inline `fprintf(stderr, …)`**; the only
  seeds of structure are the lexer's deferred `TOKEN_ERROR` + `error_message`
  and eval's internal `RuntimeError current_error`.

## Global rules (apply to every phase)

- [ ] Golden tests are **byte-exact**. Any stderr/stdout diff is a **regression**,
      never a rebaseline, unless Matthew approves that specific diff explicitly.
- [ ] **One phase per session.** Stop at each phase boundary for review.
- [ ] Optional-dependency code stays behind its `#if HAVE_*` guard.
- [ ] Full suite must be green at each phase boundary (core suites always;
      module suites skip cleanly when their dep is absent).

---

## Phase 0 — Carve the build target (no behavior change)

Prove the object boundary before touching any source. `libgbasic` = every object
except `main.o`; relink `gbasic` against it.

- [x] Add `LIB_OBJS` (lexer, parser.tab, ast, eval, builtins, actor) and a
      `libgbasic.a` archive target to the Makefile.
- [x] Relink `gbasic` from `main.o` + `libgbasic.a` (+ existing `LDLIBS`).
- [x] `libgbasic.a` added to the `clean` target.
- [x] `.gitignore` covers `libgbasic.a` / `src/*.o` as appropriate.
- [x] `make clean && make` builds `gbasic` and `libgbasic.a` with no source edits.
- [x] Core suite green: `./tests/run_examples.sh`, `./tests/run_negative.sh`,
      `bash tests/run_bag_smoke.sh`.
- [x] Module suites skip-or-pass (not fail): sqlite / webclient / webserver / site.
- [x] Show diff, stop for review.

**Boundary:** no `.c`/`.h` source changed. Only Makefile / .gitignore. ✋ STOP.

---

## Phase 1 — Structured diagnostics (route reporting through a sink)

Introduce an append-only, structured diagnostic sink and route the two centralized
reporters into it. CLI drains the sink and prints in **today's exact format** so
golden tests do not move.

### Tests first (must be written FAILING before any Phase 1 impl code)

- [x] (a) **Diagnostics test**: parse a buffer containing 3 known errors; assert
      exactly 3 structured diagnostics returned, each with exact `line`/`column`
      and message. Drive via a tiny C harness linked against `libgbasic.a`
      (`tests/frontend/`), not the CLI, so it reads structure not stderr text.
- [x] (b) **Two-contexts-in-one-process smoke test**: create/parse in two
      independent contexts in one process. Marked **expected-fail** (xfail) until
      Phase 2 makes the parser reentrant; the runner must report xfail as a
      non-failure and would flag an unexpected *pass*.
- [x] Add both to a new `tests/run_frontend.sh` (skips cleanly if no cc), wired
      into the session's test invocation. Confirm (a) fails and (b) xfails now.

### Implementation

- [x] Define `gb_diag` (severity, code, path, line, column, message) and
      `gb_diagnostics` (append-only list) in a new `include/diagnostics.h`.
- [x] Add a diagnostics sink; provide `gb_diag_count` / `gb_diag_at` accessors.
- [x] Route `report_parse_issue` (`parser.y`) into the sink instead of `stderr`.
- [x] Route `runtime_error_raise` STOP-mode reporting (`eval.c`) into the sink;
      feed from the existing `RuntimeError current_error`.
- [x] Feed the lexer's deferred `TOKEN_ERROR` + `error_message` into the sink.
- [x] CLI (`main.c`) drains the sink and prints in the **exact** prior stderr
      format (`"<kind> at <path>:<line>:<col>: <msg>"`, `"warning: …"`, etc.).
- [x] Sweep ad-hoc `eval.c` `fprintf(stderr, …)` sites into the sink where they
      map cleanly; leave the rest for Phase 3 (note them).
      NOTE: only the centralized `runtime_error_raise` STOP path was routed. The
      ~20 truly ad-hoc `eval.c` `fprintf(stderr, …)` sites were ALL deferred to
      Phase 3 — none mapped cleanly enough to route without byte-exact risk this
      phase. They still print immediately, unchanged.
- [x] Full core + module suite byte-exact green (no stderr drift).
- [x] Diagnostics test (a) now **passes**; two-contexts test (b) still xfails.
- [x] Show diff, stop for review.

**Boundary:** diagnostics structured and CLI-format-preserving; parser still
non-reentrant. ✋ STOP.

---

## Phase 2 — Reentrant front end

Make the Bison parser pure and move the four `parser.y` globals into a per-parse
context. Lexer and AST need no change (already global-free).

### Implementation

- [x] Introduce `gb_parse_ctx` holding `active_lexer`, `lexer_error_reported`,
      `active_parse_path`, `parsed_program`, and the diagnostics sink.
      Placed in a new guarded `include/parse_ctx.h`, included from both the
      grammar's `%code requires` (so the header's `yyparse(gb_parse_ctx*)`
      prototype sees it) and the prologue (so parser helpers see it). Also holds
      `la_*` (last-lexed token location) so action-level error reports reproduce
      the exact location the former global `yyerror` read from global `yylloc`.
- [x] Convert grammar to pure parser: `%define api.pure full` + `%param
      {gb_parse_ctx *ctx}`; thread `ctx` through `yylex`/`yyerror`. Generated
      parser is now `YYPURE 2`; `yylval`/`yylloc` are locals inside `yyparse`.
- [x] Rewrite the four globals as `ctx->` fields; `source_declares_function` and
      `modifier_lparen_ahead` read `ctx->active_lexer->source`. Diagnostic
      routing goes to `ctx->diags` via a new `gb_report_to(sink, ...)` primitive
      (`gb_report` == `gb_report_to(<global sink>, ...)`), so parser diagnostics
      never touch the process-global sink.
- [x] Confirm `gbasic_builtin_function` (name lookup) stays global-safe.
      Verified: a `static const` table lookup with no mutable state.
- [x] `parse_source` becomes a thin wrapper that stack-allocates a `gb_parse_ctx`.
      The reentrant core is `parse_source_reentrant(source, path, diags, out)`
      (used by `gb_parse`); `parse_source` + `parse_set_source_path` remain as
      global-backed shims for the single-threaded CLI/eval import paths.
- [x] Full core + module suite byte-exact green. Zero rebaselines: examples,
      negative (incl. the 4 action-level-`yyerror` location tests), core, bag,
      sqlite, webclient, webserver, gbasic_site, crypto, xml-bigfile — all 0 FAIL.
- [x] Diagnostics test (a) still passes.
- [x] **Two-contexts test (b) flips to PASS**; un-marked its xfail (500 iters ×
      2 threads, 0 bad parses).
- [x] NOTE: the survey held exactly — `src/lexer.c` and `src/ast.c` needed
      **zero** changes (not even signature threading).
- [x] Show diff, stop for review.

**Boundary:** entire front end reentrant + structured diagnostics; CLI unchanged
in observable behavior. ✋ STOP. — **DONE.**

---

## Deferred — NOT started this track (do not begin without a new go-ahead)

### Parser error recovery (multiple diagnostics per buffer)
The current parser aborts at the first error, so one parse yields at most one
diagnostic. Real multi-error reporting (recover and keep going) is required for a
good LSP experience — a language server wants every error in a buffer, not just
the first. Becomes its own phase later. Until then, the Phase 1 diagnostics test
collects three errors by parsing three one-error buffers into a single sink.

### Phase 3 — Interpreter context struct
Collect eval.c's ~60 file-scope mutables into a `gb_interp` context, in dependency
clusters (env/functions/modifiers; error state; import/use state; module resource
tables + RNG). Convert `atexit`/signal lock cleanup and `_exit`/`abort` paths to
context teardown so an embedder isn't killed by a script. Largest effort; most
regression risk; sequenced last.

### Phase 4 — Public header + CLI as pure consumer
Freeze `include/gbasic.h` (context lifecycle, parse, run, diagnostics, AST access).
Rewrite `main.c` against it. Keep `parse_source`/`eval_program` as thin
compatibility shims during transition, then delete once nothing internal uses the
globals.
