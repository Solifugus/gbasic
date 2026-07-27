# DOGFOOD.md — gBASIC friction log

Append-only. When writing gBASIC (a human, Claude Code, or Codex) hits a surprise,
a limitation, a bug, or a missing feature and has to work around it, add an entry
here **before continuing**. Newest at the bottom. Do not edit or delete past
entries; when something is later fixed, add a *new* entry (or a `Status:` update
line) rather than rewriting history.

This is a chronological **log**, not the reference. The systematic "gBASIC is not
QBasic/VB" catalog lives in `docs/ai/UNLEARN.md`; the error catalog in
`docs/ai/ERRORS.md`; the blessed idioms in `docs/ai/COOKBOOK.md`. Log entries may
name a quirk, but the durable explanation belongs in those files.

## Entry template

```
## <YYYY-MM-DD> — [human|CC|Codex] — while: <what you were doing>
- **Type:** language-surprise | bug | missing-feature | perf | doc-gap
- **Severity:** low | medium | high
- **What:** <what actually happened, with a minimal snippet if useful>
- **Workaround:** <what you did instead — or "resolved in <commit>">
```

---

## Seed entries — D0 audit surprise harvest

The first entries below come from the Phase D0 audit (2026-07-17/18). Status
reflects **current truth**, not the audit snapshot: two were resolved in Phase
D0.6, the rest remain open or are by-design.

## 2026-07-18 — CC — while: D0 audit (S2)
- **Type:** missing-feature
- **Severity:** medium
- **What:** `program main(args)` declared a parameter but never bound it —
  referencing `args` raised `undefined variable: args`, and the CLI rejected any
  trailing arguments (`rc=2`).
- **Workaround:** RESOLVED in `06eca7b` (D0.6 B4). A `program NAME(param)` block's
  first parameter now binds to the command-line args after the script path as a
  0-based string array (empty when none). `gbasic FILE [args...]` is accepted.

## 2026-07-18 — CC — while: D0 audit (S3)
- **Type:** language-surprise
- **Severity:** medium
- **What:** `goto`/`gosub` at the top level silently printed
  "…only supported inside functions for now" to stderr and *continued* — a no-op
  that looked like it ran.
- **Workaround:** RESOLVED in `fbe62b5` (D0.6 B5). Top-level `goto`/`gosub` now
  raise a structured runtime error (code 1003). They remain supported **inside
  functions** — that limitation is intentional and now fails loudly.

## 2026-07-18 — CC — while: D0 audit (S6)
- **Type:** missing-feature
- **Severity:** medium
- **What:** No modulo. `%` and `\` are lexer errors; there is no `mod()` /
  `remainder()` builtin; `7 mod 2` collides with duration syntax
  (`unknown duration unit: mod`).
- **Workaround:** OPEN. Compute `a - floor(a / b) * b` by hand. A `mod()`/
  `remainder()` builtin (or a `%` operator) is unbuilt — candidate for a future
  phase.

## 2026-07-18 — CC — while: D0 audit (S13)
- **Type:** language-surprise
- **Severity:** high
- **What:** `on error resume next` cannot be used inside a library function to
  catch a raise (e.g. from `decode`) and still return a clean fallback — the whole
  failing caller statement is abandoned and the assigned variable is left unbound.
  Black-box tests also showed inconsistent resume points (local-resume in one
  case, whole-statement abandonment in another), so the exact semantics are not
  yet pinned.
- **Workaround:** OPEN. PRE-VALIDATE with a non-raising checker and only call the
  raising builtin when it will succeed (see `stdlib/llm.bas` `_json_valid`). The
  true resume model must be read from the `eval.c` error path — tracked as the
  Phase D3 `ERRORS.md` task.

## 2026-07-18 — CC — while: D0 audit (BASIC-intuition cluster)
- **Type:** language-surprise
- **Severity:** low
- **What:** Several behaviors contradict QBasic/VB intuition (all **by design**,
  verified against the current binary): no numeric `for i = 1 to n` (only
  `for each x in coll … end for`); `<>` is a parse error (use `!=`); `mid` is
  **0-based** and strings are not indexable (`s[0]` raises — use `mid(s,i,1)`);
  comments are `'` only (`rem`/`//` error) and string concat is `+` only
  (`&` errors); `print` takes a single expression; keywords are case-insensitive
  but identifiers are case-sensitive; record-literal keys must be identifiers
  (`{ "a": 1 }` is a parse error — use `{ a: 1 }` or bracket-assign); no
  `dim`/`redim` (variables spring into being on assignment); `0` and `""` are
  falsy.
- **Workaround:** By design — use the gBASIC idiom in each case. The full,
  blunt treatment with snippets goes in `docs/ai/UNLEARN.md` (Phase D3); this
  entry just records that the audit surfaced them.

## 2026-07-18 — CC — while: D0 audit (perf traps)
- **Type:** perf
- **Severity:** high
- **What:** `arr[i]` inside a `while i < count(arr)` loop **deep-copies the whole
  array every iteration** → O(n²) (a 12k-item loop took 13s). `append(arr, x)`
  also returns a full copy each call, so accumulating a large list via `append` is
  O(n²) too.
- **Workaround:** Iterate large arrays with `for each x in arr` (0.15s for the
  same 12k loop); stream/count in place instead of accumulating with `append`
  when you don't need to keep the list.

## 2026-07-18 — CC — while: D3 UNLEARN verification
- **Type:** bug
- **Severity:** low
- **What:** `dim x` (dim is unsupported) prints `unexpected token DIM at 1:1` to
  **stderr**, produces empty stdout, and **exits 0** — an error message with a
  success exit code. Most parse errors exit nonzero; this path is inconsistent.
- **Workaround:** Don't use `dim` (there is no such statement — assign to create
  variables; see `docs/ai/UNLEARN.md`). Non-blocker for gbasic-studio, but the
  exit-code inconsistency is worth fixing; noted in PLAN.md deferred.

## 2026-07-18 — CC — while: D3 ERRORS.md (S13 follow-up)
- **Type:** language-surprise
- **Severity:** high
- **What:** Status update to the S13 entry above. The `on error resume next`
  semantics are now **pinned from source** (`error_generation` counter in
  `src/eval.c`) and proven by fixture. Precise model: a raise resumes at the next
  statement in the *same* statement list at every frame, but the raising
  statement's value is abandoned and that abandonment propagates through call
  boundaries — so a callee cannot catch-and-return a fallback, and `error.clear()`
  does not rescue the caller (it clears state, not the generation counter).
- **Workaround:** Unchanged — PRE-VALIDATE. Full model + proof in
  `docs/ai/ERRORS.md` and `examples/on_error_resume_next_test.bas`.

## 2026-07-19 — CC — while: NAP-2 (GI out/inout args) struct-out test design
- **Type:** language-surprise
- **Severity:** low
- **What:** GI out-parameters get packaged into a return record keyed by each
  out-param's *introspected name*. When a name collides with a gBASIC reserved word
  the key becomes unreachable: `Gtk.TextBuffer.get_bounds` has out args literally
  named `start` and `end`, and `end` is a keyword, so `r.end` fails to parse
  (`unexpected ... near END`). More generally any C API with a param named `end`,
  `to`, `step`, etc. produces a record field you cannot dot-access.
- **Workaround:** Used the single-out `get_start_iter`/`get_end_iter` methods instead
  (each returns its one out value directly, no record), sidestepping the reserved key.
  Durable fix would be bracket/string-key record access (e.g. `r["end"]`) or key
  sanitization; noted in the NAP-2 plan deviations. Not blocking NAP-2.

## 2026-07-20 — CC — while: NAP-3 (WI-4 event loop) writing actor+mainloop tests
- **Type:** language-surprise
- **Severity:** medium
- **What:** Carrying counter state across event-loop handler calls hit several scope
  rules at once: (1) a function cannot rebind a top-level scalar (`ticks = ticks + 1`
  inside a function creates a local; the global stays unchanged), though it CAN mutate
  a shared reference type — `rec.field = ...` on a top-level record persists. (2) When
  a `program main` block is present, top-level statements do NOT run at all (only the
  program block executes), so `s = {}` at file scope is dead code and handlers see
  `undefined variable`. (3) Assignments INSIDE `program main` do land in the global
  scope, so top-level handler functions can see them. (4) `load gi` must be inside
  `program main` too — a top-level `load` doesn't carry into the program block.
- **Workaround:** For actor tests (which need `program main`), initialise the shared
  handler-state record and `load gi`/`gi.require` INSIDE `program main`; mutate record
  fields (not scalars) from handlers. Also `sleep` is a function call `sleep(0.1)`, not
  a statement `sleep 0.1`. All by-design given shared-nothing scoping; noted so the next
  person writing loop/actor code doesn't rediscover it.

## 2026-07-20 — CC — while: NAP-5 (LE-1 .property/.method sugar) writing a record-method regression test
- **Type:** language-surprise
- **Severity:** low
- **What:** Declaring the receiver explicitly in a method — `function greet(this)` —
  is wrong: gBASIC binds `this` IMPLICITLY at the call site, so `this` must NOT be a
  declared parameter (`function greet()` + `this.name`, per `examples/constructor_test.bas`).
  Writing `greet(this)` makes the function arity 1, and `person.hello()` then fails the
  arity check. Worse, the arity failure ("hello expects 1 arguments") printed to STDOUT
  and execution CONTINUED (exit 0) rather than raising — an invalid method call is
  silently non-fatal at top level.
- **Workaround:** Never declare `this`; use it implicitly. (Coming from Python/JS `self`/
  explicit-receiver habits, this is an easy trap.) The non-fatal-arity-error-to-stdout
  behavior is pre-existing and unrelated to NAP-5; left as-is, noted here.

## 2026-07-21 — CC — while: pre-NAP-10 standalone fix — chained method receivers
- **Type:** language-surprise
- **Severity:** medium
- **What:** A method call whose receiver was itself a field/index/call expression did
  not parse: `a.b.method()`, `s.field.method()`, `a[0].widget.present()`,
  `make().method()`. Only a single-identifier (or `this`) receiver dispatched, so real
  UI/data code had to bind the receiver to a local first (`w = s.button; w.set_label(...)`).
  Hit repeatedly in NAP-7, NAP-8, and NAP-9.
- **Workaround:** RESOLVED (pre-NAP-10). Generalized the grammar: a new `AST_EXPR_CALL`
  `receiver` field + `ast_method_call`; postfix / ident_suffix / call_statement productions
  for `receiver DOT (IDENT|QUALIFIED_IDENT) LPAREN args RPAREN` (the lexer folds a trailing
  `field.method(` into one QUALIFIED_IDENT, so both token shapes are handled). Eval evaluates
  the receiver ONCE (an lvalue receiver as a live cell so record `this` mutation persists; a
  call-result as a temporary) and routes through the SAME dispatch (eval_user_function_with_receiver
  / gi_invoke_method_on). `a.b().c = x` stays invalid (never an lvalue). One narrow remaining
  limitation: a method call on a call-result receiver as a BARE statement (`make().show()` with
  the result discarded) doesn't parse — works in expression position; documented in reference.md.

## 2026-07-21 — CC — while: NAP-10 (filesystem builtins) — writing the atomicity stress test
- **Type:** language-surprise
- **Severity:** low
- **What:** The `file` modifier does not work as an inline postfix in expression/argument
  position. `atomic_replace("/tmp/x".file, dst)` and `write("/tmp/x".file, "data")` raise
  `field access expects a record` — `"literal".file` is parsed as a field access on the
  string, not as the file-value modifier. In a concurrent test this was silent-until-run:
  the writer errored on its first line, never created its `done.flag` sentinel, and the
  reader loop hung until killed.
- **Workaround:** Use the modifier-ASSIGNMENT form to make a file value, then pass the
  variable: `sa(file)= "/tmp/x"` then `write(sa, ...)` / `atomic_replace(sa, dst)`. (This
  is the same form every existing file example uses — e.g. `source(file)= path`.) The `.file`
  postfix-in-expression case is left OPEN as a separate ergonomics gap, out of scope for
  NAP-10. Note the NAP-10 builtins themselves accept both a file reference and a plain path
  string for their arguments, so string paths also sidestep this.

## 2026-07-22 — CC — while: NAP-11 (gtkui reconciler) — signal bookkeeping lost across updates
- **Type:** language-surprise
- **Severity:** medium
- **What:** A helper that mutated a nested record field of its argument (`rnode.sigs[s] = id`)
  had no effect on the caller's record: records are copy-on-write value types, so the helper
  mutated its own copy. The reconciler's per-widget signal-id map was therefore never recorded
  in the persisted tree, so later `update`s disconnected stale/empty ids and signal connections
  accumulated — a button fired its handler 3× after two reconciles instead of once.
- **Workaround:** RESOLVED in the library design (not a runtime change). Helpers that update
  rnode state must RETURN the new value and let the owner assign it into a field of a record it
  itself returns up the call chain (`old.sigs = gtkui._reconcile_signals(old.widget, old.sigs, v)`),
  never mutate a parameter's nested field in place. General rule: with COW records, "mutate through
  a helper" silently no-ops; thread state via return values.

## 2026-07-22 — CC — while: NAP-11 (gtkui reconciler) — scalar global writes in a handler don't persist
- **Type:** language-surprise
- **Severity:** medium
- **What:** Inside a `gi.connect`ed handler, `counter = counter + 1` on a global scalar does not
  update the global — the assignment binds a new function-local `counter`, so every invocation
  reads the original value and the global never changes. A `record.field = ...` write on a global
  record, by contrast, DOES persist (the existing global record is resolved and its field mutated).
- **Workaround:** Keep all mutable application state in a single global RECORD and mutate its
  fields (`state.count = state.count + 1`, `state.items = append(...)`, `state.handle = update(...)`),
  as `examples/native_ui/dynamic_list.bas` does. Bare scalar globals are effectively read-only from
  inside a handler. (gBASIC has no closures, so a record is also how a handler shares state anyway.)

## 2026-07-23 — CC — while: NAP-12 DataGrid perf profiling — array reads/appends were O(n)/O(n²) (FIXED)
- **Type:** perf
- **Severity:** high
- **What:** Arrays stored elements in a bare `{items,count}` with no sharing, so
  `value_copy` deep-copied the whole array. Every rvalue read went through
  `env_get -> value_copy`, making `b = a`, `a[i]`, and passing an array to a
  function all O(n); a `while` loop indexing an array was O(n²). Separately,
  `append` mutated in place but returned a full deep copy, so `a = append(a, x)`
  in a loop was O(n²) — and the far more common bare `append(a, x)` statement
  (669 of ~690 call sites) paid that copy for a discarded value. Measured: 200
  reads of a 128k array ≈ 1.1s; building a 25k record array ≈ 88s. This surfaced
  while profiling an array-backed DataGrid but is a language-wide issue
  (analytics/ETL/finance/function calls/Studio), not DataGrid-specific.
- **Workaround:** RESOLVED at the runtime level, not worked around. Arrays now use
  a reference-counted copy-on-write backing store (`docs/array_cow_design.md`):
  copy/assign/arg-pass/read are O(1), append is amortized O(1), a build loop is
  O(n), and mutation of a shared array detaches once (O(n)). Observable value
  semantics are unchanged and byte-exact (the full golden suite, including
  `watcher_mutator_notification_test`, passes unmodified). Until this fix the
  documented workaround for hot loops was `for each` (evaluates the container
  once) rather than indexed `while`; that workaround is no longer necessary.

## 2026-07-23 — CC — while: NAP-12 DataGrid — factory bind won't start without a program-scope grid reference
- **Type:** bug
- **Severity:** medium
- **What:** Building a GtkColumnView datagrid entirely through stdlib/datagrid.bas
  (which stores each column's GtkSignalListItemFactory in the program-global
  `_DATAGRID` registry), then presenting the window and pumping the loop, realizes
  rows (the native model's `get_item` fires, ~207 for a 1e6-row grid) but the factory
  "bind" signal NEVER fires, so cells stay blank. The `setup` signal on the same
  factory DOES fire. It is not retention (the factory gobject survives COW churn) and
  not the handler being a library function (setup proves that works). The fix that
  reliably makes bind fire is a **program-scope** read of the registry factory path
  (`keep = _DATAGRID.grids[0].columns[0].factory`, even discarded) — the SAME read
  performed *inside* a library function does not help. So it is scope-specific and
  smells like a gi/COW object-lifetime-or-timing interaction, not yet isolated.
- **Workaround:** Hold the grid/its widgets from program scope before showing (real
  apps do this; `examples/native_ui/datagrid_demo.bas` and the display smoke reference
  the factories). Data access (`datagrid.cell`), the native model, and all non-display
  behavior are unaffected — the deterministic tests do not depend on rendering. Should
  be root-caused before Studio (which renders grids). Logged as a NAP-12 finding.
- **RESOLVED 2026-07-23 — this entry was a MISDIAGNOSIS; there is no such bug.** The
  program-scope reference never mattered. `GtkColumnView` realizes and binds its
  visible rows when the view is first given a size — i.e. when it is added to a
  window (`win.set_child`) — **not** on `present()` and not when the main loop runs.
  The original measurement reset the bind counter *after* `present()`, which zeroed
  binding that had already completed, and then read zero. `get_item` looked like it
  "still fired" only because `item_requests` accumulates earlier still, at
  `view.set_model()` — so the two counters were simply being read on opposite sides
  of the work. Adding/removing the `keep = ...factory` line changes nothing:
  instrumented A/B runs give identical setup/bind counts with and without it, and a
  grid built inside a helper that returns (no factory, column, view or model held
  anywhere outside the registry) binds normally under record/array churn, repeated
  refresh, multiple grids, and create/destroy cycles. Covered by
  `tests/datagrid/lifetime.bas`, which fails (setup true, bind false — the exact
  symptom reported here) when the bind connection is removed. The workaround lines
  have been deleted from the demo and the display smoke, and
  `tests/datagrid/display_smoke.bas` now asserts `req > 0` and `accesses() > 0`
  instead of a bound that passed trivially at zero. **Lesson: when instrumenting a
  toolkit, establish *when* the work happens before choosing where to reset the
  counters — a counter reset past the work reads as "never happened".**

## 2026-07-23 — CC — while: NAP-12 tests — `call(args) = value` misparses as a modifier clause
- **Type:** language-surprise
- **Severity:** low
- **What:** An equality comparison whose left side is a function/method call with
  parentheses, e.g. `string(datagrid.row_count(g) = 100)` or `if reflect.kind(x) =
  "record"`, is a parse error ("unexpected MOD_LPAREN, expecting LPAREN"): the lexer's
  context-sensitive `(...)=` modifier tokenization fires on the `) = ` and treats it as
  a modifier definition. A bare-variable left side (`n = 100`) is fine.
- **Workaround:** Bind the call result to a variable first, then compare the variable:
  `n = datagrid.row_count(g)` then `string(n = 100)`. Applies anywhere `call(...) =`
  appears in an expression, not just inside `string(...)`.

## 2026-07-24 — CC — while: NAP-13 llm tools — `error` is reserved, so `{ error: ... }` will not parse
- **Type:** language-surprise
- **Severity:** low
- **What:** `error` is a statement keyword (token ERROR_VALUE), so it cannot be used as
  a record-literal key or a dotted field: `return { error: "no such customer" }` is a
  parse error ("unexpected ERROR_VALUE, expecting IDENT or RBRACE or NEWLINE"), and so
  is reading `c.error`. This bites hardest when designing an API, because `error` is
  the single most natural name for a failure field — the obvious shape for "a tool
  reports failure by returning a record" is exactly the shape the parser rejects.
- **Workaround:** Set and read the key dynamically — `r = {}` then `r["error"] = msg`,
  and `c["error"]` to read. Because that is ugly at every call site, `llm.bas` ships a
  constructor, `llm.tool_error(message)`, so users never write the literal. The same
  applies to any other reserved word used as a field name.

## 2026-07-24 — CC — while: NAP-13 llm tools — `encode` emits a gBASIC JSON dialect, not standard JSON
- **Type:** limitation
- **Severity:** medium
- **What:** `encode` writes gBASIC's own spellings for the two empty values — `nothing`
  and `unknown` — rather than `null`. `encode({a: nothing})` yields `{"a":nothing}`,
  and `decode("{\"x\":null}")` then `encode` round-trips to `{"x":nothing}`. It is
  self-consistent inside gBASIC (`decode` accepts the dialect tokens, and
  `examples/serialization_test.bas` pins the behavior deliberately), but it is NOT
  standard JSON, so any HTTP body built with `encode` can be rejected by an external
  API. NAP-13 hit this for real: the openai assistant tool-call turn carries
  `"content": null`, which replayed verbatim became `"content":nothing` and would have
  made every tool continuation a malformed request.
- **Workaround:** `llm.bas` defends its own wire path — `_json_safe` recursively drops
  empty-valued fields from replayed provider messages (omitting `content` is valid for
  an assistant message carrying tool_calls), and an empty tool result is emitted as
  `null`. Verified by parsing every emitted payload with a real JSON parser, not
  gBASIC's own `decode` (which would accept the dialect and prove nothing).
  A general fix — a strict-JSON mode for `encode` — was prototyped and then REVERTED:
  it is a language-level behavior change that moves a golden which documents the
  current semantics on purpose, so it needs its own decision rather than riding along
  in a library phase. Flagged SHOULD FIX BEFORE STUDIO: any gBASIC program that
  POSTs `encode` output to a third-party API is exposed to this, not just `llm.bas`.

## 2026-07-24 — CC — while: strict-JSON investigation (follow-up to the NAP-13 finding)
- **Type:** bug
- **Severity:** medium
- **What:** RESOLUTION of the "encode emits a gBASIC JSON dialect" entry above, and the
  architectural lesson behind it. **Internal round-trip serialization and standards-
  compliant interchange serialization are different concerns and must not share one
  ambiguous contract.** gBASIC had three serializers with overlapping jobs and no
  stated boundary: `serialize`/`deserialize` (exact binary round-trip),
  `encode`/`decode` (documented as "JSON serialization" but actually a JSON-*like*
  dialect), and — because `encode` could not be trusted on the wire — two independent
  ad-hoc sanitizers grown in libraries: `crypto.json_encode` (a flat, one-level
  standard-JSON encoder) and `llm._json_safe`. Whenever a library has to write its own
  JSON encoder to talk to the outside world, the core contract is the thing that is
  wrong. The investigation also found `encode` prints non-finite numbers as bare
  `nan`/`inf` — output gBASIC's OWN `decode` rejects, so for those values it is not a
  self-consistent dialect at all but a plain round-trip bug.
- **Workaround:** RESOLVED by adding a separate strict path rather than redefining the
  existing one: `json_encode(value)` (RFC 8259) and `json_encodable(value)` (a
  side-effect-free preflight, needed because a raising serializer cannot be caught
  from a library frame). `nothing` → `null`; `unknown`, NaN/infinity, and all typed and
  live values are REFUSED rather than coerced into an invented token. `encode`/`decode`
  and `string` are byte-for-byte unchanged — the dialect is long-standing, pinned by
  `examples/serialization_test.bas`, and useful for gBASIC-to-gBASIC round-trips.
  `llm.bas` now builds every request body with `json_encode`, so `_json_safe` is gone
  (replaced by the much narrower `_drop_unknown`). Standards compliance is proved by an
  INDEPENDENT parser in `tests/run_json_strict.sh`, never by gBASIC's own permissive
  `decode`. **Clears the SHOULD FIX BEFORE STUDIO flag from the NAP-13 entry.**
  Still open (DEFERRED, documented in docs/reference.md): `encode`'s bare `nan`/`inf`,
  and `crypto.json_encode` remaining a separate flat encoder.

## 2026-07-25 — CC — while: STU-0 (Studio persistence backbone, pure gBASIC)
- **Type:** language-surprise
- **Severity:** medium
- **What:** A `load` statement placed at the top level (before `program main`) is
  silently inert when a `program` block is present — top-level statements don't run
  in that case. The failure surfaces far from the cause: calling a function from the
  un-loaded library raises `invalid function call: lib.func` (or `undefined variable`
  for a bare-name library export), which reads like the library or function doesn't
  exist. Cost real debugging time before the pattern was recognized.
- **Workaround:** Put every `load` INSIDE the `program` block (as the workbench/
  crypto examples do). All studio_* loads live at the top of `program main`. Already
  noted in prior memory as "load/state must be inside program main," but the
  misleading error message is what makes it a repeat trap. Would help: either run
  top-level `load` declarations even with a program block, or make the call-site
  error say "library 'lib' is not loaded".

## 2026-07-25 — CC — while: STU-0 (filesystem persistence)
- **Type:** language-surprise
- **Severity:** low
- **What:** Two filesystem sharp edges hit while writing the store: (1) `exists(f)`
  rejects a **directory** reference — `d(dir)= path; exists(d)` raises "exists expects
  a file reference"; you must probe a directory's existence via a `(file)` reference
  to the same path. (2) `make_dir` raises on an already-existing directory (EEXIST)
  rather than being idempotent, so every create must be guarded by an existence check.
- **Workaround:** `ensure_dir` in `stdlib/studio_store.bas` splits the path, and for
  each segment probes existence with a `(file)` reference and only calls `make_dir`
  when absent. Works, but an idempotent `make_dir` (or `make_dir` accepting an
  existing dir as a no-op) and an `exists` that accepts either reference kind would
  remove the ceremony. DEFERRED (not a blocker).

## 2026-07-25 — CC — while: STU-0 (corrupt-file recovery)
- **Type:** missing-feature
- **Severity:** medium
- **What:** Graceful recovery from a corrupt/truncated JSON store needs to detect
  invalid JSON WITHOUT raising, because `decode` raises on malformed input and gBASIC
  cannot catch a raise and continue (the standing `on error resume next` limitation).
  There is no non-raising decode/validity builtin (`json_encodable` is encode-side
  only).
- **Workaround:** Wrote a recursive-descent JSON validator `stdlib/studio_json.bas`
  (`valid(text)` → bool, `parse_or(text, fallback)`), mirroring the private validator
  `stdlib/llm.bas` already had to grow for the same reason. Two libraries now hand-roll
  a JSON validator to work around the same gap — a core `json_valid(text)` (or a
  non-raising `try_decode`) would retire both. SHOULD FIX.

## 2026-07-26 — CC — while: STU-2 (document manager, filesystem)
- **Type:** missing-feature
- **Severity:** medium
- **What:** No way to test whether a path is a directory without risking an
  uncatchable raise. `read(f)` and `file_size(f)` both RAISE on a directory, and
  gBASIC can't catch a raise, so a "user selected a directory instead of a file"
  path would crash Studio if probed directly. `list(dir)` returns empty for BOTH a
  file and an empty directory, so it can't distinguish them either.
- **Workaround:** `studio_docs._is_dir` asks the PARENT directory for the entry and
  reads its `type` ("file"/"folder") — reliable even for empty dirs, but O(parent
  size) per check. A core `is_dir(path)` / `file_type(path)` (non-raising) would
  remove the parent scan. SHOULD FIX (companion to the earlier non-raising-JSON gap).

## 2026-07-26 — CC — while: STU-2 (record field names)
- **Type:** language-surprise
- **Severity:** low
- **What:** `next` is a reserved word (`resume next`), so `{ next: 1 }` is a parse
  error ("unexpected NEXT") — another reserved word that cannot be a record field
  key, joining `error` and `end`. Also re-confirmed: `new` cannot name a function
  (`function new()` fails; use `create()`), and a library function whose name shadows
  a builtin (`find`) prints a load-time warning on stderr.
- **Workaround:** Renamed the field to `next_doc`, the constructor to `create()`, and
  the lookup to `find_open`. No fix requested — the durable list of reserved words
  belongs in docs/ai/UNLEARN.md; logging the specific ones that bit here.

## 2026-07-26 — CC — while: STU-2 (saving source files)
- **Type:** language-surprise
- **Severity:** low
- **What:** `atomic_replace(temp, dest)` is rename(2), so the saved file takes the
  TEMP file's inode: permissions reset to the umask default (verified 600 -> 664) and
  a symlink `dest` is REPLACED by a regular file rather than written through. For a
  user's source tree that silently changes file semantics. There is no `chmod`/`lstat`
  builtin to write-temp-then-fix-perms-then-rename, so a semantics-preserving atomic
  save isn't expressible.
- **Workaround:** Studio saves SOURCE files with in-place `write` (preserves perms and
  symlink target), and keeps `atomic_replace` only for its own metadata stores (where
  perms don't matter). Documented in docs/gbasic_studio_stu2.md. DEFERRED — a
  perms/symlink-preserving atomic write (or chmod/lstat) is a general platform nicety,
  not a blocker.

## 2026-07-26 — CC — while: PLAT-OUTLINE (source_outline byte-slice test driver)
- **Type:** language-surprise
- **Severity:** low
- **What:** Two surprises writing a driver that slices source by the byte offsets
  `source_outline` returns. (1) `mid(s, start, len)` is **0-based** — `mid("abcdef",
  0, 3)` = `"abc"`, `mid(..., 1, 3)` = `"bcd"` — not 1-based as VB/QBasic intuition
  expects. (2) `mid` counts **codepoints**, not bytes, so it disagrees with byte
  offsets on any multi-byte UTF-8 text. Also: `"\r"` is not a valid string escape
  (only `\n`, `\t`, `\\`, `\"`, `\u{}`), so escaping a CR needs `from_bytes([13])`.
- **Workaround:** For byte-exact slicing use `byte_at` + `from_bytes` (build the byte
  array over `[start, start+len)` and `from_bytes` it) rather than `mid`; this matches
  `source_outline`'s byte-offset convention exactly. Dropped `\r` escaping (fixtures
  use `\n` only). Documented the byte convention in `docs/source_outline_design.md`
  §16.2. Not a bug — just a convention gap worth stating for any offset consumer.

## 2026-07-27 — CC — while: PLAT-PROC (writing the live-child-control test fixtures)
- **Type:** language-surprise
- **Severity:** low
- **What:** `contains(haystack, needle)` looks like the natural "does this string
  contain that substring" test, but it is **array-only**: `contains("hello world",
  "lo w")` raises `contains expects an array`. The sibling `find` *does* accept
  strings (`find(value, target)` — "index of a substring in a string, or of an
  element in an array"), so the pair is asymmetric: `find` is polymorphic over
  strings and arrays, `contains` is not.
- **Workaround:** Test substring presence with `find(s, sub) != nothing` — note the
  absent case is `nothing` for strings, so comparing against `-1` silently never
  matches. Used throughout the PLAT-PROC fixtures to poll a child's accumulated
  output for a marker. Not a bug (documented behavior), but the asymmetry is easy to
  trip over when reaching for the more obviously-named builtin first.
