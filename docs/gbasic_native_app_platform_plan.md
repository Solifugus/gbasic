# gBASIC Native Application Platform — Implementation Plan

Status: **build-ready plan, not implemented.** Derived from
`docs/gbasic_native_app_platform_coverage.md`. Follows the conventions of `PLAN.md`
(byte-exact goldens, one phase per session, stop at each boundary, optional-dep code
behind `#if HAVE_*`).

**Goal:** make gBASIC capable of building sophisticated native GTK4 applications, then
prove it with a general **Native Application Platform Spike** — *before* any Studio
code. Every addition is general (test: *"would another sophisticated gBASIC desktop
app want this?"*). No Studio-specific APIs appear anywhere in this plan.

## Global rules (apply to every phase)

- [ ] Golden tests are **byte-exact**. Any stdout/stderr diff is a regression, never a
      rebaseline, unless explicitly approved for that specific diff.
- [ ] **One phase per session.** Stop at each boundary for review.
- [ ] New bridge/runtime C stays behind `#if HAVE_GIR` (GI work) or is unconditional
      (process/reflection/filesystem builtins), degrading to a clean runtime error when
      a feature is compiled out.
- [ ] Prefer **headless** tests (mirror `tests/run_gi.sh`, which runs display-free on
      Gio/GObject/GLib types); GUI-only behavior is a **manual display checklist**
      recorded in the phase, mirroring `tests/run_gui_parse.sh` (parse-only in CI).
- [ ] Run new C paths under **valgrind** (0 leaks / 0 errors) and, for GI work, under
      `G_DEBUG=fatal-criticals` so any GLib lifetime assertion fails the suite.
- [ ] `make dev` (all binaries) green + the full battery at every boundary.

## Layer legend

**A** generic GI bridge · **B(rt)** general language/runtime · **B** general GTK/AI
library · **C** general reusable component · **spike** proof point.

## Dependency graph (critical path to the proof point)

```
NAP-0 wiring
  → NAP-1 boxed(A) → NAP-2 out-params(A) → NAP-3 signal-return+events+mailbox(A)
  → NAP-4 GVariant+lists(A) → NAP-5 .prop/.method dispatch (B(rt), language)
  → NAP-6 process.run (B(rt)) → NAP-7 SourceEditor+gtk.bas (B) → NAP-8 SPIKE
Post-proof (independent, any order): NAP-9 reflection · NAP-10 filesystem
  · NAP-11 reconciler · NAP-12 DataGrid(C) · NAP-13 llm tool-calling
Explicitly out of scope: WI-5 callbacks, WI-9 runtime subclassing.
```

---

## NAP-0 — Wiring & test-harness precondition (no behavior change) · A — DONE

Establish that the platform's runtime deps resolve and set the headless test pattern
for the new GI features.

- [x] Confirm `gi.require("GtkSource","5")` resolves on the dev box; record the runtime
      dependency (`gir1.2-gtksource-5`, `libgtksourceview-5`) in the plan and README.
      (No compile/link change: GtkSourceView is driven through `gi`, not linked.)
      **Verified on the dev box:** girepository-2.0 2.88.0; `GtkSource-5.typelib` +
      `Gtk-4.0.typelib` present; the probe `tests/native_platform/require_typelibs.bas`
      resolves GLib/Gio/Gtk-4.0/GtkSource-5 and prints its golden line.
- [x] Add `tests/run_native_platform.sh` skeleton that **skips cleanly** when
      `HAVE_GIR=0` or the required typelibs are absent (mirror `run_gi.sh`). Wired: it
      skips on absent `girepository-2.0` (pkg-config gate) and on absent GTK4/GtkSource
      typelibs (a `gi.require` probe whose `could not load namespace` failure is treated
      as an environment SKIP; any other probe error FAILs). Extensible `positive_cases`/
      `negative_cases` arrays for later phases.
- [x] Full battery byte-exact (nothing behavioral added — only a new test runner,
      a new headless fixture, and docs). Suites green.

**Runtime dependency (recorded):** the Native Application Platform requires, at
runtime only, the GTK 4 and GtkSource 5 introspection typelibs — Debian/Ubuntu
`gir1.2-gtk-4.0` (+ `libgtk-4-1`) and `gir1.2-gtksource-5` (+ `libgtksourceview-5`).
Both are loaded through `gi` (not linked); the interpreter builds and runs without
them, and `tests/run_native_platform.sh` skips when they are absent.

**Completion:** typelib precondition documented; skipping runner wired; suite green.
**DONE** — stop-at-boundary honored; NAP-1 not started.

---

## NAP-1 — WI-1 boxed/struct values + WI-8 struct fields · A — DONE

Add a boxed-type value kind so structs cross the bridge; enable field get/set and
struct construction.

Source areas: `src/eval.c` gi block — value kinds (`ValueKind`), `Value` union,
`value_copy`/`value_free`, the four marshallers (`gi_value_from_gvalue`,
`gi_value_to_gvalue`, `gi_value_from_giarg`, `gi_giarg_from_value`), the `gi.*`
dispatch table.

- [x] **Tests first (FAILING, headless):** `tests/native_platform/boxed_struct.bas`
      (construct `Gdk.RGBA`, set/get four gdouble fields, alias/mutation, 1000× alloc/
      free loop, `type()`/print); six negatives (`negative_boxed_*`: unknown type,
      not-a-struct, unknown field, wrong value type, unsupported field type via
      `Pango.GlyphString.glyphs`, serialize-reject). Confirmed failing before impl.
- [x] `VALUE_GBOXED` kind + `struct GBoxedValue { GType type; gpointer box;
      size_t ref_count; int owned; }`. **DEVIATION:** reference-semantic refcount
      sharing (`value_copy` → `ref_count++`, `value_free` → `gboxed_release` →
      `g_boxed_free` at last ref), NOT value-semantic `g_boxed_copy` per copy. Required
      because `env_get` copies on every identifier read, so value semantics would make
      `gi.struct_set` mutate a throwaway copy. Mirrors the gobject wrapper exactly.
      Full lifecycle audited: `value_kind_name`/`builtin_type_name` ("gboxed"),
      `value_truthy` (true), `value_print`/`builtin_string_value` ("<gboxed>"),
      `value_storage_equal` + `eval_comparison` (handle identity, `=`/`!=` only),
      `encode_value_to_builder` + `serialize_value` (reject). Guarded `#if HAVE_GIR`.
- [x] Extended all four marshallers: `G_TYPE_BOXED` (GValue both ways, with
      `g_type_is_a` guard on set) and `GI_IS_STRUCT_INFO`+`G_TYPE_IS_BOXED` (GIArgument
      both ways). Non-boxed / plain-C structs stay unsupported (clean error, no coerce).
- [x] `gi.new_struct` (boxed, non-foreign, size>0 only; via `g_boxed_copy` of a zeroed
      buffer), `gi.struct_get`, `gi.struct_set` over `GIFieldInfo` (readable/writable
      flags enforced; strict value/field-type check — no silent coercion).
- [x] valgrind clean (0 lost / 0 errors on lifecycle + error paths); byte-exact suite;
      `G_DEBUG=fatal-criticals`. Zero rebaselines.

**Completion:** boxed roundtrip + field get/set green; zero rebaselines. **DONE** —
stop-at-boundary honored; NAP-2 not started. See the phase report re: the
reference-semantics deviation (plan struct sketch should be amended to include
`ref_count` and reference semantics).

---

## NAP-2 — WI-2 out/inout arguments + WI-10 GError edges · A

Make getters that return through out-parameters callable — the key unlock for text
buffers, tree models, geometry.

Source areas: `gi_invoke_callable` (13191-13270), specifically the arg loop that
currently rejects non-IN (13221-13228) and the return marshalling (13255-13259).

- [x] **Tests first (headless):** single scalar out + bool return
      (`GLib.ascii_string_to_unsigned` → record `{result, out_num}`); multiple outs
      (`GLib.uri_split` → record keyed by string/scalar/nullable out names); GtkTextIter
      caller-allocates struct out (`Gtk.TextBuffer` `get_start_iter`/`get_end_iter` →
      boxed iters fed back into `get_text` — the bridge proof); GError failure raises.
      (`gdk_rgba_parse` was rejected as a fixture: introspection models it as an
      instance-method with the out-struct as `self`, not an out-param.)
- [x] Direction-aware marshalling: IN unchanged; OUT allocates a `GIArgument` storage
      cell (or a NAP-1 boxed buffer for caller-allocates structs) and passes its address
      via `out_args`; INOUT (by-value scalar/enum only) passes one storage cell's address
      through both `in_args` and `out_args`. Return rule implemented exactly: no out →
      return value (unchanged, byte-exact for existing calls); one out + void → that
      value; else a record with per-out-name keys + `result` for a real return. Arity
      now counts only IN+INOUT (out-params are results). Single cleanup path;
      transfer-full string outs freed; caller-alloc struct wrapped as a boxed COPY then
      the raw buffer freed.
- [x] GError: the existing trailing-`GError**` throws path already surfaces failures as
      a raised gBASIC error with the message preserved and no leak (verified valgrind).
      Explicit `GI_TYPE_TAG_ERROR` out-args are not a distinct path in practice (throwing
      callables use the `throws` flag, excluded from `n_args`).
- [x] valgrind clean (0 lost / 0 errors) over 500× string-out/scalar-out/struct-out +
      repeated GError-failure; INOUT round-trip + rejection memory-safe; byte-exact suite;
      `G_DEBUG=fatal-criticals`. Zero rebaselines.

**Completion:** single + multi-out + struct-out + GError tests green; records/existing
calls unaffected. **DONE** — stop-at-boundary honored; NAP-3 not started.

**Deviations/notes:** (1) reference-semantic boxed from NAP-1 respected throughout —
struct outs wrap a copy, boxed IN args borrow, identity equality untouched. (2) INOUT
is implemented for by-value scalars/enums and refused (clean error) for pointer-shaped
INOUT; no deterministic golden ships because the only scalar INOUT callables in the
available typelibs (`GLib.ref_count_inc`, `once_init_*`) expose internal encodings
unsuitable for a byte-exact golden — INOUT is instead covered by a memory-safe
scratchpad run plus a stable pointer-INOUT rejection golden. (3) `gi.call`/`gi.invoke`
still require a GObject / free-function; methods on boxed *instances* (e.g. calling a
GtkTextIter method) remain out of scope (receiver dispatch is NAP-5). (4) an out-param
whose introspected name is a gBASIC reserved word (e.g. one literally named `end`) would
produce a record key unreachable via `.field` — noted, not hit by any shipped fixture.

---

## NAP-3 — WI-3 signal returns + WI-4 event sources + mailbox fd · A

Give handlers a return path, integrate the GLib loop, and expose the actor inbox for
non-blocking UI updates. This phase makes responsive, async gBASIC GTK apps possible.

Source areas: `gi_signal_marshal` (12915-12965, the `(void)return_gvalue` at 12918);
new builtins in the `gi.*` dispatch; actor `root_mailbox` (7447) for the inbox fd.

- [ ] **Tests first:**
  - (headless) `gi.timeout(ms,fn)` and `gi.idle(fn)` driving a `GMainLoop` that quits
    after N ticks; `gi.source_remove`.
  - (headless) `gi.watch_fd(fd,fn)` on a `pipe()` fires when data arrives.
  - (headless) spawn an actor, `gi.watch_mailbox(fn)` (or `gi.watch_fd(self_fd(),fn)`)
    fires and `receive` delivers the frame — proves async-result-on-loop.
  - (unit) WI-3 signal return: marshaller-level test that a handler's return `Value` is
    written into `return_gvalue` (a display-free returning signal is scarce; cover the
    conversion at unit level). `close-request` veto is a **manual display** check.
- [ ] Set `return_gvalue` from the handler's return via `gi_value_to_gvalue` (guard
      type mismatch; free the Value as today).
- [ ] `gi.timeout`/`gi.idle` → `g_timeout_add`/`g_idle_add` (handler returning false
      stops the source); `gi.watch_fd` → `g_unix_fd_add`; `gi.source_remove`. Reuse the
      `GiClosureData` + `function_resolve` closure pattern (12894-12922).
- [ ] Expose the inbox: a `self_fd()`/`gi.watch_mailbox(fn)` reading `root_mailbox.read_fd`.
- [ ] valgrind; byte-exact; fatal-criticals. Show diff, STOP.

**Completion:** timeout/idle/watch_fd/mailbox green headless; signal-return unit green;
close-request manual check recorded.

---

## NAP-4 — WI-6 GVariant + WI-7 arrays/lists · A

Unlock menus/actions/accelerators and real list data.

Source areas: the marshallers (NAP-1 sites) for container tags
(`GI_TYPE_TAG_ARRAY`/`GLIST`/`GSLIST`, currently NULL-only at 12862-12871); new
`gi.variant_*` builtins.

- [ ] **Tests first (headless):** build `gi.variant_string/bool/int` and read back;
      `gi.variant_parse(type,text)`; marshal a `char**`/`GStrv` out (e.g.
      `GLib.strsplit`) → gBASIC string array; pass a string array into a function taking
      `GStrv`; a `GList`/array of GObject out → object array.
- [ ] `GVariant` via `g_variant_new`/`parse`/`get_*`, marshalled as a boxed (NAP-1).
- [ ] Array/`GStrv`/`GList`(object|utf8) both directions, driven by the element
      `GITypeInfo` (`gi_type_info_get_param_type`); honor transfer.
- [ ] valgrind; byte-exact; fatal-criticals. Show diff, STOP.

**Completion:** variant + strv + object-list roundtrips green.

> **Bridge core complete.** After NAP-4, windows/panes/tabs/menus/lists/editor-iters/
> events/async are all reachable from gBASIC. Remaining critical-path work is ergonomics
> + the editor library + process exec.

---

## NAP-5 — LE-1 `.property` / `.method()` dispatch hook · B(rt) (language)

The largest-risk phase (core eval + grammar). Makes native-object code readable
(`win.title = "x"`, `box.append(child)`) instead of `gi.set`/`gi.call` noise. General
to all native-object usage, not GTK-specific.

Source areas (per `PLAN.md:547-559` deferred spec): `AST_EXPR_FIELD` read path,
`assign_lvalue`/`resolve_lvalue_ref` write path (note: needs a **setter callback**
branch — `resolve_lvalue_ref` returns a slot and cannot express a setter),
`eval_call`/grammar for native-receiver + chained methods.

- [ ] **Tests first:** `x = obj.prop` / `obj.prop = v` / `obj.method(args)` over a
      `VALUE_GOBJECT`; chained `a.b().c`; **byte-exact regression** that `VALUE_RECORD`
      field read/write and record-method behavior is unchanged (the primary risk).
- [ ] Getter branch in `AST_EXPR_FIELD` for non-record kinds → `gi` get; setter-callback
      branch in `assign_lvalue` → `gi` set; method-call receiver extension for gobject
      (and boxed) receivers → `gi.call`. Records untouched.
- [ ] Full battery byte-exact (records/methods identical); valgrind. Show diff, STOP.

**Completion:** gobject property/method sugar green; **zero** record-path rebaselines.

---

## NAP-6 — General process API: `process.run` · B(rt)

A general, shell-injection-safe process runner (not a shell builtin). Smallest step of
a coherent long-term API.

Source areas: new unconditional builtins (`src/builtins.c` registry + `src/eval.c`
impl); reuse the fork+exec plumbing (generalize the `execv` at 8360 to an arbitrary
path + argv + captured pipes).

- [ ] **Tests first (headless):** `process.run({command:"/bin/echo",args:["hi"]})` →
      `{exit_code:0, stdout:"hi\n", stderr:""}`; a nonzero-exit command; a missing
      command → clean runtime error; verify **no shell interpolation** (args with
      spaces/metachars pass literally); a `timeout` kills a long child.
- [ ] Implement `process.run` (structured argv → `execv`, capture stdout/stderr via
      pipes, exit status, optional cwd/env/stdin/timeout). Define — but do **not** yet
      implement — the `process.start`→handle async form as the documented next step.
- [ ] valgrind; byte-exact. Show diff, STOP.

**Completion:** run/exit/missing/no-shell/timeout tests green.

---

## NAP-7 — `SourceEditor` gBASIC library + `gbasic.lang` + minimal `gtk.bas` · B

The editor as a **reusable gBASIC library over generic GI** — no native code
(SW-1 withdrawn). Depends on NAP-1..5.

Source areas: new `stdlib/gtk.bas` (idiomatic constructors/helpers over `gi` + LE-1
sugar), `stdlib/sourceeditor.bas`, and a `gbasic.lang` GtkSourceView syntax file
shipped alongside the stdlib (install path documented like other stdlib assets).

- [ ] **Tests first:** parse-only CI coverage (wire the library + a demo into
      `run_gui_parse.sh`-style parsing); `gbasic.lang` validated by
      `GtkSourceLanguageManager` load in a headless-ish check where possible; a
      **manual display checklist** (open, type, undo, highlight a range, place a gutter
      mark, insert an inline child-anchor widget, scroll to a location).
- [ ] `stdlib/gtk.bas`: `gtk.app`, `gtk.window`, `gtk.box`, `gtk.paned`, `gtk.stack`,
      `gtk.scrolled`, `gtk.listbox`, `gtk.button`, `gtk.notebook`, enum/event helpers.
- [ ] `stdlib/sourceeditor.bas`: `SourceEditor.new`, `.text/.set_text`, `.cursor/.set_cursor`,
      `.scroll_to(line)`, `.highlight(ranges)`, `.mark(line,category)`,
      `.add_inline(line,widget)` (child anchor), `.on_change(fn)`, `.set_language("gbasic")`.
- [ ] `gbasic.lang` (keywords/strings/comments/numbers per `docs/TOKENS.md`).
- [ ] Core suite byte-exact; parse tests green; manual checklist recorded. Show diff, STOP.

**Completion:** editor library + `gbasic.lang` load and drive GtkSourceView on a display;
CI parse coverage green.

---

## NAP-8 — Native Application Platform Spike (proof point) · spike

A general demo app (`examples/native_platform/` — **no Studio naming**) exercising the
whole platform. This is the milestone that proves the thesis.

- [ ] **Tests:** the spike **parses in CI** (wired into `run_gui_parse.sh`); a **manual
      display acceptance checklist** covering the 11 capabilities from the coverage
      survey §10 (app+window; GtkSourceView editor with gBASIC highlighting; `GtkPaned`
      panes; `GtkStack`/notebook + scrolling list; dynamic add/remove; expandable
      structured value via `keys`/`values`; a modest table; async actor work via
      `gi.watch_mailbox` without UI freeze; `process.run` capturing output; programmatic
      highlight/`grab_focus`; one inline child-anchor widget at a source location).
- [ ] No GLib criticals under `G_DEBUG=fatal-criticals`; valgrind clean on the C paths
      it exercises.
- [ ] Record the acceptance run. Show diff, **STOP — proof point reached.**

**Completion:** spike parses in CI and passes the manual acceptance checklist using
**general capabilities only** — no native component beyond the bridge extensions, no
hard FFI.

---

## Post-proof phases (independent; do not start before the spike is accepted)

### NAP-9 — General reflection facility · B(rt)
`reflect.variables()` / `reflect.get(name)` / `reflect.inspect(value,depth)` over the
current `Env` (359-363). Tests: enumerate locals in a scope; recurse a nested record
(reusing existing `keys`/`values`). Cross-interpreter/paused-frame enumeration is
explicitly deferred to the interpreter-context refactor (PLAN Phase 3).

### NAP-10 — Filesystem builtins · B(rt)
`file_mtime`/`file_size` (from `stat`, exposed), `replace(temp,dest)` (real
`rename(2)`, crash-safe), optionally `watch_file` (inotify). Tests: mtime/size of a
fixture; atomic replace; (optional) inotify event.

### NAP-11 — Declarative widget-tree / reconciler library · B
Pure-gBASIC retained-mode UI: record-tree → widgets, diff+apply on change (the dynamic
mutation the old `gui` module lacked, done correctly over `gi`). Depends on NAP-5.
Tests: parse-only + manual mutation checklist.

### NAP-12 — General `DataGrid` component · C (the one justified native component)
A single fixed C `GListModel` GType adapting a gBASIC array/cursor to
`GtkColumnView`/`GtkListView` (virtualized, lazy, no widget-per-cell), plus a general
gBASIC `DataGrid` API. **General component, not Studio's.** Tests: headless model
unit test (item count / `get_item` on a large backing array); manual display grid with
10⁵+ rows, sort/filter/select. Justified because a `GListModel` implementation is
otherwise hard-FFI (WI-9).

### NAP-13 — `llm.bas` tool/function-calling · B
Add a `tools` parameter + `tool_use`/`tool_result` handling to the anthropic + openai
adapters (`stdlib/llm.bas`). Tests: offline fixtures (tool-call request/response
roundtrip, no network). Streaming stays deferred (needs `webclient` SSE).

### Explicitly out of scope (do not implement without a new go-ahead)
- **WI-5 callback marshalling** (libffi/trampolines) — only if custom drawing or
  async-callback APIs later prove necessary.
- **WI-9 runtime subclassing / vfunc override** — avoided by design throughout.

---

## Boundary discipline

Stop after every NAP-n for review. NAP-1→NAP-8 is the ordered critical path to the
proof point; NAP-9→NAP-13 are independent and picked up only after NAP-8 is accepted.
No phase implements a later phase's surface early. The plan builds a **general native
application platform**; Studio itself is a *separate* subsequent effort that consumes
this platform and adds only Studio-specific gBASIC (section/branch engine, replay,
agent orchestration, project/workspace model) on top.
