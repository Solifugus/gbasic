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

## 2026-07-28 — CC — while: PLAT-STREAM (splitting captured child output into lines)
- **Type:** language-surprise
- **Severity:** medium
- **What:** String escapes are **not** processed inside a modifier clause. `p(split
  "\n") = s` splits on the two-character sequence backslash-n, not on a newline, so
  a multi-line string comes back as a single element with no error and no warning:
  ```
  s = "a" + "\n" + "b" + "\n"    ' 4 bytes -- "\n" IS a newline here
  p(split "\n") = s              ' count(p) = 1   <-- silently wrong
  nl = "\n"
  r(split nl) = s                ' count(r) = 3   <-- correct
  ```
  The same literal means different things depending on whether it sits in an
  ordinary expression or in a `(...)=` modifier clause, because the modifier body is
  lexed in a separate content mode (`modifier_content_mode`, `src/lexer.c`) that
  hands the text through raw.
- **Workaround:** Bind the separator to a variable first (`nl = "\n"`) and pass the
  variable into the clause. Used in the PLAT-STREAM volume/diagnostic fixtures, which
  split captured child output on newlines. The failure is silent — a line count of 1
  where thousands were expected — so it is worth knowing before it is debugged.

## 2026-07-28 — CC — while: PLAT-STDERR (writing to standard error before the statement existed)
- **Type:** missing-feature
- **Severity:** high
- **What:** A gBASIC program had no route to standard error. The runtime wrote there
  (runtime errors, `--json-diagnostics`) but the language exposed nothing: no
  `print` redirect form, no file-handle mechanism, no builtin under any name — 157
  builtins, checked one by one. So a command-line tool written in gBASIC had to fold
  its progress and error messages into stdout, and a caller piping it anywhere got
  data and chatter mixed together with no way to separate them.
  The only reachable workaround was a Linux path trick, and it is a trap:
  ```
  f(file)= "/dev/stderr"
  ok = write(f, "message\n")     ' appears on fd 2 -- but see below
  ```
  `write` opens with mode `"wb"`, so when the caller has redirected stderr to a
  file, this **truncates that file**, destroying diagnostics already written to it.
  Measured: a log holding `PRE-EXISTING-DIAGNOSTIC` contained only `SECOND`
  afterwards. It also opens a second, independently-buffered file description via
  `/proc`, so ordering against the runtime's own writes is not guaranteed; it is
  Linux-specific; and it takes a string rather than rendering a value.
- **Workaround:** Resolved — `print to error <expression>` (PLAT-STDERR). It shares
  `print`'s rendering path, so every value shape prints identically, and stderr is
  unbuffered so it needs no flushing. Reference: `docs/reference.md` (Statements →
  "Print to standard error"); tests: `tests/run_stderr.sh`.

## 2026-07-28 — CC — while: PLAT-STDERR (running a fixture twice under --json-diagnostics)
- **Type:** language-surprise
- **Severity:** low
- **What:** `--json-diagnostics` takes a FILE and nothing after it. Passing program
  arguments the way the plain run mode accepts them fails with the usage text and
  exit 2, even though that usage line itself reads `FILE [args...]`:
  ```
  ./gbasic prog.bas one two                       ' args=2
  ./gbasic --json-diagnostics prog.bas one two    ' usage: ...  (exit 2)
  ```
  The dispatch for that mode matches `argc == 3` exactly (`src/main.c`), and
  `--help` documents it as `--json-diagnostics FILE`, so this is a documented
  limitation rather than a bug — but the shared usage line implies otherwise, which
  is what made it surprising. Same for `--ast`, `--tokens` and `--add-loads`.
- **Workaround:** Pass the parameter through the environment instead and read it
  with `env(name)`, launching via `/bin/sh -c "VAR=value ./gbasic --json-diagnostics
  FILE"`. Used in `tests/native_platform/plat_stderr_json.bas`, which needs one
  fixture to run in two modes from an identical path, line and column so the
  runtime's diagnostic is byte-comparable between the runs.

## 2026-07-29 — CC — while: PLAT-STRIDX/PLAT-ARRIDX (measuring cost curves)
- **Type:** missing-feature
- **Severity:** medium
- **What:** gBASIC cannot time anything shorter than a second. `epoch()` returns
  whole seconds, so every duration a program can measure itself is an integer:
  ```
  t0 = epoch()
  ... work ...
  print "secs=" + (epoch() - t0)      ' prints 0, or 1, and nothing between
  ```
  Two phases of performance work needed sub-second numbers, and none of them
  could be produced from inside the language. It also means a gBASIC program
  cannot measure its own hot spots, cannot report a rate, and cannot implement a
  timeout finer than a second — and STU-5A's stored run durations are whole
  seconds for this reason, not by choice.
- **Workaround:** Time the whole process from outside, with
  `/usr/bin/time -f "%e" ./gbasic prog.bas` or `date +%s.%N` either side, and
  subtract a do-nothing run to remove startup. Used throughout
  `tests/run_stridx.sh` and `tests/run_arridx.sh`, which is also why their
  performance tiers live in the shell runner rather than in a `.bas` fixture.
  The fixture prints a checksum so the runner can tell a fast run from one that
  silently did nothing. A monotonic sub-second clock is already on the platform
  list; this is the concrete cost of not having one.

## 2026-07-29 — CC — while: cleaning up after STU-4/STU-4B test runs
- **Type:** bug
- **Severity:** medium
- **What:** A child started with `process.start` outlives its parent
  indefinitely if the parent is killed rather than exiting cleanly. Found four
  `./gbasic --json-diagnostics …/run-doc-1-1.bas` processes still sleeping two
  days after the runs that created them (started Jul 27 16:18 and Jul 28 03:07,
  reparented to init, three of them with their working directory already
  deleted). Only the ACTOR path arms a death signal — `eval_run_actor` calls
  `prctl(PR_SET_PDEATHSIG, SIGTERM)` and additionally re-checks `getppid() == 1`
  to close the fork/arm window (`src/eval.c:9141`). The two `process.*` fork
  sites, `process_launch` and `process_do_run`, fork and `execvp` with neither.
  STU-4's session engine does sweep orphans *during* a run — the STU-4B scratch
  file left on disk recorded `orphans_before_sweep=2 swept=2` — so the gap is
  specifically the case where the sweeping parent never gets to run: an
  interrupted or killed test runner, or any gBASIC program that is SIGKILLed
  while holding live child handles.
- **Workaround:** None applied; killed the strays by hand (all four went down on
  SIGTERM). NOT fixed here — arming PDEATHSIG on `process.start` would change
  the semantics of any deliberately detached child, so it wants its own decision
  rather than a drive-by. Recorded so the next process-lifetime phase starts from
  evidence. Until then, a runner that launches gBASIC children should kill its
  own process group on exit rather than trusting the child to notice.

## 2026-07-29 — CC — while: PLAT-DEBT 2
- **Type:** bug
- **Severity:** medium
- **Status update to the 2026-07-28 `--json-diagnostics` entry above:** RESOLVED
  for `--json-diagnostics`, which now accepts and binds `FILE [args...]` exactly
  as run mode does. It was the mode in the wrong: it RUNS the program (it reaches
  `eval_program`, and already called `eval_set_program_args` with an empty list),
  so arguments were always meaningful there — the dispatch just matched
  `argc == 3`.
  The other three named in that entry — `--ast`, `--tokens`, `--add-loads`
  (and `--add-uses`) — were the *documentation* in the wrong, and are unchanged:
  they only inspect the source and never run it, so a program argument there
  could only be accepted and ignored. They keep rejecting it, and the usage line
  now shows two shapes instead of implying one. The env-var workaround recorded
  in the earlier entry is no longer needed for `--json-diagnostics`.

## 2026-07-29 — CC — while: PLAT-DEBT 4
- **Type:** bug
- **Severity:** medium
- **Status update to the 2026-07-28 modifier-clause escape entry above:** RESOLVED.
  A string literal now means the same thing inside a `(...)=` modifier clause as
  it does anywhere else. `p(split "\n") = s` splits on a newline; `\t`, `\"`,
  `\\` and `\u{...}` all behave as they do in an ordinary expression, and an
  invalid escape raises instead of passing through as literal text.
- **What it actually was:** not one bug but THREE string scanners that had to
  agree and did not, none of which is the ordinary string lexer — a modifier
  clause is deliberately captured as raw text so a multi-word phrase like
  `split ","` can be separated into name and argument only once the registered
  modifiers are known.
  1. `modifier_lparen_ahead` (`src/parser.y`) decides whether `(` opens a clause
     at all. Its string skip had no escape handling, so `"\""` read as two
     strings, the second unterminated — it gave up and the clause silently
     degraded to an ordinary parenthesised expression.
  2. `modifier_content_token` (`src/lexer.c`) finds the `)` that closes the
     clause. It tested `current[-1] != '\\'`, the usual broken approximation,
     which cannot tell `\"` from `\\"`. Its direct sibling `lens_content_token`,
     thirty lines below, already tracked escapes properly — which is what makes
     this an oversight rather than a decision.
  3. `eval_modifier_arg_text` (`src/eval.c`) turns the argument into a value. It
     stripped the quotes and took the bytes verbatim. THIS is the one that
     produced the silent wrong answer in the original report; the other two
     produced loud parse failures.
- **Workaround:** none needed now. The variable-binding workaround still works
  and the fixtures that use it were left alone. Held in step by
  `examples/modifier_escape_test.bas`, which emits every escape through both the
  clause path and the ordinary literal path and requires the two to be
  byte-identical, so fixing one scanner and not the others fails a test.

## 2026-07-31 — CC — while: Studio STU-2B (wiring the shell's first input handlers)
- **Type:** missing-feature
- **Severity:** medium
- **What:** the `gi` bridge has **no way to emit a signal**. The dispatch table
  (`gi_eval_call`, `src/eval.c:15560`) is `require/new/new_struct/get/set/
  struct_get/struct_set/call/invoke/connect/disconnect/enum/is_a/type_name/main/
  quit/timeout/idle/source_remove/watch_fd/watch_mailbox/variant_*` — there is no
  `emit`, no `signal_emit`, and `g_signal_emit_by_name` is not introspectable, so
  `gi.invoke` cannot reach it either. A GTK program written in gBASIC therefore
  has no direct way to test its own handlers: you can `gi.connect` a callback but
  you cannot make it fire.
- **Workaround:** synthesise the signal through an introspected method that emits
  it as a documented side effect. Verified against GTK 4.22:
  - `gi.call(row, "activate")` → `row-activated` on the parent GtkListBox — sync
  - `gi.call(listbox, "select_row", row)` → `row-selected` — sync
  - `gi.call(notebook, "set_current_page", n)` → `switch-page` — sync
  - `buffer.text = s` (GtkTextBuffer) → `changed` — sync, and fires **twice**
    (delete then insert); the first fire sees an EMPTY buffer, so a handler that
    mirrors buffer text into a model must tolerate a transient "".
  - `gi.call(button, "activate")` → `clicked` **asynchronously**, ~250 ms later
    via GtkButton's own activate timeout. It does *not* fire inline, so a test
    that clicks a button has to give the main loop back (`gi.timeout`) rather
    than asserting on the next line.
  Also verified: rebuilding a GtkListBox (remove every row, re-append) from
  inside that same listbox's `row-activated` handler is safe under
  `G_DEBUG=fatal-criticals` — no criticals, and the box stays usable.
  This is enough to test input end-to-end and no language change was needed, but
  a first-class `gi.emit(obj, signal, args...)` would make it direct rather than
  a per-widget scavenger hunt, and would cover signals no method happens to emit.
  Used by gbasic-studio `tests/run_studio.sh` (`ui_gui`, `ui_gui_cold`).

## 2026-08-01 — CC — while: Studio STU-2D (building a list of strings)
- **Type:** language-surprise
- **Severity:** low
- **What:** `+` concatenates strings and adds numbers, but on two arrays it raises
  `arithmetic operator '+' expected number but got array`. Splitting a long list
  across statements the obvious way (`a = ["x"]` then `a = a + ["y"]`) therefore
  fails at run time, not at parse time. There is no `concat`/`extend` builtin
  either — `append(arr, value)` adds ONE element, so joining two arrays means a
  loop.
- **Workaround:** a single array literal spanning several physical lines, which
  *does* parse (`a = ["x",\n "y",\n "z"]` works). Good outcome for this case, but
  a list assembled conditionally still needs a `for each ... append` loop. An
  array `+` (or a `concat(a, b)`) would remove it; UNLEARN.md's operator section
  says `+` concatenates "when either operand is a string" and is silent on
  arrays, so the failure is only discoverable by hitting it.

## 2026-08-01 — CC — while: Studio STU-2D (two windows at once)
- **Type:** doc-gap
- **Severity:** medium
- **What:** `gtk.application(app_id)` builds a `Gtk.Application` with default flags,
  which is SINGLE-INSTANCE. A second process running the same program therefore
  does not open a second window: it registers as a remote, forwards "activate" to
  the first process and exits, having printed nothing. Worse for the first
  process — it receives an extra "activate", so an app that builds its window in
  that handler builds a SECOND one over the same globals and ends up with two of
  every signal handler. In Studio this surfaced as a display test that
  occasionally saw one synthesised click land twice, which took a while to trace
  back to a stdlib default. `gtk.bas` documents no flags parameter and says
  nothing about instance semantics.
- **Workaround:** build the application by hand, which `gtk.bas`'s header already
  sanctions ("callers drop straight down to the raw gi bridge for anything not
  wrapped here"):
  `solo = gi.enum("Gio.ApplicationFlags.NON_UNIQUE")` then
  `gi.new("Gtk.Application", "application-id", id, "flags", solo)`
  (with `gi.require("Gio", "2.0")` first). Either a `flags` parameter on
  `gtk.application` or one sentence in its comment naming the default would have
  saved the hunt — the failure mode is silent and acts at a distance.

## 2026-08-01 — CC — while: ARI Phase 2 (locating a literal anchor in a report line)

- **Type:** language-surprise
- **Severity:** medium
- **What:** `find` and `match` are the literal and regex halves of the same
  operation, and they report a miss **differently**:

  ```basic
  print type(find("abc", "z"))     ' nothing
  print type(match("abc", "z"))    ' unknown
  ```

  `match` returning `unknown` is deliberate — it is the NA policy, and
  `is_unknown` is the documented miss test (docs/text_design.md §3). But `find`
  predates that policy and returns `nothing`, so the obvious guard silently does
  not fire:

  ```basic
  idx = find(hay, needle)
  if is_unknown(idx) then return unknown end if   ' never true on a miss
  r = idx + 1                                     ' arithmetic operator '+' expected number but got nothing
  ```

  The failure surfaces a few frames away from its cause, in whatever arithmetic
  first touches the null. Adding regex to the core made this worse, not better:
  there are now two closely-related builtins with two miss conventions, and
  nothing warns you which one you are holding.
- **Workaround:** check both in any helper that can take either path
  (`is_nothing(idx)` then `is_unknown(idx)`), so callers see one miss value.
  See `stdlib/ari.bas` `_find_token`. A real fix would be to decide one
  convention for miss across the string builtins; changing `find` is a breaking
  change and would need its own phase.

## 2026-08-01 — CC — while: ARI Phase 2 (printing parsed money amounts)

- **Type:** bug
- **Severity:** high
- **What:** `print` and `string()` render numbers with about **6 significant
  digits**, so any amount from $10,000.00 up silently loses its cents:

  ```basic
  v = 13586.25
  print v            ' 13586.2    <- a cent has vanished
  print string(v)    ' 13586.2
  print v * 100      ' 1358625    <- the VALUE is fine
  print v = 13586.25 ' true
  ```

  The stored value is exact; only the rendering is lossy. That distinction is
  cold comfort in a reporting library, where the rendering *is* the product — a
  teller totals report printed through gBASIC would be wrong on every line over
  $9,999.99, and wrong quietly. It also makes golden tests misleading: a golden
  captured from `print` cannot distinguish 13586.25 from 13586.20.
- **Workaround:** in `examples/ari_teller_test.bas`, money is displayed as
  integer cents (`v * 100`) so the golden proves what was parsed. That is a test
  device, not a fix. Previously noted as a "%g number-display precision" gap
  during the bitwise work; recording it again with a concrete cost, because it
  is materially worse for money than for the bit patterns that first surfaced it.

## 2026-08-01 — CC — while: ARI Phase 2 (converting a parsed date to a value)

- **Type:** missing-feature
- **Severity:** medium
- **What:** gBASIC has a `datetime` value kind, but no way to **construct one
  from year/month/day at run time**. There is no `date(y, m, d)` builtin, and no
  date literal I could find — `d = 2024-03-15` parses as arithmetic and yields
  the number 2006. The only constructors are `now()` and `from_epoch(secs)`, and
  `from_epoch` applies a timezone shift:

  ```basic
  print from_epoch(1710460800)    ' 2024-03-14 20:00:00  (asked for the 15th UTC)
  ```

  So a library that parses `27/12/2021` out of a report cannot turn it into a
  native date without either computing an epoch and then fighting the timezone
  back off it, or inventing a timezone the source document never specified.
- **Workaround:** `ari`'s `as date` yields a **normalized ISO string**
  (`"2021-12-27"`) rather than a `datetime`. That is deterministic, timezone-
  free, lexically sortable and comparable, and it round-trips — but it is not
  what docs/text_design.md §4 promised ("type keywords map to native gBASIC
  values"), so the design is now ahead of the language on this point. A
  timezone-free `date(y, m, d)` constructor would close it.

## 2026-08-01 — CC — while: scoping ARI Phase 3 (native value conversion)

- **Type:** doc-gap
- **Severity:** low
- **Status update / CORRECTION** to the entry above, "*while: ARI Phase 2
  (converting a parsed date to a value)*", which claimed gBASIC has **no way to
  construct a date at run time**. **That was wrong.** Dates and money are built
  with **assign modifiers**, not functions or literals:

  ```basic
  m(USD) = 12.34            ' money
  d(date) = "2021-12-27"    ' datetime
  s = "2019-03-04"
  e(date) = s               ' works from a runtime string too
  ```

  Both verified. The `date` modifier takes an ISO-like string and is
  timezone-free, so it is exactly the constructor the earlier entry said did not
  exist. `from_epoch` and its timezone shift were never the only route.
- **What actually went wrong:** I searched for a `date(...)` builtin and a date
  *literal*, found neither, and concluded there was no constructor — without
  checking the modifier surface, which is where gBASIC puts typed construction.
  The real gap is a **documentation** one: `docs/reference.md` describes money
  and datetime values but does not show how to make one, and nothing in
  `docs/ai/COOKBOOK.md` covers it either. That is why a fairly careful search
  missed it, and it will keep costing whoever looks next.
- **Workaround:** none needed. ARI Phase 3 can convert to native values. The
  ISO-string intermediate `as date` already produces turns out to be precisely
  the input the `date` modifier wants, so the two compose.

## 2026-08-01 — CC — while: ARI Phase 3 (native money conversion)

- **Type:** doc-gap
- **Severity:** low
- **Status note** on the entry above, "*while: ARI Phase 2 (printing parsed
  money amounts)*", which recorded that `print` loses cents above $9,999.99.
  That entry stands for plain NUMBERS — but a **native money value prints
  correctly**:

  ```basic
  v = 13586.25       : print v      ' 13586.2    <- number, cent lost
  m(USD) = 13586.25  : print m      ' 13586.25   <- money, exact
  ```

  So the mitigation for anything monetary is to hold it as money rather than as
  a number, which is what `ari`'s `as money` now does. Both ARI goldens dropped
  their integer-cents workaround as a result and show amounts as a reader would.
  The underlying `%g` behaviour for plain numbers is unchanged and still wrong
  for financial work that has not converted.

## 2026-08-03 — CC — while: scanning the 15,871-workbook Enron corpus for xlsx function usage

- **Type:** missing-feature
- **Severity:** medium
- **What:** `tools/xlsx_function_scan.bas` walks a directory in-process, and
  that shape cannot survive a real corpus. Two independent reasons, both found
  by reading the runtime rather than by the scan blowing up:
  1. `list_files` does not recurse (`src/eval.c`, a plain `opendir`), so a
     nested corpus is silently under-scanned rather than refused.
  2. **`xlsx.open` raises, and gBASIC cannot catch a raise.** One malformed
     workbook in thousands aborts the entire scan and yields no data at all.
     The scanner's own comment claimed a "pre-validation" guard, but no such
     guard was ever in the code — the comment described an intent.

  This is the same shape that produced `try_decode` (PLAT-JSON): an operation
  that must report failure as a *value* because there is no way to recover from
  a raise. A batch tool over untrusted inputs needs that, and `xlsx.open` is
  exactly such an operation.
- **Workaround:** drove the scan from a shell loop, **one `gbasic` process per
  workbook**, so a raise costs exactly that file and the exit status identifies
  it. This turned out to be a benefit as well as a workaround — the failure list
  *is* the robustness data, and it is what located the partless-sheet bug (400
  files, 2.5%, all 400 failures the same cause). 14-way parallel, ~4 minutes for
  15,871 files. Not a substitute for a fix: the natural in-process idiom is
  still unavailable, and the eventual answer is probably an `xlsx.try_open`
  returning `{ok, workbook, message}` alongside the raising form, mirroring
  `try_decode`/`decode`.
- **Related, and genuinely fixed here:** the corpus also showed
  `xlsx.sheets`/`xlsx.cells` contradicting each other on macro sheets. That one
  was a real defect and is fixed, with the measurement recorded in
  `docs/xlsx_design.md` §13.I.
