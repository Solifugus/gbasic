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

## Phase L — LSP server + JSON diagnostics + token inventory

First external consumer of `libgbasic.a`: a Language Server. Proves the reentrant
front end + structured diagnostics carry a real tool. Plus two CLI-side extras.
Builds on Phases 0–2; does NOT depend on the deferred Phase 3/4.

### Gate 1 — design proposal (NO code) ✋ propose, then STOP for review
- [x] Propose `gbasic-lsp` file/module structure, JSON strategy, and test plan.
- [x] Propose the JSON dependency choice (vendored single-file vs hand-rolled),
      with rationale, BEFORE implementing. **Approved: vendor cJSON (MIT, single
      .c/.h) for the server; hand-rolled JSON-line emitter for the CLI flag so the
      main `gbasic` binary gains no dependency.**
- [x] Test plan includes: (a) a scripted harness that pipes framed LSP messages
      into `gbasic-lsp` and asserts on published diagnostics (golden-style),
      runnable from the suite; (b) an explicit multi-byte + astral UTF-8 transcode
      unit test.
- [x] STOP for review before any code. (Approved: cJSON + separate `make
      gbasic-lsp` target + layout/test plan as proposed.)

### Deliverable 1 — `gbasic-lsp` (new binary, C, links `libgbasic.a`)
- [x] JSON-RPC 2.0 over stdio with `Content-Length` framing (binary-safe reads).
      `src/lsp/rpc.c`.
- [x] Lifecycle: `initialize` / `initialized` / `shutdown` / `exit`.
- [x] Sync: `textDocument/didOpen`, `didChange`, `didClose` — Full sync only
      (`TextDocumentSyncKind.Full`).
- [x] Push `textDocument/publishDiagnostics` after every open/change; clear
      (empty array) on close.
- [x] Per-document sink: `gb_parse` the buffer, map `gb_diag` spans → LSP ranges.
      Reality noted, not hidden: the parser aborts at the first error today, so
      v1 publishes 0 or 1 diagnostic per document until the deferred error-recovery
      phase lands.
- [x] Position encoding: advertise `utf-8` via LSP 3.17 `positionEncoding` when
      the client offers it (our byte columns == UTF-8 code units); otherwise
      transcode byte columns → UTF-16 code units using the source line's bytes.
      Convert 1-based (line,col) → 0-based (line,character) in both cases.
      `src/lsp/lsp_position.c` (standalone, dep-free).
- [x] Unit-test the transcode with multi-byte AND astral (surrogate-pair) UTF-8.
      `tests/lsp/test_position.c` (10 cases, green). Harness golden also bakes in
      an end-to-end UTF-16 transcode (é → character 8, not byte 9).
- [x] Single-threaded v1 (the parser is reentrant; threading is deferred).
- [x] cJSON v1.7.18 vendored (MIT) under `third_party/cjson/` with LICENSE +
      provenance README; built with relaxed warnings; only the LSP binary links it.
- [x] Framed golden harness `tests/lsp/run_lsp.sh` drives a full session
      (initialize → sync → shutdown/exit) and byte-compares stdout.

### Deliverable 2 — CLI flag `--json-diagnostics`
- [x] `gbasic --json-diagnostics` emits collected diagnostics as JSON lines to
      stderr instead of the legacy format. Hand-rolled emitter
      (`gb_diag_write_json` in `src/diagnostics.c`, no cJSON in the main binary).
      Covers parse, lexer, and runtime diagnostics.
- [x] Default behavior (no flag) stays byte-exact legacy format. Test:
      `tests/run_json_diagnostics.sh` (asserts both the JSON output and that the
      default path is unchanged).

### Deliverable 3 — `docs/TOKENS.md`
- [x] Inventory of lexer token kinds, keywords, and literal forms; the source of
      truth for external syntax highlighters. Derived from `include/lexer.h`
      (`TokenType`) + `src/lexer.c` (`identifier_type`/`number_token`/
      `string_token`/operator switch), cross-checked with `gbasic --tokens`.
      Documents the reserved-but-unused keywords (`dim`/`as`/`step`), the
      column-sensitive `consider` variants, and the context-sensitive spans.

### Rules
- [x] No new system dependencies. cJSON vendored single-file, MIT, license header
      intact.
- [x] Golden suite stayed byte-exact throughout: examples, negative, frontend,
      bag, sqlite, xml-bigfile — all 0 FAIL/mismatch. New Phase L runners green.
- [x] Stopped at the phase boundary (full phase completed, not the handshake
      fallback).

**Boundary:** a working diagnostics-only LSP + `--json-diagnostics` + TOKENS.md,
all golden-green. ✋ STOP. — **DONE.**

---

## Phase GI — GObject-Introspection bridge (independent track)

> NOTE: This is a **separate track** from the libgbasic extraction above. It adds a
> new optional module; it does not touch the extraction phases. The existing GTK3
> `gui` module is **not** modified, replaced, or refactored — it remains its own
> module. Approved design lives in the conversation report (survey + design).

Goal: a generic GObject FFI exposed as a `gi.*` builtin library, driven by the
typelib at runtime via **libgirepository-2.0** (modern `gi_repository_*` API,
GLib ≥ 2.80). GTK4 is the first *target* toolkit but nothing GTK is linked — the
bridge links only `girepository-2.0`/`gobject-2.0`/`glib-2.0` and `dlopen`s the
toolkit through its typelib. v1 is the **raw bridge only**: `gi.get`/`set`/`call`/
`connect`. Idiomatic `.property`/`.method()` sugar is deferred (see Deferred).

Design invariants (apply to every step):
- Everything behind `#if HAVE_GIR`; **zero-impact when compiled out**; the golden
  suite stays **byte-exact** throughout.
- Reuse the existing refcounted-native-handle pattern (`SqliteConnectionValue`
  shape) and the `eval_call` `strcmp` module-dispatch path. **No** lexer / grammar
  / AST / lvalue changes in v1.
- Single process, single thread; GObjects are not actor-serializable.

### Gate 0 — build wiring + early confirmation ✅ DONE
- [x] Makefile: `girepository-2.0` pkg-config detection → `HAVE_GIR` (0/1),
      mirroring the other optional deps. No legacy 1.x fallback.
- [x] `#if HAVE_GIR #include <girepository/girepository.h>` in `eval.c`.
- [x] Confirm a rebuild flips `HAVE_GIR=1`, links `girepository-2.0`, and the full
      golden suite is byte-exact with the module **compiled in but unused**
      (examples, negative, sqlite, bag, lsp, json-diagnostics — all exit 0).
- [x] Install requirement recorded: `libgirepository-2.0-dev` (2.88.0-1); Gio /
      GObject / GLib typelibs already present via `gir1.2-glib-2.0`.

### Tests first (write FAILING before implementation; headless / no display)
- [x] `tests/gi/` fixtures exercising **non-GUI Gio/GObject types** so CI never
      needs a display:
  - [x] **Signal dispatch** — `Gio.Cancellable`: `gi.connect(c,"cancelled",fn)`,
        emit via `gi.call(c,"cancel")` (fires synchronously, no main loop needed),
        assert the gBASIC handler ran AND the emitter arg maps back to the same
        wrapper (qdata identity). NOTE: switched from the planned
        `Gio.SimpleAction`/`activate` because SimpleAction's construct-only `name`
        is awkward under a bare `gi.new`; Cancellable constructs clean and its
        method is a direct (non-interface) emit.
  - [x] **Property roundtrip** — `gi.set`/`gi.get` `application-id` on
        `Gio.Application`, plus `gi.type_name` / `gi.is_a`.
  - [x] **Return-value transfer** — NOTE: implemented with `Gio.Cancellable`
        (`c2 = c` shares one wrapper/one object ref; mutate via one handle, observe
        via the other; neither frees prematurely) rather than the planned
        `Gio.ListStore`, whose construct-only `item-type` cannot be set through a
        bare `gi.new`. Same coverage intent: value_copy refcount + no premature
        free. (`gi_enum_test` additionally covers enum/flags resolution.)
  - [x] **Handler-raises regression** — a signal handler raises; the outer program
        continues with **clean error/line/stop state** (exit 0, stdout intact) while
        the handler error is surfaced on stderr via the sink — verifies the
        marshaller's snapshot/restore.
  - [x] **Negative suite** (`.err`): not-loaded, unknown namespace, unknown type,
        unknown property, unknown method, arity → clean structured runtime errors
        (domain `"gi"`).
- [x] `tests/run_gi.sh` — **skips cleanly when `HAVE_GIR=0`** (mirrors
      `run_sqlite.sh`).
- [~] GTK **widget** tests are **manual** (need a backend); all CI-critical coverage
      is on Gio/GObject types. Golden suite stays display-free. The **coexistence
      guard** is implemented but not golden-tested here: this box has neither GTK3
      (HAVE_GTK=0) nor the Gtk-4.0 typelib, so it cannot be exercised in CI (manual).

### Implementation
- [x] `VALUE_GOBJECT` kind + `GObjectValue { GObject *obj; size_t ref_count;
      int closed; }`; `value_copy` (refcount++) / `value_free` (refcount-- →
      `g_object_unref`). Enum constant exists unconditionally; the `obj` field and
      all construction/bodies are under `HAVE_GIR`. Added `VALUE_GOBJECT` cases to
      the value-kind switches (type name, truthy, print, string, identity, encode,
      serialize→"gobjects cannot be serialized") and `=`/`!=` identity comparison.
- [x] **qdata canonicalization**: one wrapper per `GObject*` via
      `g_object_get_qdata`/`set_qdata` (quark `gbasic-gobject-wrapper`); the qdata
      link is severed before the final `g_object_unref`. Identity verified by the
      signal test (`source = c` → "same object").
- [x] Ownership: `gi.new` → adopt the fresh ref and `g_object_ref_sink` (sinks
      floating refs once); getter/method object returns honor the typelib
      **transfer** annotation (transfer-none → `g_object_ref`; transfer-full → adopt).
- [x] GValue ⇄ Value conversion (v1 types): bool, char/int/uint/…/int64/double →
      number, UTF-8 string, enum/flags → number, object → `VALUE_GOBJECT`,
      void/NULL → null. Unsupported types raise a clear per-call error — never
      silently mis-convert.
- [x] `gi.*` builtins via `eval_call` dispatch: `require(ns[,ver])`, `new(type)`,
      `get(obj,prop)`, `set(obj,prop,v)`, `call(obj,method,args…)` (via
      `gi_function_info_invoke`, walks parents+interfaces with
      `find_method_using_interfaces`, in-params only; out/inout → clean error),
      `connect(obj,sig,fn)` / `disconnect(obj,id)`, `enum("Ns.Enum.MEMBER")`,
      `is_a`, `type_name`, `main()` / `quit()`.
- [x] Signals via a generic `GClosure` marshaller (`g_signal_connect_closure`):
      GValue args → Values, **snapshot** `runtime_stopped`/`error_mode`/`current_line`/
      `column`/`error_generation`, `invoke_function`, **restore** on a raise;
      an unhandled handler error stays in the diagnostics sink (surfaced) and quits
      the main loop.
- [x] Main loop is a toolkit-agnostic `GMainLoop` owned by the gBASIC program
      (`gi.main()` blocks, `gi.quit()` ends). Not `gtk_main`.
- [x] **Coexistence guard**: `gi.require` of `Gtk` 4 while the GTK3 `gui` module is
      loaded raises a structured error (domain `"gi"`); the reverse (loading `gui`
      after Gtk 4) is guarded by a `gi_gtk4_active` flag checked in the `gui` load
      path. No build switch in v1. (Not CI-tested here — see tests note.)
- [x] Absent-`HAVE_GIR` stubs raise (at both `load gi` and any `gi.*` call):
      `"gobject-introspection support is unavailable; install libgirepository-2.0-dev
      (GLib >= 2.80) and rebuild"` (domain `"gi"`), mirroring the GTK message.
      Verified by a forced `HAVE_GIR=0` build.

### Follow-up — construct-time properties + namespace functions ✅ DONE
The two deferred items that blocked real GTK4 applications (below), landed
tests-first as a single follow-up. Golden suite byte-exact throughout; the one
authorized rebaseline is `negative_gi_new_arity.err` (message change, `gi.new` is
now variadic). valgrind clean (0 leaks / 0 errors) on the new + error paths.
- [x] **`gi.new` construct-time properties** — `gi.new(type [, name, value]...)`
      over `g_object_new_with_properties`. Each value is converted by the
      property's `GParamSpec` type (looked up on a `g_type_class_ref`'d class
      BEFORE construction) via the existing `gi_value_to_gvalue`; unknown property
      / unconvertible value / non-string name / unpaired arg raises **before any
      object is created**. Ref-sink + qdata-canonicalize exactly as the bare form.
      On an early raise mid-pair, already-converted GValues are `g_value_unset` and
      the class ref released — no leak (valgrind-verified, incl. `nready > 0`).
      Closes the Phase-GI detour: `gi_construct_props_test` builds
      `Gio.SimpleAction` with its construct-only `name`; `gi_construct_object_prop_test`
      passes a GObject value (`Gio.BufferedInputStream` `base-stream`) and reads the
      same canonical wrapper back (the `application`-property conversion, headless).
- [x] **`gi.invoke("Ns.function", args…)`** — namespace-level free functions
      (no receiver), resolved via `gi_repository_find_by_name` → `GIFunctionInfo`;
      methods are rejected (`use gi.call`). Arg/return marshalling reuses a factored
      `gi_invoke_callable` shared with `gi.call` (`gi.call`'s observable behavior is
      unchanged — same error strings, guarded by the existing tests). Covered by
      `gi_invoke_test` (`GLib.markup_escape_text`: borrowed UTF8 in, int64 in,
      transfer-full UTF8 return).
- [x] **Null passthrough for pointer/array params** (shared `gi_giarg_from_value`,
      benefits both `gi.call` and `gi.invoke`): tags ARRAY/GLIST/GSLIST/GHASH/ERROR
      accept `nothing` → NULL; any non-null container still raises (marshalling
      deferred). Enables the canonical `gi.call(app, "run", 0, nothing)`.
- [x] **`run_gi.sh` hardened**: exports `G_DEBUG=fatal-criticals` so any GLib
      critical (e.g. a `G_IS_OBJECT` lifetime assertion) aborts and fails the suite.
- [x] **Construct-time floating-ownership fix** (found via the display acceptance
      run — the first GTK4 window vanished immediately). A construct property can
      make an EXTERNAL owner adopt (sink) a widget's floating ref DURING
      construction: `gi.new("Gtk.ApplicationWindow", "application", app)` adds the
      window to the GtkApplication, which sinks it, so the object returns
      NON-floating with its single ref owned by the app (hence
      `gtk_application_window_new` is `transfer none`). `gi.new` was adopting that
      ref as the wrapper's own, so the handler-local `win` wrapper's scope-exit
      unref destroyed the app's window. Fix: in `gi_do_new`, when a freshly built
      `G_TYPE_INITIALLY_UNOWNED` object comes back non-floating, take our OWN
      `g_object_ref` instead of co-owning the external owner's ref. Diagnosed
      empirically (C probe: window refcount went 1→destroyed; fix → 1→2→survives,
      matching PyGObject's grefcount 2). Verified on a live display: the window now
      survives `activate` and `run()` blocks. Headless regression
      `gi_handler_survives_scope_test` models the accounting with
      `Gio.Application`+`activate`+`SimpleAction` (external owner strong-refs a
      handler-created object; it survives handler scope). valgrind clean.
- [x] **Example promoted to the canonical idiom**: `examples/gi/gtk4_hello.bas` now
      uses `Gtk.Application` (via `gi.new(..., "application-id", ...)`), `connect
      "activate"`, a real `Gtk.ApplicationWindow` built with construct-time
      `"application"`, and `gi.call(app, "run", 0, nothing)` — replacing all three
      v1 workarounds. **Verified on a live display**: launches, the window survives
      `activate`, the loop runs, no GLib criticals under `G_DEBUG=fatal-criticals`.

### Rules
- [x] Golden suite byte-exact throughout; module zero-impact when compiled out
      (verified under both `HAVE_GIR=1` and a forced `HAVE_GIR=0` build).
- [x] `make dev` (all binaries) green; `run_gi.sh` green (skips when absent).
- [ ] Show diff, stop at the phase boundary. ✋ STOP.

### Manual verification — coexistence guard (out of CI)
Not golden-tested because this dev box has neither GTK3 (`HAVE_GTK=0`) nor the
Gtk-4.0 typelib. To exercise it by hand (human or agent) once both toolkits are
present:

1. **Install both toolkits** (Ubuntu): `sudo apt-get install libgtk-3-dev
   gir1.2-gtk-4.0 libgtk-4-1`. The GTK3 *dev* package makes `HAVE_GTK=1` so the
   `gui` module compiles in; the Gtk-4.0 *typelib* lets `gi.require("Gtk","4.0")`
   resolve. Then `make clean && make`. Confirm with `make -n | grep -o 'HAVE_GTK=1'`.

2. **Direction A — gi loads GTK4 after the gui module (GTK3) is active:**
   ```
   load gui
   load gi
   gi.require("Gtk", "4.0")
   ```
   Run with `GBASIC_PATH=stdlib ./gbasic <file>` (gui resolves from stdlib).
   Expected on stderr, nonzero exit:
   `runtime error at <file>:3:1: GTK 3 (gui module) and GTK 4 (gi) cannot be used in the same process`

3. **Direction B — the gui module (GTK3) loads after gi has taken GTK4:**
   ```
   load gi
   gi.require("Gtk", "4.0")
   load gui
   ```
   Expected: the same message, raised at the `load gui` line (`:3:1`), nonzero exit.

Both directions share the guard string (domain `"gi"`). If either prints the
message and exits nonzero, the guard is intact.

---

## Pre-Studio audit + documentation system (multi-phase effort)

Governance: **one phase per session, stop at each boundary for review.** Golden
suite byte-exact throughout; any behavioral fix that would change a golden STOPS
for approval before it is made. Single source of truth: nothing duplicated
between `docs/` (human) and `docs/ai/` (agent layer).

### Phase D0 — Audit (read-only, no changes) ✅ ACCEPTED
Report only; make no code/test changes. Deliverables:
1. Classify every runnable source (`examples/`, `tests/` fixtures, `gi` examples)
   as **current / stale / broken / untested** (runs in no wired suite) against the
   current `gbasic`.
2. Coverage gaps: language features and builtin libraries with no example and/or
   no wired test — listed by module.
3. Surprise harvest: every behavior that contradicts QBasic/VB/common-BASIC
   intuition, every quirk noted in PLAN.md or code comments, every undocumented
   behavior determined by experiment — each with a minimal snippet showing actual
   behavior. Feeds the D3 unlearning file.
Output: written report + proposed fix list. If the fix list is large, propose
splitting the fixes into their own phase rather than folding them into D1.

D0 fix list accepted and split into two fix phases before D1:

### Phase D0.5 — Fixture hygiene (Group A, no behavior changes) ✅ DONE (pending review)
Purely additive/mechanical; no interpreter behavior changed; goldens stay
byte-exact.
1. Rebaselined the 24 orphaned negative fixtures that carried the retired 3-line
   `Error code: N` format to the current single-line `runtime error at …` format,
   and wired all 24 into `tests/run_negative.sh` (247 PASS / 0 FAIL). Old codes
   (all `1003`) harvested first to `docs/ai/_scratch/D3_error_codes_harvest.md`
   (D3 ERRORS.md seed). NB: the 15 crypto/sqlite fixtures that looked orphaned are
   already wired in the gated `run_crypto.sh`/`run_sqlite.sh` — left as-is.
2. Deleted dead orphans: `examples/lexer_test.gb` (stale v0.1 grammar),
   `examples/error_fatal_test.gb` (1-line scratch probe), `examples/libs/math.bas`
   (redundant with the inline `library math` in `load_test.bas`; loaded by nothing).
3. Moved deliberate playgrounds/probes to `examples/scratch/` (excluded from all
   suites, README added): `webclient_playground.bas` + the three `exploratory_*`
   probes. No runner globs `tests/`/`examples/`, so scratch never auto-runs.

### Phase D0.6 — Reviewed behavior changes (Group B) — ✅ DONE (pending review)
Behavior changes with deliberately reviewed golden updates; one commit per item.
- **B4** ✅ `program NAME(args)` binds its first parameter to the command-line
  args after the script path (0-based string array, empty when none); CLI accepts
  trailing args in run mode. `smoke_ask.bas` fixed by the feature (no edit);
  `args_test.bas` (+ zero/multi-arg goldens) wired. Additive — full battery stayed
  green. Commit `06eca7b`.
- **B5** ✅ Top-level `goto`/`gosub` now raise a structured runtime error (1003,
  "invalid control flow") instead of a stderr warning-and-continue; README
  overpromise fixed. Golden rebaselines: NONE (no golden exercised it); two new
  negatives added. Commit `fbe62b5`.
- **B6** ✅ `tests/run_gui_parse.sh` parses (not runs) every `examples/gui` +
  `examples/gi` file via `--ast`; wired into CLAUDE.md test list; both READMEs
  mark them manual display tests. No display virtualization. Commit `c1bf1c1`.

### Phase D1 — Infrastructure (small, mechanical) — ✅ DONE (pending review)
Created: `/DOGFOOD.md` (append-only friction log with the agreed template, seeded
with the D0 surprise harvest — status reflects current truth: B4/B5 resolved,
modulo + resume-next open); `docs/ai/START-HERE.md` manifest + placeholder
`UNLEARN.md`/`ERRORS.md`/`COOKBOOK.md` (filled in D3, ERRORS seeded from the
existing `_scratch` harvest); `AGENTS.md` (Codex entry) + a CLAUDE.md `## House
rules` section carrying byte-identical rules. Both entry files stay thin. No code
or golden changes.

1. `DOGFOOD.md` at repo root — append-only friction log; entry template
   (`## <date> — [human|CC|Codex] — while: <context>` / Type / Severity / What /
   Workaround). Seed with the D0 quirks as first entries.
2. `docs/ai/START-HERE.md` — manifest: reading order and purpose (unlearning file
   first, then cookbook, error catalog, pointer to the shared human reference).
   Placeholder files for the three, filled in D3.
3. `AGENTS.md` (Codex entry point) + update `CLAUDE.md` so both carry IDENTICAL
   house rules: (a) before writing gBASIC code, read `docs/ai/START-HERE.md` and
   follow it; (b) if you work around a gBASIC limitation/surprise, append a
   `DOGFOOD.md` entry before continuing; (c) evidence standards: tests-first,
   byte-exact goldens, measure don't assume, report what you could not verify.
   Keep both entry files thin — build, test, rules, pointers; no duplicated
   language content.

### Phase D2 — Human documentation sweep — ✅ DONE (pending review)
Survey `docs/` for accuracy against current behavior; fix stale content; ensure
the shared language reference covers every construct and builtin library at
reference (not tutorial) depth. Propose structure before rewriting anything large.

Done (TOC approved before writing; reference voice; each step its own commit):
- Baseline: adopted the three pre-existing doc edits (CLAUDE.md, README.md,
  reference.md) after review.
- Accuracy: documented `args` binding (B4) + `gbasic FILE [args...]`; noted
  top-level `goto`/`gosub` is a runtime error (B5).
- Front matter (reference-not-tutorial + START-HERE pointer) and a one-sentence
  diagnostic-position spec (1-based byte columns, inclusive/exclusive spans, →
  include/diagnostics.h + docs/ai/ERRORS.md).
- New sections: XML, GTK 3 GUI (concise, status-honest, steers to gi),
  Cryptography, First-Class Functions; Actors promoted to a top-level section.
- Regrouped the Core Builtins catch-all into named families (builtin set verified
  byte-identical); added a catalog-only Standard Library section (one line +
  pointer per stdlib toolkit, no API duplication).
Single source of truth held throughout; full battery green (15/15).

### Phase D3 — AI documentation layer (content)
1. `docs/ai/UNLEARN.md` — "gBASIC is not QBasic/VB": every D0 surprise, bluntly
   stated, each with a minimal correct-behavior snippet; include negative
   knowledge (features other BASICs have that gBASIC lacks + the gBASIC
   alternative).
2. `docs/ai/ERRORS.md` — diagnostic-code / runtime-error-domain catalog
   (code → meaning → typical cause → fix), generated from source where possible;
   state how derived and how to regenerate. Seed from
   `docs/ai/_scratch/D3_error_codes_harvest.md`. **Explicit task (D0 surprise
   S13):** read the `eval.c` error path and pin down the exact `on error resume
   next` resume semantics — black-box tests showed local-resume in one case and
   whole-statement abandonment in another; the true model must come from source,
   not experiment. Correct the `gbasic_error_handling_gotcha` memory once pinned.
3. `docs/ai/COOKBOOK.md` — one blessed idiom per major construct and module; every
   snippet is a runnable file under `examples/`/`tests/` that the cookbook
   references (never inline untested code).
4. Executable-docs gate: a runner that executes every doc code sample in the test
   suite so docs cannot rot. Add to CLAUDE.md's test list and `make dev`.

## Deferred — NOT started this track (do not begin without a new go-ahead)

### Dynamic property get/set hook + native `.property` / `.method()` syntax
Prerequisite for **idiomatic GI sugar**. Today member access (`obj.field`) is
hardcoded to `VALUE_RECORD` in eval (`AST_EXPR_FIELD` read path;
`resolve_lvalue_ref`/`assign_lvalue` write path), and method-call receivers are
limited to single-identifier record variables — there is no per-kind get/set
dispatch hook. Routing a native object's `.property` get/set and `.method()` calls
to C hooks requires: a getter branch in `AST_EXPR_FIELD`, a **setter callback**
branch in `assign_lvalue` (note `resolve_lvalue_ref` returns a `Value*` slot and
cannot express a setter), and `eval_call`/grammar work for chained/native-receiver
methods. Once landed, the ergonomic `gi` layer (e.g. `win.title = "x"`,
`win.present()`) can be written partly in gBASIC over the raw `gi.*` bridge.

### gi.new construct-time properties — ✅ DONE (Phase GI follow-up)
Shipped as the trailing `(name, value)` pairs form
(`gi.new("Gtk.ApplicationWindow", "application", app)`) over
`g_object_new_with_properties`. Namespace-level free functions shipped alongside
as `gi.invoke("Ns.function", args…)`. See **Phase GI → Follow-up** above.

### LSP v2 (deferred)
Hover, completion, signature help, go-to-definition, document symbols, incremental
sync (`TextDocumentSyncKind.Incremental`), workspace features, and multi-threaded
request handling. v1 is diagnostics-on-sync only. Real multi-diagnostic publishing
depends on the parser error-recovery item below.

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
