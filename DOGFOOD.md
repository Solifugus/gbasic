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

## The open ledger

The log below is APPEND-ONLY and long on purpose — it is the record of what
building on gBASIC actually cost, and the resolved entries are where the
lessons live. This section is the part you act on: every currently-open item,
one line each. Maintenance rule: filing a new OPEN entry adds a line here;
resolving one strikes it and adds a RESOLVED note at the entry. Entries not
listed here are resolved or absorbed into doctrine (UNLEARN/ERRORS/reference),
and the stale-looking ones carry a Status line saying what overtook them.

### Open — worth fixing (ranked)

1. ~~**No modulo**~~ **RESOLVED 2026-08-24.** `mod(a, b)`, FLOORED — which is
   what the hand workaround `a - floor(a/b)*b` computed, so the four libraries
   written against that advice keep their meaning. Diverges from QBasic's
   truncating `MOD` deliberately and loudly (UNLEARN, reference). `%` remains
   a separate decision.
2. ~~**`encode` emits bare `nan`/`inf` that its own `decode` rejects**~~
   **RESOLVED 2026-08-24.** `decode` accepts the four spellings `encode` emits
   (`inf`, `-inf`, `nan`, `-nan`), gated on the DIALECT parser — the wire
   parser stays RFC 8259 and refuses them, proved end to end by posting each to
   a running server. Taking the "accept" branch over the "refuse" branch
   because the dialect exists to round-trip and `serialize` already round-trips
   these; the strict encoder is the one that must refuse. Also: `-inf` decoded
   correctly the whole time by accident (strtod parses it), which is what makes
   this a bug and not a policy. `examples/encode_roundtrip_test.bas`.
3. ~~**No array concat**~~ **RESOLVED 2026-08-24.** `concat(a, b, …)`,
   variadic. Array `+` remains a separate decision.
4. ~~**`process.start` children outlive a SIGKILLed parent**~~ **RESOLVED
   2026-08-24.** The decision this was waiting on had already been made and
   written down: `docs/reference.md` says in bold that nothing the interpreter
   starts outlives it. So there is no deliberately-detached child to protect —
   an opt-in would have been a documented way to break a documented promise —
   and the only question was that the promise was enforced in a userspace
   teardown pass a SIGKILL skips. All three fork sites now arm PDEATHSIG between
   fork and exec. `tests/run_process_lifetime.sh`.
5. ~~**`xlsx.open` raises; batch tools cannot survive one bad workbook**~~
   **RESOLVED 2026-08-25.** `xlsx.try_open(path)` → `{ok, workbook, message}`,
   sharing ONE code path with `open` so the two cannot disagree about what is
   readable. The `list_files` half was a DOC gap, not a missing feature: the
   walk was always writable, just not on `list_files` — that one reports files
   only and cannot see a subdirectory at all, while `list(d)` on a `{dir}`
   reference answers `{name, type}` for every entry. Neither was documented in
   reference.md; both are now. `examples/xlsx_try_open_test.bas`.
6. ~~**Parse errors of the `dim x` class print to stderr and exit 0**~~
   **RESOLVED 2026-08-24.** Larger than filed: `yylex` hands bison a synthetic
   end-of-file for a token it cannot map, and wherever the grammar allows a
   program to end, bison ACCEPTS — so a top-level file was silently truncated
   at the bad token, the statements before it RAN, and the exit was 0. Not
   `dim`-specific (any unlexable byte did it) and not merely cosmetic (under
   `--json-diagnostics` the bare line was non-JSON in a JSON stream).
   `tests/run_parse_exit.sh`.
7. ~~**Doc-gaps, each cheap:**~~ **RESOLVED 2026-08-25.** All three written
   into `docs/reference.md`. The first turned out to be the smallest part of a
   bigger problem: the Modifiers section was stale from rc6 in three separate
   ways — it still described the paren spelling as merely deprecated, claimed
   parenthesized ASSIGNMENT modifiers were not deprecated (they were removed),
   and said modifiers do not apply to call results (they do). Every claim there
   is now checked against a running program.
8. ~~**No PBKDF2/scrypt in `crypto`**~~ **RESOLVED 2026-08-25.**
   `pbkdf2_sha256` / `pbkdf2_sha512` (RFC 8018) and `scrypt` (RFC 7914),
   returning raw key bytes. Verified against INDEPENDENT implementations
   (python3 `hashlib`, RFC 7914 §12), never against gBASIC — a KDF that agrees
   only with itself proves nothing, since a shared bug still round-trips and the
   key is simply weak. An empty salt is REFUSED; the cost parameters are not
   floored, because the published test vectors use deliberately tiny ones and a
   floor would make the implementation untestable against the vectors that prove
   it correct.
9. ~~**`crypto.json_encode` remains a separate flat encoder**~~ **RESOLVED
    2026-08-25.** Removed. Once `json_encode` became a builtin the library's copy
    was unreachable by the natural call — `json_encode(x)` resolved to the
    builtin, and the runtime warned on every `load crypto` that it was doing so.
    The DECODER stays and the reason is not symmetry: it reads attacker-supplied
    token payloads and accepts RFC 8259 only, where `try_decode` speaks the
    permissive dialect. Found while folding: that decoder **raised** on malformed
    input instead of refusing it — a denial of service on hostile input, not a
    rejection. `examples/crypto_json_hostile_test.bas`.
10. ~~**No record merge**~~ **RESOLVED 2026-08-24.** `merge(a, b, …)`,
    variadic, later-wins, shallow. Decided together with item 3 as the ledger
    asked: both are BUILTINS, and `+` on a container stays unanswered.
11. ~~**A discarded return value is silent**~~ **RESOLVED 2026-08-23** by
    PLAT-WARN: `unused-result` warns when a bare call discards a non-`nothing`
    return from a gBASIC function. Shipped as a runtime diagnostic rather than
    the parse-time one proposed here, because the runtime knows the actual
    returned VALUE — which is what lets `return nothing` (the void convention)
    be exempt by value and keeps `append`'s 1101 bare calls quiet.

12. **`money` is exact in storage and lossy at every boundary.** The int64
    cents core is right (0.01 accumulated 1000 times is exactly 10.00, which a
    double cannot do), but nothing can put an exact value INTO it and `*`/`/`
    take it back out through a double. Reported by the gdash session with
    evidence; verified here. See the 2026-08-29 entry for the three defects,
    the guard-digit proposal, and the ruling still needed on currency identity.

### Open — accepted as documented limitations (no action planned)

- A raise cannot be caught; `on error resume next` abandons the whole failing
  statement. Doctrine: pre-validate, and grow `try_*`/probe builtins where a
  failure must be a value (`try_decode`, `process.which`,
  `launch_failure:"result"`, `has_builtin`). ERRORS.md is the model.
- `call(args) = value` misparses as a modifier clause — bind the result first
  (`docs/gbasic_clause_recognition.md` §9).
- Keywords work as record-literal keys but not after a dot (`r.end`) — lexer
  boundary, bracket access reaches them (2026-08-15).
- `.file`/`.date` modifiers do not work postfix in expression position — use
  the assignment form.
- No `gi.emit` — the per-widget signal-synthesis catalogue (2026-07-31) covers
  testing; Studio's display tiers run on it.
- gi cannot call STATIC class functions (`Gtk.StyleContext.add_provider_for_display`)
  — per-instance routes exist (2026-08-22).
- No exponent literal — `1e20` lexes as a duration with a misleading message;
  `number("1e20")` is the idiom.
- `find` misses with `nothing`, `match` with `unknown` — changing `find` is
  breaking; helpers check both.
- `exists` rejects a dir reference; `make_dir` is not idempotent — the
  `ensure_dir` ceremony stands.
- `atomic_replace` gives dest the temp's inode (perms reset, symlinks
  replaced); no `chmod`/`lstat` to compose the safe form — Studio writes
  source files in place because of this.
- No line continuation; multi-line array literals and `join` are the idioms.
- A method call on a call-result receiver as a bare statement does not parse
  (expression position works).
- Library diagnostics print the load path unnormalized (`tests/../stdlib/...`)
  — cosmetic.
- valgrind does not exist for riscv64; ASan runs there with degraded
  diagnostics. A platform fact, stated in release notes rather than fixed.

## Seed entries — D0 audit surprise harvest

The first entries below come from the Phase D0 audit (2026-07-17/18). For
CURRENT truth, the open ledger above supersedes any status line below; several
of these were resolved long after their text was written.

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
- **Status 2026-08-24:** RESOLVED, and the severity here was too low because the
  filed symptom was the small end of it. The cause is that `yylex` signals an
  unmappable token by returning 0 — end of file — which bison cannot tell from a
  real one. Inside a block that is a syntax error, which is why the entry only
  ever saw the exit code; at TOP LEVEL the grammar accepts, so the file was
  truncated at the bad token, everything before it ran, everything after it
  disappeared and the exit was 0. Fixed on both halves: the token map reports
  through the diagnostics sink (located, and `dim` now says to assign instead),
  and `parse_source_reentrant` fails the parse when a diagnostic was reported
  even if bison accepted. `tests/run_parse_exit.sh`. **Lesson:** a bug filed
  from its exit code is filed from its least interesting symptom — the exit code
  was inconsistent, but what it was inconsistent ABOUT was a silently truncated
  program.
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
- **Status 2026-08-22:** RESOLVED by later work — bracket/string-key access (`r["end"]`) landed and reaches any reserved-word field; verified against rc3.
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
- **Status 2026-08-22:** the arity half is RESOLVED — a wrong-arity call is now a located, fatal runtime error (see the 2026-08-22 arity entry). The implicit-`this` half is doctrine (UNLEARN).
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
- **Status 2026-08-22:** RESOLVED for record LITERALS by the 2026-08-12 `field_name` grammar change — `{ error: 1 }` parses; dot access still needs brackets (2026-08-15 entry). Verified against rc3.
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
- **Status 2026-08-24:** the `nan`/`inf` half is RESOLVED, and this entry's own
  framing is what decided which way. Once "round-trip serialization" and
  "interchange serialization" are stated as different concerns, the fix follows:
  the DIALECT exists to round-trip, so `decode` learns the spellings `encode`
  already writes; the STRICT path exists to be standard, so it keeps refusing
  them. The wire parser (`json_only` — webclient and webserver bodies) is on the
  strict side of that line and is untouched, which
  `tests/webserver_client.py` now proves by posting `{"a":inf}` and friends to a
  live server. `crypto.json_encode` remains open (ledger item 9).

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
- **Status 2026-08-22:** RESOLVED — `try_decode` is a core builtin; `studio_json.bas` and `llm`'s private validator were both retired by it.
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
- **Status 2026-08-22:** RESOLVED — `file_type(path)` exists (PLAT-WEB-1 Gap B) and answers file/folder/other without raising; `studio_docs._is_dir`'s parent scan is retired.
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
- **Status 2026-08-22:** the key half is RESOLVED — `{ next: 1 }` parses since the 2026-08-12 grammar change; verified against rc3. `new` as a function name still refused.
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
- **Status 2026-08-22:** RESOLVED by later work — `contains("hello world", "lo w")` now works on strings; verified against rc3. The `find`-miss-is-`nothing` half still stands (see the accepted list).
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
- **Status 2026-08-22:** RESOLVED — `monotonic()` exists and resolves sub-second; verified against rc3. The shell-side timing in run_stridx/arridx predates it and still works, so it was left alone.
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
- **Status 2026-08-24:** RESOLVED. The decision this entry deferred ("arming it
  changes the semantics of any deliberately detached child, so it wants its own
  decision") turned out to be already made, in `docs/reference.md`: *"at program
  exit any still running are killed and reaped, so nothing this interpreter
  started outlives it"* — in bold, written before this entry. There is no
  detached-child use case to weigh, because the language documents that it does
  not have one; an opt-out would be a documented way to break a documented
  promise. What remained was that the promise rested on a userspace teardown pass
  that SIGKILL skips, and a guarantee about surviving a kill can only be made by
  the kernel. All three fork sites now arm PDEATHSIG between fork and exec
  (`proc_arm_parent_death`), which also closes the fork→exec→arm race exactly —
  against a recorded pid rather than the actor path's `getppid() == 1`, which was
  additionally wrong when gbasic is pid 1 in a container. **Lesson:** before
  deferring a decision, check whether the docs already made it.
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
- **Status 2026-08-22:** RESOLVED by PLAT-NUMFMT (2026-08-14) — numbers print shortest-round-trip, so cents survive; money values were always exact. The integer-cents test device is gone.

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

## 2026-08-09 — CC — while: pinning NOW()/TODAY() in a golden test
- **Status 2026-08-22:** the underlying limitation is RESOLVED by PLAT-NUMFMT — a serial now prints in full. The split-assertion technique in the fixture remains good practice and was left in place.

- **Type:** language-surprise
- **Severity:** low
- **What:** an Excel date serial cannot be golden-tested through `print`.
  `print` renders numbers with roughly six significant digits, so the serial
  46237.5674884 (2026-08-03 13:37:11) comes out as `46237.6`. A golden over the
  raw value therefore pins the DAY and almost nothing else — the time of day,
  which is the half most likely to be wrong, is rounded away before it can be
  compared.

  Not the same entry as the money/`%g` one already logged: that was about cents
  being lost from a value someone would read. This is about a test being unable
  to observe a difference the code genuinely computes.
- **Workaround:** assert the two halves separately, each in a range `print` can
  represent exactly — `floor(n)` for the day and
  `floor((n - floor(n)) * 86400 + 0.5)` for the seconds of day. Both are small
  integers, so the golden pins every digit. `tests/xlsx_volatile_test.bas`
  documents the reasoning inline so the next person does not "simplify" it back
  into printing the serial.
- **Also worth knowing:** the underlying computation was verified correct to
  full precision by cross-checking against LibreOffice, which is where the real
  confidence comes from; the split assertion is what makes it *repeatable in
  CI*.

## 2026-08-11 — CC — while: building stdlib/grid.bas (L2, ARI-for-grids)

- **Type:** doc-gap
- **Severity:** medium
- **What:** a library's dependency on another library must be declared INSIDE
  the `library` block, and a top-level `load` silently does not work:

  ```basic
  load frame            ' file top level -- does NOT put frame in scope for
  library grid          ' this library's own functions
      function f()
          return frame.from_rows([])   ' invalid function call: frame.from_rows
      end function
  end library
  ```

  The correct form, as `stats.bas` does over `matrix.bas`:

  ```basic
  library grid
      load frame from "frame.bas"
      ...
  ```

  What made this cost real time is that the broken form **works whenever the
  CALLER also loads the dependency**. My first tests did `load grid` and
  `load frame` in the program, so everything passed; the failure only appeared
  once a test loaded `grid` alone — which is how every real user would call it.
  A library shipped that way would look fine in its own test suite and break in
  the first program that used it.
- **Workaround:** none needed — the in-block form is correct and documented by
  example in `stats.bas`, `forensics.bas` and `screener.bas`. But nothing in
  `docs/reference.md` or `docs/ai/COOKBOOK.md` states the rule, and the failure
  mode (works until the caller stops loading your dependency for you) is not
  one a reader would guess.
- **Also:** `has_key` does not exist; the record key-presence builtin is
  `has(record, key)`. Found by writing the former and reading `builtins.c`.

## 2026-08-12 — CC — while: xlsx L2/L3/L4 and the corpus scans (a batch)

Logged late, in one batch, which is itself the first entry: the house rule says
*append before continuing* and I did not, so several of these were carried in
working memory across days instead of written down. That is exactly how a
finding gets lost.

### (a) Records are a linear-scan association list, and there is no map type
- **Type:** perf
- **Severity:** high
- **What:** indexing data by string key is O(n^2) to build, and nothing says
  so. Measured on this machine:

  | fields | build time |
  |---|---|
  | 2,000 | 28 ms |
  | 4,000 | 91 ms |
  | 8,000 | 333 ms |
  | 16,000 | 744 ms |

  `src/eval.c` looks a field up with `for (size_t i = 0; i < record.count; i++)`.
  I wrote the natural implementation of a sheet index — cells keyed `"r,c"` —
  and it could not run on a real workbook at all: a 182,000-cell sheet is about
  1.6e10 comparisons and the first corpus file tried timed out at 120 s.
- **Workaround:** sparse parallel per-row arrays with a binary search for the
  row (`stdlib/grid.bas`), and ultimately a new C verb `xlsx.grid` returning
  column-oriented arrays so the index is never built in gBASIC at all.
- **Why this is core work, not a library concern:** it caps what anyone can
  build in the language, silently, at a size small enough to miss in testing
  and large enough to matter in use. There is a proven template in this
  codebase: strings (PLAT-STRIDX) and arrays (PLAT-ARRIDX) were both rescued
  from quadratic behaviour by adding an index behind an unchanged API. Records
  are the third member of that family and the only one still unfixed.

### (b) `print` renders about six significant digits
- **Type:** bug
- **Severity:** high (for the domain this is aimed at)
- **What:** every test written this session had to route around it.
  `265550.75` prints as `265551`; `23750.25` as `23750.2`; the Excel date
  serial `46237.5674884` as `46237.6`. A money VALUE prints exactly, but data
  read from a spreadsheet arrives as plain numbers, so the money path does not
  save you.
- **Workaround:** assert by COMPARISON rather than by printed text
  (`print sum(...) = 265550.75`), and for date serials split the value into
  day and seconds-of-day, each small enough to survive. Both are documented
  inline in the tests so nobody "simplifies" them back.
- **Note:** this is separate from the earlier money/`%g` entries. Those were
  about a value a person reads; this is about a test being unable to observe a
  difference the code genuinely computes.

### (c) Reserved words cannot be record keys, and literal keys must be identifiers
- **Type:** language-surprise
- **Severity:** medium
- **What:** the natural vocabulary for a mapping spec collides with the
  keyword namespace. `{ from: [...], as: "money" }` is a parse error, so the
  spec ships as `names:`/`kind:` — worse names, chosen by the grammar rather
  than by meaning. Previously the same thing forced `next_at`, `halt`,
  `message`. Separately, a record LITERAL accepts only identifier keys, so
  `{ "Rate (%)": 1 }` will not parse and the record must be built by
  assignment.
- **Workaround:** rename the fields; build by assignment when a key is not an
  identifier.
- **Analysis, because this one looks cheaper to fix than it seems:** both
  places are CLOSED CONTEXTS in the grammar. `record_field_list`
  (`src/parser.y:1304`) requires `IDENT` immediately before `COLON`/`OP_EQ`,
  and field access is `DOT IDENT`. In both positions nothing but a field name
  is legal, so a `field_name` nonterminal — `IDENT` plus the keyword tokens,
  each yielding its own spelling — would admit `from:` and `rec.from` with no
  quoting on either side.

  What is NOT known, and must be measured rather than assumed: whether that
  introduces LALR conflicts. `DOT IDENT` appears in seven rules including
  method calls, lvalues and watch paths, and the record rule also carries the
  PBI `IDENT (policy):` form. The experiment is cheap — add the nonterminal,
  run bison, count new conflicts — and it should be run before anyone promises
  this.

  Quoted keys would still be wanted for names that are not identifiers at all
  (`"Rate (%)"`), which is what bracket access already handles.

### (d) Small gaps that each cost real time
- **Type:** missing-feature
- **Severity:** low
- **What:** no modulo operator (`i % 6` is a lexer error; written as
  `i - floor(i / 6) * 6`); the record key-presence builtin is `has`, not
  `has_key`, and is discoverable only by reading `src/builtins.c`; still no
  line continuation, which shapes how every multi-line expression is written.

## 2026-08-12 — CC — while: testing whether entry (c) above is cheap to fix

- **Type:** language-surprise
- **Severity:** medium
- **Status update on (c).** The grammar experiment was run rather than argued,
  and it split the problem in two:

  * **Record KEYS: done.** A `field_name` nonterminal (`IDENT` plus the keyword
    tokens, each supplying its own spelling) admits `{ from: 1, next: 2,
    error: 3 }` with **zero new LALR conflicts** and every suite green,
    including the 283 negative tests — which mattered, since turning keywords
    into field names could have swallowed parse errors those tests expect.
  * **DOT access: not the same problem.** `rec.next` still fails. The reason is
    that `a.b` is assembled in the LEXER, which can emit a single
    `QUALIFIED_IDENT`, so admitting keywords after a dot is a lexer change, not
    a grammar one. My earlier analysis said both sides were closed contexts in
    the grammar; that was right about record keys and wrong about dot access,
    and the parse error names `QUALIFIED_IDENT` explicitly.

  Also learned: `as` and `from` are not declared parser tokens at all. The
  lexer emits `TOKEN_AS`, the grammar has no rule for it, and the result is an
  undeclared-token syntax error rather than a keyword clash. So `as:` needs the
  token declaring before it can join the field-name list; `from:` already works
  because `from` lexes as an ordinary identifier.
- **Workaround:** none needed for keys. For a keyword field in dot position,
  bracket access reaches it today.

- **Follow-up, same day:** `as` now works as a field name too. It needed the
  TOKEN DECLARING as well as the grammar rule — the lexer emits it for ARI's
  `as money` conversions, but the parser had no rule for it at all, so it hit
  the token mapper's default arm and was reported as an unexpected token. Zero
  new conflicts; the ARI suite (10 cases, where `as` is load-bearing in the
  modifier syntax) is green, as are the 283 negative tests. `stdlib/consolidate.bas`
  can now be renamed from `names:`/`kind:` to the natural `from:`/`as:`, which
  is left as a separate change rather than mixed into a parser commit.

## 2026-08-13 — CC — while: fixing entry (a) above (records as a map)

### Status update on 2026-08-12 (a): fixed, and it was TWO costs not one
- **Type:** perf
- **Severity:** was high
- **Resolved.** Records now carry a lazily-built hash index from field name to
  slot, and the field array is shared with copy-on-write instead of being
  duplicated on every read. `tests/run_recidx.sh`.
- **The finding worth recording is that the index alone fixed nothing.** With
  the index in and sharing not yet done, building a record by string key went
  from 3.42 s to 0.04 s at 32 000 fields — which looked like the fix, and was
  the only thing my first benchmark measured. But `env_get` calls `value_copy`
  on every read of a variable, and `value_copy` on a record duplicated the whole
  field array, so every READ still cost O(fields) *and threw the freshly built
  index away*. Measured: 300 000 lookups of ONE key against a 1 000-field
  record 0.65 s, against 8 000 fields 8.41 s — a fixed amount of work growing
  with the size of the record. A loop reading n keys stayed quadratic.

  This is written down two hundred lines above the code I was editing, in the
  PLAT-STRIDX comment: *"Sharing and caching are one mechanism, not two: fixing
  either alone leaves a per-character loop quadratic."* Strings needed both.
  Arrays needed both. Records needed both. I measured the write path, saw a
  85x improvement, and nearly stopped.
- **What caught it:** not a test. A mutation test — deliberately breaking the
  index to check the new fixture could go red — did NOT go red, which made no
  sense until a trace showed the record being copied (and its index rebuilt)
  between the write and the read. The test I had written was passing for the
  wrong reason.
- **Lesson for the next one of these:** a per-element cost has a *build* side
  and a *read* side and they are separately quadratic. Benchmark both before
  claiming the class changed. The `tests/run_recidx.sh` shape tiers are split
  along exactly that line — ops that touch every field are gated as linear, ops
  doing FIXED work against a GROWING record are gated as flat — because the flat
  tier is the one that was failing and a suite of linear-only gates would have
  reported the half-fix as done.

### `=` on two records is always true
- **Type:** bug
- **Severity:** medium
- **What:** `{ x: 1 } = { y: 2 }` evaluates to `true`, as does every other pair
  of records. Confirmed against an untouched binary, so it is pre-existing and
  not something the work above introduced. `value_storage_equal` implements
  record comparison correctly (count, then each field by name); the `=`
  operator does not reach it for records.
- **Why it cost me time:** I wrote a test asserting that two records built with
  DIFFERENT keys are unequal. It passed. It would have passed against any
  implementation, including one where the index resolved every name to slot 0.
  A test that cannot fail is worse than no test, and this one looked like the
  strongest check in the file.
- **Workaround:** compare `encode(a) = encode(b)`, which walks the fields in
  slot order and so sees content and order both. Used in
  `tests/recidx_test.bas`, with the reason written inline so nobody
  "simplifies" it back to `=`.
- **Not fixed here:** it is a separate defect in the comparison operator, and
  fixing it will move goldens wherever a program is currently relying on two
  records comparing equal.

### A refcounted value needs to know which pointers are owners
- **Type:** language-surprise
- **Severity:** medium (for anyone editing the runtime)
- **What:** making the record field array refcounted was straightforward except
  for one thing: `Value` is passed around as both an owner and a borrow with
  nothing in the type to say which. My first version detached inside
  `record_find`, on the reasoning that a caller taking a writable pointer must
  own the record. Called on a borrow, that decrements a refcount the caller
  never held, and the real owner plus any legitimate copy are then both holding
  an array whose count is one too low — a double free, which surfaced as the
  webserver dying on its third request.
- **Workaround / resolution:** detach only where ownership is structural —
  `resolve_lvalue_ref`, which by construction walks owned storage, and
  `record_set`, which is handed a record to mutate. That is the same rule
  `array_ensure_unique` already follows, which I should have read first: the
  answer for the third member of the family was written down for the second.
- **Also:** the two real defects here (this one, and an uninitialised `refs` on
  the path that grows an empty record) were both found by valgrind and neither
  produced a wrong VALUE — the suites were green when the first one was live.
  For a refcounting change, a valgrind tier is not an extra; it is the tier.

## 2026-08-14 — CC — while: PLAT-NUMFMT, fixing the six-digit `print`

### Status update on 2026-08-12 (b): fixed
- **Resolved.** `format_number` in `src/eval.c` emitted `%g`, six significant
  digits. It now emits the **shortest decimal that reads back as the same
  double**, so `265550.75` prints as `265550.75`, `23750.25` as `23750.25`, and
  the serial `46237.5674884` in full. The integer branch is untouched: an
  integer-valued double below 2^53 still prints plainly, so ids, epoch seconds
  and bitwise results are unaffected.
- **Why shortest rather than `%.17g`:** both round-trip, but `%.17g` renders
  `0.1` as `0.10000000000000001`. Shortest-round-trip is the only rule that is
  simultaneously lossless and not noisy, and it makes `number(string(x)) = x` a
  property the language can assert about itself — which is the load-bearing
  tier in `tests/run_numfmt.sh`, because unlike a list of expected digit
  strings it cannot rot.
- **Cost, stated plainly:** floating-point error is now VISIBLE.
  `print(0.1 + 0.2)` shows `0.30000000000000004` where it used to show `0.3`.
  That is the honest rendering of a value binary floating point cannot hold, and
  the old format was concealing the difference rather than removing it — but it
  is a real change in what a program's output looks like, and any program
  printing derived floats will look noisier. `round(x, 2)` for display, or a
  money value, both still print tidily.
- **Rebaseline:** 17 goldens moved, all in the same direction (digits that were
  being truncated): 13 stats examples, plus `xlsx_compile`, `xlsx_consolidate`,
  `xlsx_modern` and `insiders_cluster`. Listed here because a golden that moves
  is supposed to be a deliberate, enumerated act.
- **What did NOT change:** the tests that assert by COMPARISON rather than by
  printed text stay that way. The formatter gap was the original reason for the
  idiom, but it is the better assertion regardless — it tests that the pipeline
  preserved a value rather than that the interpreter can display one. The stale
  *justifications* in `CLAUDE.md` and `docs/xlsx_design.md` were updated rather
  than left to become the next six-week-old lie (cf. the PLAT-DEBT 1 rule).

### The oracle tier was vacuous on the first attempt
- **Type:** doc-gap (test-design, really)
- **Severity:** medium
- **What:** the suite's independent tier dumped every rendered number and had
  awk recompute the shortest form of each, requiring agreement. It passed
  against the **unfixed** binary. The reason is circular and worth remembering:
  awk parsed *our own printed text*, and the shortest representation of the
  double that `0.333333` denotes is `0.333333`, so a truncated rendering agrees
  with itself. The tier could detect spurious digits and was structurally
  incapable of detecting the actual defect.
- **Workaround:** the dump now emits a **recipe** (`div 1 3<TAB>0.3333333333333333`)
  and the checker recomputes the value from the recipe in its own arithmetic,
  never reading our number; the two meet only at the comparison. Every recipe is
  one correctly-rounded IEEE primitive, so both languages land on the identical
  double. Proven red at 2590 of 4070 disagreements against the pre-change binary.
- **The general lesson:** an oracle fed the output it is checking is not an
  oracle. It has to reach the expected answer by a path that does not pass
  through the thing under test — the same reason `xlsx.check` uses Excel's
  cached values rather than our own evaluator's.

### `1e20` is not a number — it lexes as a duration
- **Type:** language-surprise
- **Severity:** low
- **What:** gBASIC has no exponent literal. `print(1e20)` fails with
  `unknown duration unit: e20`, and `-1.2345678901234567e-308` fails with
  `unknown duration unit: e` followed by a type error, because the duration
  syntax claims the trailing identifier. Nothing about the message points at
  scientific notation, so it reads as a lexer bug until you know.
- **Workaround:** build the value from text — `number("1e20")`. Used throughout
  the width tier of `tests/numfmt_test.bas`, which needs the extreme doubles and
  cannot write any of them as literals. Documented in `docs/ai/UNLEARN.md`.
- **Note:** low severity because the workaround is one call and extreme
  magnitudes are rare in BASIC-shaped programs. It is logged because the
  diagnostic actively misleads, which is the expensive part, not the gap.

### Found while here, NOT fixed: `encode` and `print` now disagree
- **Type:** language-surprise
- **Severity:** low
- **What:** JSON `encode` renders numbers with `%.17g`, which this change did
  not touch, so the two surfaces now say different things about the same value:
  `print(0.1)` shows `0.1` while `encode({a: 0.1})` shows
  `{"a":0.10000000000000001}`. Both are lossless and both read back correctly —
  this is verbosity, not a wrong answer — but the inconsistency is new, and it
  is visible in anything that persists JSON, `stdlib/persist.bas` included.
- **Workaround:** none needed; the values are correct.
- **Deliberately out of scope:** PLAT-NUMFMT was about a program being unable to
  DISPLAY a value it computed. `encode` never had that problem. Fixing it means
  pointing `format_number` at the JSON writer and rebaselining every JSON
  golden, which is a separate, enumerable act rather than a rider on this one.
  Worth doing — every mainstream JSON emitter uses shortest-round-trip, and it
  would shrink persisted files — but as its own change.
- **Note:** `pg_parameter_text` also uses `%.17g` and should STAY that way. It
  is a wire format where maximum precision is the point and nobody reads it.

## 2026-08-14 — CC — while: scoping the next core-language item

### `=` on two compound values is unconditionally true — and it is not just records
- **Type:** bug
- **Severity:** high
- **What:** the 2026-08-13 note recorded this as "records compare equal". The
  real rule is broader and the cause is one line. `eval_comparison`'s final
  `else` (src/eval.c:22495) coerces BOTH sides with `value_number_or_zero` and
  compares as numbers. A record coerces to 0, an array coerces to 0, and a
  non-numeric string coerces to 0, so:

      {x:1} = {y:2}       -> true
      [1,2] = [3,4,5]     -> true
      []    = [1,2,3]     -> true
      [1,2] = "hi"        -> true      (array 0, string 0)
      {x:1} = 5           -> false     (accidentally right: 0 != 5)
      {x:1} = unknown     -> false     (caught by the earlier NULL/UNKNOWN arm)

  Ordering operators are the same coercion, so `{a:1} > {a:2}` is a silent
  false rather than a refusal.
- **The damaging consequence is not the operator, it is the search builtins.**
  `contains` and `find` route through `values_equal` -> `eval_comparison`, so a
  search through an array of records matches the FIRST element and reports
  success:

      people = [{name:"ann"}, {name:"bob"}]
      contains(people, {name:"bob"})   -> true    (right answer, wrong reason)
      contains(people, {name:"zed"})   -> true    (absent value "found")
      find(people, {name:"bob"})       -> 0       (index of ann)

  An array of records is exactly how every frame in the xlsx pipeline is
  represented, so this is live in the code we have been building on. `unique` is
  the honourable exception — it RAISES "unique supports only scalar array
  values" rather than guessing, which is what the others should have done.
- **Workaround:** none in use, because nothing in the suites depended on it —
  which is precisely why it survived. Compare records field by field yourself,
  or search with an explicit loop on a key (`p.name = "bob"`).
- **The fix is not far away:** `value_storage_equal` (src/eval.c:3382) ALREADY
  implements correct deep comparison for arrays and records, recursing properly,
  and is what watchers use to decide whether a value changed. `=`/`!=` need to
  reach it for compound kinds; the ordering operators should raise on compound
  operands rather than coerce, the way `unique` already does.
- **Note on evidence:** no golden can catch this class, because a golden records
  whatever answer the binary gives as the expected one. It is the third defect
  in a row of that shape (PLAT-RECIDX's cost, PLAT-NUMFMT's truncation, this),
  and all three were found by asking a question no test was asking rather than
  by a test going red.

## 2026-08-14 — CC — while: PLAT-EQ, fixing compound comparison

### Status update on the entry above: fixed
- **Resolved.** `eval_comparison` now has branches for arrays and records. `=`
  and `!=` delegate to `value_storage_equal`, which already implemented correct
  deep comparison and is what watchers use — so the operator and "did this
  change?" can no longer disagree. Ordering operators refuse compound operands
  (`arrays support only = and !=`), following the idiom the same function
  already used for functions, regexes, gobjects and boxed values. Record
  comparison is order-insensitive, since a record is a mapping and field order
  is how it was written rather than what it means.
- **What this fixed beyond the operator:** `contains`, `find` and `remove_value`
  became correct because they route through `values_equal`, and `consider` can
  now dispatch on a record's shape — which is a capability, not just a repair.
  It previously took its first branch whatever the subject was.
- **Zero regressions, zero golden movement** across every suite. Worth stating
  plainly rather than as reassurance: nothing in 215 examples, 287 negative
  tests or any module suite depended on the broken answer. That is precisely why
  it survived this long, and it is the argument for why a change this deep in a
  core value type was nonetheless safe to make.

### The tier that could not go red is the interesting one
- **Type:** doc-gap (test-design)
- **Severity:** medium
- **What:** every tier of `tests/run_equality.sh` was proven red against the
  pre-change binary except valgrind, which passed. That is not a weakness in the
  valgrind tier — it is the defining property of this defect class. The old code
  was memory-correct, crash-free, and produced a wrong answer. No sanitiser, no
  fuzzer over memory safety, and above all **no golden** could have caught it,
  because a golden records whatever the binary answers as the expected output.
  A golden written a month ago would have pinned `{x:1} = {y:2}` → `true` and
  actively defended it.
- **Workaround:** the fixture is self-checking — every line states its own
  expected answer and prints `ok` or a MISMATCH naming both sides — and every
  pair is additionally checked against a deep equality written in gBASIC, so the
  C comparison has an independent second opinion that shares no code with it.
- **The pattern across three fixes now:** PLAT-RECIDX (cost), PLAT-NUMFMT
  (truncation) and PLAT-EQ (wrong answer) were all found by asking a question no
  test was asking, never by a test going red. The suites are good at defending
  what they already assert and blind to what nobody thought to assert. Worth a
  deliberate sweep at some point for other "what does nothing ask?" questions,
  rather than waiting to trip over the next one.

### Found while here, NOT fixed: `print` renders strings inside an array as `?`
- **Type:** bug
- **Severity:** medium
- **What:** `print(["a", "b"])` outputs `[?, ?]`, and `print([1, "two", true,
  unknown])` outputs `[1, ?, ?, ?]` — only numbers survive. `string()` on the
  same array is correct (`["a"]`), so `print` and `string` disagree about how an
  array renders, which also means the obvious workaround
  (`print(string(arr))`) works.
- **Workaround:** `print(string(arr))`, or `join(arr, ", ")` for a flat list of
  strings.
- **Note:** pre-existing and unrelated to PLAT-EQ — found while checking what
  `keys()` returns, since `print(keys(rec))` shows `[?, ?]` and looks at first
  like `keys` is broken when it is fine. That misdirection is the expensive
  part. Not fixed here because it is a separate surface (the array-element
  formatter, not the comparison) and deserves its own change and golden pass.

## 2026-08-14 — CC — while: PLAT-RENDER, unifying print with string()

### Status update on the `[?, ?]` entry above: fixed, and it was bigger than reported
- **Resolved.** `print` had its own renderer. It understood numbers inside an
  array and nothing else, so beyond `["a","b"]` → `[?, ?]` the real headline was
  that **a record could not be displayed at all** — `print({a:1})` emitted the
  literal word `{record}`. `print` now delegates to `builtin_string_value`, and
  `tests/run_render.sh` requires `print v` and `print string(v)` to stay
  byte-identical across 58 value shapes.
- **The file already knew the argument.** The comment above `value_print_to`
  explains that `print` and `print to error` share one renderer precisely so
  they cannot drift apart on some value shape. That reasoning is right, and it
  had simply not been applied one level out. Worth remembering as a review
  question: when a comment justifies deduplicating two things, check whether a
  third belongs in the set.
- **Two goldens were defending the bug.** `examples/record_helpers_test.out` and
  `examples/conversion_builtin_test.out` contained `[?, ?]` and `{record}` as
  expected output, and `tests/native_platform/plat_stderr_parity_child.bas` had
  a comment reading "non-numeric (renders as ?)" — the defect documented as a
  feature. This is the golden failure mode from the PLAT-EQ entry, caught in the
  wild twice in one day.

### Delegating naively would have made `print` able to kill the program
- **Type:** bug (avoided)
- **Severity:** high, had it shipped
- **What:** `string()` renders compound values through the JSON encoder, which
  legitimately refuses anything with no JSON form. So `string([aDate])`,
  `string([aFunction])` and `string({when: aDate})` all **raised**. Pointing
  `print` at that would have converted a display operation into a
  program-ending error for values that display perfectly well.
- **Workaround:** the walker gained a third mode. `RENDER_JSON` (json_encode,
  strict), `RENDER_ENCODE` (encode — lenient about `unknown`, which `decode`
  reads back, but still refusing typed values), and `RENDER_DISPLAY`, which is
  **total**: for a typed or live kind it recurses into the single-value
  renderer, which has a text form for every one. No cycle, because that renderer
  only re-enters the walker for arrays and records.
- **The principle worth keeping:** displaying a value must never be able to fail.
  Encoding one may — `encode` refusing a date is correct, because a lossy token
  would produce text `decode` cannot read back. Those are different jobs and it
  was the shared implementation, not the shared intent, that conflated them.

### Durations had no text form at all
- **Type:** missing-feature
- **Severity:** medium
- **What:** `print(2 days 3 hours)` and `string(2 days 3 hours)` both produced
  the literal `{duration}`. Coherent between the two renderers, and useless in
  both — after the unification it was the only value kind a program could hold
  and not display.
- **Workaround:** resolved. Renders in the words the literal syntax uses, so
  what you read back is what you would write: `2 days 3 hours`, `1 day`
  (singular at 1), `0 seconds` for an empty duration rather than an empty line,
  which would be indistinguishable from a failure to print.

### A PLAT-NUMFMT golden was missed because its suite is display-gated
- **Type:** doc-gap (process)
- **Severity:** medium
- **What:** `tests/native_platform/boxed_struct.out` should have been
  rebaselined by PLAT-NUMFMT and was not. `run_native_platform.sh` is gated on
  GTK4 typelibs plus a display, so it was not in that day's sweep, and the
  failure only surfaced today. The value is honest — a `GdkRGBA` component is a
  C `float`, so 0.9 stored at single precision and widened back reads
  `0.8999999761581421`, which `%g` had been rounding away — but the point is
  that a whole suite silently sat outside the verification.
- **Workaround:** rebaselined, with the float32 explanation recorded in the
  fixture so the next reader does not file it as a bug.
- **Note:** the display-gated suites (`run_native_platform`, `run_native_editor`,
  `run_gtkui`, `run_datagrid`, `run_gi`) are the blind spot for any change to a
  shared renderer. This machine HAS a display, so there is no excuse for
  skipping them; they just are not in the reflexive list.

## 2026-08-14 — CC — while: the nested-number inconsistency PLAT-RENDER shipped

### I shipped a defect and the parity tier could not see it
- **Type:** bug
- **Severity:** medium
- **What:** an hour after PLAT-RENDER went out, `print(0.1)` gave `0.1` and
  `print([0.1])` gave `[0.10000000000000001]`. One value, two renderings,
  decided by whether it sat inside a container. Numbers nested in an array or
  record go through the shared encode/display walker, which had its own
  `snprintf(..., "%.17g", ...)` that PLAT-NUMFMT never touched — the unification
  made that second formatter reachable from `print` for the first time, so a
  pre-existing inconsistency became a visible one.
- **Workaround:** resolved. The walker now calls `format_number`, the same
  function `print` and `string()` use on a bare number.
- **THE TEST-DESIGN LESSON, which is the reason this entry exists.** PLAT-RENDER's
  headline tier asserts that `print v` and `print string(v)` are byte-identical
  across every value shape. It passed. It *had* to pass: both paths reach the
  same walker, so both were wrong in exactly the same way. **A parity tier proves
  agreement, not correctness** — the two things it compares can be two faces of
  one mistake. It is the same failure as PLAT-NUMFMT's first oracle, which fed
  awk our own printed text, and I wrote that lesson down the same day and then
  walked into the neighbouring version of it.
- **The other half was fixture coverage.** `render_parity_test.bas` had nested
  INTEGERS (`[1,2,3]`) and a top-level fraction, but no nested fraction — and
  integers render identically under any formatter, so the one shape that would
  have exposed it was the one missing. Nothing in 215 examples or 288 negative
  tests had a nested fraction either: the fix moved ZERO goldens, which is not
  reassurance but a measure of the hole.
- **Now asserted as an invariant, not as digits:** `string([v])` must equal
  `"[" + string(v) + "]"` over a generated battery, so it holds for values
  nobody listed. Proven red against the binary I had pushed an hour earlier.

### `encode` and `json_encode` now use shortest-round-trip too
- **Resolved** (this was the standing item from the PLAT-NUMFMT entry).
  `encode({a: 0.1})` was `{"a":0.10000000000000001}` and is now `{"a":0.1}`.
  Both forms round-trip exactly — shortest-round-trip IS the guarantee `%.17g`
  was there to provide — so nothing is lost, stored JSON gets smaller and
  readable, and it matches what every mainstream JSON emitter produces. A
  360-value `decode(encode(v)) = v` battery asserts the losslessness directly
  rather than trusting the argument.
- **Deliberately NOT changed:** `pg_parameter_text` and the PostgreSQL JSON
  parameter builder keep `%.17g`. Those are wire formats nobody reads, where
  maximum precision is the safe default and there is no consistency argument to
  serve. Noted at both sites so the next person does not "finish the job".

## 2026-08-15 — CC — while: returning to the xlsx track

### A keyword can be a record LITERAL key but still not follow a dot
- **Type:** language-surprise
- **Severity:** low
- **What:** the 2026-08-13 parser change made reserved words legal as record
  field names, and it half-landed in a way worth knowing. `{ as: 1 }` parses;
  `r.as` does not — `syntax error, unexpected AS, expecting IDENT or
  QUALIFIED_IDENT`. Measured across a sample: `from`, `kind`, `names`, `of` and
  `by` work in BOTH positions, while `as`, `to`, `in` and `end` work only as
  literal keys. So a field can be created and never read with dot notation.
- **Workaround:** `r["as"]`, which always works. Or pick a name in the
  both-positions set.
- **Why the split:** the two positions are handled in different places. A record
  literal key is a grammar production that now admits keyword tokens, but
  `r.as` is resolved in the LEXER, which emits `QUALIFIED_IDENT` for `ident.ident`
  and does not do so when the tail is a keyword token. This is exactly the
  distinction noted when the change was made — dot access is lexer-level, not
  grammar-level — so this is a known boundary rather than a regression. Logged
  because the asymmetry is invisible until you hit it, and the error names the
  keyword rather than explaining that the literal form would have worked.
- **Consequence today:** `stdlib/consolidate.bas` was renamed from `names:` to
  the natural `from:`, which now works in both positions. The type field stays
  `kind:` rather than becoming `as:` for exactly this reason — `rule.as` would
  not compile, forcing `rule["as"]` throughout the library — and `kind` says
  more than `as` anyway. `names:` is still accepted, with one call site in
  examples/xlsx_consolidate_test.bas deliberately left on it so the
  compatibility path stays exercised rather than merely claimed.

### The oracle could count what it refused but not say what it was
- **Type:** missing-feature (tooling)
- **Severity:** medium
- **What:** `xlsx.check` reports an `unsupported` count — 3.44M cells across the
  corpus, four times the 872K disagreements — but its per-cell notes carried
  `ref`, `verdict`, `formula`, `computed` and `cached` and *not the name of the
  function it refused*. The evaluator knew: it fills a buffer with exactly that
  name. It simply was not surfaced. So the only way to rank 3.44M cells was to
  count `NAME(` tokens in the formula text, which is the method §13.J already
  showed to be structurally blind — a formula usually holds several functions
  and only one of them is the blocker, and following that ranking misdirected
  the roadmap twice.
- **Workaround:** resolved. Notes now carry `blocked_by`, the name actually
  refused, empty for other verdicts. `tools/xlsx_corpus_blockers.{bas,sh}` rank
  it across a corpus, so the oracle now ranks its own roadmap instead of a proxy
  for it.
- **Note:** the fix is six lines. It sat unnoticed because the count alone
  *looked* like enough information — a number with no breakdown reads as a
  measurement, and it took wanting to act on it to notice it could not be acted
  on.

## 2026-08-15 — CC — while: implementing the TEXT and MATH families

### The roadmap was ranked by a proxy for two months, and the proxy was wrong
- **Type:** doc-gap (methodology)
- **Severity:** high (in wasted direction, not in broken code)
- **What:** the next task was going to be compiler phase 2 — `VLOOKUP`→join and
  `SUMIF`→filtered aggregate — because that is what §14's roadmap says comes
  next. Ranking the corpus by the function the evaluator ACTUALLY refused put
  those at roughly 14,000 cells, and put **FIND at 240,587 and LEFT at 207,757**,
  with LN (48,767), EXP (38,315), SQRT (29,312), HOUR (27,518) and MID (26,896)
  behind them. Ordinary text and math functions, never written, blocking more
  than an order of magnitude more cells than the planned work.
- **Why it stayed hidden:** §13.J had already established that counting `NAME(`
  tokens in formula text is structurally blind, and then that remained the only
  available method, because `xlsx.check` reported an `unsupported` COUNT without
  the name attached. The lesson was learned and the instrument was not fixed, so
  the same blindness kept operating for months under a documented warning.
- **Workaround:** notes now carry `blocked_by`; the corpus ranks itself.
- **The general shape:** a known-flawed measurement will keep steering as long
  as it is the only one available. Recording that a method is unreliable does
  not stop it being used — replacing it does.

### `_xlfn.CEILING.MATH` is not an alias for `CEILING`
- **Type:** language-surprise (Excel's, not gBASIC's)
- **Severity:** medium
- **What:** LibreOffice rewrites a plain `CEILING(2.1,0.5)` into
  `_xlfn.CEILING.MATH(2.1,0.5)` on export, so a reader that implements only the
  legacy names refuses a formula the user typed as `CEILING`. They are not the
  same function: `.MATH` makes significance OPTIONAL and adds a third `mode`
  argument that decides how negatives round — `CEILING.MATH(-2.1)` is `-2`
  (toward zero) by default and `-3` with a non-zero mode. Aliasing them would be
  right on every positive input and wrong on negatives.
- **Workaround:** implemented separately, with the mirror relationship written
  as one condition so the two cannot drift.
- **AND THE ORACLE FAILED HERE:** LibreOffice emits `_xlfn.CEILING.MATH` and
  then **cannot evaluate its own output** — it caches `#VALUE!`. So for exactly
  the cases where the two functions differ, the independent implementation has
  no answer. Those cases are excluded from the fixture rather than pinned to an
  artifact, and the negative-mode behaviour is implemented from Microsoft's
  documentation with **no second opinion behind it**. Stated plainly because it
  is the one part of this work that is not independently validated.

### A concurrent `make clean` made the corpus report 7,797 read failures
- **Type:** doc-gap (methodology)
- **Severity:** medium
- **What:** the post-change corpus run reported 7,797 read errors out of 15,871
  against zero before — which reads as a catastrophic regression in the reader.
  It was not. `tests/run_examples.sh` begins with `make clean && make`, and it
  was running in the same tree while 20 scan processes were exec'ing `./gbasic`.
  The scan was recording ENOENT and partial-image failures, indistinguishable in
  its output from a reader that had started crashing on real workbooks.
- **Workaround:** the scanner copies the binary and runs the frozen copy, so
  nothing happening in the tree during a tens-of-minutes scan can affect it.
- **Note:** worth recording because the false signal was far more alarming than
  any true one so far, and the first instinct — that the new functions had
  broken the reader — was wrong. Re-running a single "failing" file passed
  immediately, which is what identified it. A measurement harness that shares
  mutable state with a build is not measuring the thing it names.

### `IF` propagated an error from the branch it did NOT take
- **Type:** bug
- **Severity:** high
- **What:** Excel evaluates only the chosen branch of an `IF`. This evaluator
  evaluates all arguments first and then dispatches, and `IF` was not in the
  error-catching list — so an error produced by the branch it did not take
  propagated and failed the cell. `IFS` and `SWITCH` had the same shape.
- **Why it stayed invisible, and this is the interesting part:** the archetypal
  formula is `IF(ISNUMBER(FIND("Pow",F11)), <arithmetic using FIND("-",R11)>,
  Q11-P11+1)` — a guard whose entire purpose is to protect a branch that would
  otherwise error. While `FIND` was unimplemented the whole cell was skipped as
  `unsupported`, so the bug could not be observed. Implementing `FIND` made the
  guard work, made the unused branch legitimately produce `#VALUE!`, and only
  then did `IF` propagate it. **A latent bug that a missing feature had been
  hiding, revealed by adding the feature.**
- **Measured:** it is the dominant cause of the 115,396 new disagreements the
  TEXT family introduced — 43,309 of 43,900 sampled disagreements were formulas
  containing `FIND`, essentially all of this shape. On one corpus workbook
  (`andy_zipper__115__DYNEGY-ICE VOL Jun1.xlsx`) disagreements fell from 1,462
  to 503 with the fix alone.
- **Workaround:** `IF`/`IFS`/`SWITCH` join the error-catching set, with each site
  still propagating an error in its own CONDITION or SUBJECT — only an unused
  RESULT is excused. `IF(#VALUE!, a, b)` must stay `#VALUE!`, and without that
  guard it would quietly take the else branch and return a plausible number.
- **The lesson about the corpus, again:** the fixture tier passed 65/65 against
  LibreOffice and every suite was green. Nothing but 15,870 real workbooks was
  going to surface this, because the shape that triggers it — a guard around a
  deliberately-failing branch — is exactly what a fixture author does not think
  to write, having just written the functions that make the guard unnecessary.

## 2026-08-15 — CC — while: ranking the remaining disagreements

### A third of all "wrong answers" were not answers at all
- **Type:** bug
- **Severity:** high
- **What:** with refusals no longer dominant, the 784,787 remaining
  disagreements needed ranking. Bucketed by the SHAPE of the mismatch rather
  than by function name, 61% were "we produced an error where Excel produced a
  number" — and the single largest cause, **272,134 cells or 35% of every
  disagreement in the corpus**, was `#REF!` against a cached number. Sampling
  found 295 of 295 were external workbook references.
- **The cause is one missed spelling.** External references are detected in the
  lexer by a leading `[` — the unquoted `[4]CurveFetch!$D$8` form. But an
  external reference can be QUOTED, and then the bracket is INSIDE the quotes:
  `'[1]FINANCIAL PIVOT'!B12`. Those went down the quoted-sheet path, which read
  the whole thing as an ordinary sheet named `[1]FINANCIAL PIVOT`, failed to
  resolve it, and returned `#REF!` **without setting `unsupported`** — so
  `xlsx.check` scored an unavailable input as a wrong ANSWER.
- **Workaround:** the quoted path marks `external` when the name begins with
  `[`. One workbook went from 288 disagreements to 0, with all 288 correctly
  reclassified as unsupported.
- **Why it hid, and it is the same shape as §13.L:** quoting is forced by a
  space in the sheet name, so the two spellings are not variants of taste — they
  are determined by the data, and any corpus is full of both. §13.L already
  recorded that the quoted form is 42% of the cross-sheet population. The
  unquoted case was implemented, tested, and correct, and its correctness said
  nothing about the other 42%.
- **The measurement lesson:** this was invisible for as long as it was, partly
  because it inflated the *disagreement* pool rather than the unsupported one.
  A cell counted as a wrong answer looks like a formula defect, so every
  ranking by function name scattered these 272,134 cells across `EXPR`, `IF`,
  `VLOOKUP` and the rest instead of naming the one cause. Bucketing by mismatch
  SHAPE found it immediately — the instrument decided what was findable.

### Refusing an ambiguous date range cost more than answering it
- **Type:** bug
- **Severity:** high
- **What:** `xlsx_civil_from_serial` refused every serial below 1900-03-01, on
  the reasoning that the 1900 leap-year bug makes that range ambiguous and
  answering one day out is worse than not answering. That was right about the
  risk and wrong about the cost. An EMPTY cell coerces to serial **0**, so
  `YEAR`/`MONTH`/`DAY` of any blank date cell returned `#VALUE!` — and the shape
  `(YEAR(Q)-YEAR(P))*12+MONTH(Q)-MONTH(P)+1` over blank rows is everywhere in
  real workbooks. Excel answers 1; we answered `#VALUE!`.
- **And the range is not actually ambiguous.** Excel's serial→date mapping there
  is well defined — it is wrong about *history*, not undetermined: 0 is
  1900-01-00 (`DAY(0)` really is 0), 1..59 are 1900-01-01..1900-02-28, and 60 is
  the phantom 1900-02-29 that Lotus invented and Excel kept. Since this module
  implements Excel, reproducing that mapping is the correct answer, not a
  concession to a bug.
- **Workaround:** resolved — the range is mapped explicitly, negatives still
  refused. One corpus workbook went from 503 disagreements to 8.
- **The pattern, for the third time today:** a defensive refusal written to
  avoid a *plausible wrong answer* turned out to be the wrong trade, because the
  input that reaches it in practice was not the ambiguous case anyone imagined —
  it was a blank cell. `IF` propagating an unused branch's error was the same
  shape: correct-looking strictness whose real-world traffic is dominated by
  inputs the strictness was never meant for.

### Ranking by workbooks, not cells, reordered the list immediately
- **Type:** doc-gap (methodology)
- **Severity:** medium
- **What:** the disagreement ranker now reports DISTINCT WORKBOOKS beside cells,
  applying §13.X's lesson to the instrument that produced it. It reordered the
  top on the first run: `#NUM!`→err is 64,534 cells but only **9 workbooks**, and
  `bool`→num is 20,680 cells in **12** — concentrated artifacts that a cell-only
  ranking placed near the top. Meanwhile `num`→`num` is 66,353 cells across
  **779 workbooks**, the widest of anything, and would have looked like a
  third-tier concern.
- **Workaround:** built in, so the skew is legible in the output rather than
  something to remember to correct for. I did not remember it an hour earlier,
  which is the argument for putting it in the tool.

### `SUMIF`'s sum_range is reshaped, not walked flat
- **Type:** bug
- **Severity:** high
- **What:** `SUMIF($D2:$D95,"Quantity",N2:AQ95)` returned 608,160 where Excel
  cached 1,004. Excel anchors sum_range at its TOP-LEFT and reshapes it to the
  criteria range's shape, so that call sums `N2:N95` and never touches columns
  O..AQ. Indexing both ranges by a flat row-major offset instead walks the first
  94 cells of a 30-column rectangle — the first three ROWS, spread sideways.
- **Workaround:** resolved; the offset is mapped by (row, column) using the
  per-argument column counts the collector already records.
- **Why it matters more than a typo:** a mismatched sum_range is not a mistake
  users make and correct. It is what Excel *produces* when a range is dragged or
  a column inserted, so real files are full of them — and the wrong answer is a
  plausible large number, not an error.

### A relative-only tolerance cannot compare anything to zero
- **Type:** bug (in the measurement)
- **Severity:** medium
- **What:** `xlsx.check` compared numbers with `|a-b| <= max(|a|,|b|) * 1e-9`.
  Against a cached `0` that collapses: our `-1.16e-10` gives a tolerance of
  `1.16e-19`, which nothing can meet, so every near-zero floating-point residue
  was counted as a disagreement however close it was in absolute terms.
  `W38-X38` on two equal-looking values is the archetype — Excel stores 0, IEEE
  subtraction leaves dust, and the two are not in disagreement about anything a
  spreadsheet means.
- **Workaround:** relative OR an absolute 1e-9 floor.
- **Note:** this one inflated the *measurement*, not the product — the third
  such today, after quoted external refs and the `blocked_by` gap. A meaningful
  share of this project's "defects" have been in the instruments, and they are
  harder to notice because a number with no breakdown still reads as a fact.

## 2026-08-15 — CC — while: re-validating the riscv64 port

### The xlsx module grew several thousand lines past its own `#if` guard
- **Type:** bug
- **Severity:** high (build-breaking, on any machine without libxml2 headers)
- **What:** `src/modules/xlsx.c` opens `#if HAVE_ZLIB && HAVE_LIBXML2` before the
  reader and used to close it at line 860. The module then grew to ~4,700 lines
  — the formula evaluator, recalc engine and SQL compiler all reach libxml2 and
  zlib types through the reader's structures — and all of that sat OUTSIDE the
  guard. On a machine without `libxml2-dev` the build did not degrade; it FAILED
  at the first `xmlNodePtr`, which is the exact opposite of what this project's
  optional-dependency rule promises.
- **Workaround:** resolved — the guard now closes immediately before
  `xlsx_workbook_retain`/`release` and `xlsx_eval_call`, which must exist in both
  configurations because the last of them is what returns the clean "install
  zlib and libxml2 and rebuild" error.
- **Nothing local could have caught it.** Every x86 dev box here has
  libxml2-dev, so `HAVE_LIBXML2` was 1 in every build anyone ran. It took an
  architecture where the package happened to be absent. `make LIBXML2_AVAILABLE=0
  ZLIB_AVAILABLE=0` reproduces it on any machine and is now the cheap check —
  worth running before a release, since the same drift can recur the next time
  the module grows.
- **The general shape:** a conditional-compilation guard is invisible to the
  compiler you build with. It is only ever tested by a configuration somebody
  actually builds, and "optional dependency" means precisely the configuration
  nobody on the team has.

### valgrind does not exist for riscv64
- **Type:** doc-gap (platform)
- **Severity:** medium
- **What:** `apt-cache policy valgrind` on Ubuntu 24.04 riscv64 reports no
  candidate — upstream's RISC-V port is too recent. So the valgrind tiers in
  run_recidx/stridx/arridx/equality/render/numfmt can only ever SKIP there, and
  the refcounting work those tiers exist to guard is unverifiable on that
  platform by that means.
- **Workaround:** gcc's AddressSanitizer and UBSan do work on riscv64 and cover
  the use-after-free / double-free class. Honest limit measured on the box: ASan
  caught a deliberate use-after-free but reported it as `SEGV on unknown
  address` rather than `heap-use-after-free`, so its diagnostics there are
  degraded rather than absent.
- **Note for release:** this is a permanent platform gap, not a to-do. It should
  be stated in whatever ships, not quietly left as an untested tier.

## 2026-08-15 — CC — while: release-prep doc sweep (xml "release blocker")
- **Type:** doc-gap
- **Severity:** high (it manufactured a false release blocker)
- **What:** `load` is an EXECUTABLE statement, so when a `program` block exists a
  top-level `load` never runs — the same rule that makes declaration hoisting
  safe (`tests/run_pre_registration.sh` asserts `top-level-ran=false`). The
  symptom is a runtime error naming the library, not the `load`:

  ```basic
  load xml                    ' never runs
  program main()
      doc = xml.parse("<a/>") ' runtime error: library not loaded: xml
  end program
  ```

  A previous session hit exactly this, and — because `docs/xml_design.md` still
  said "design proposal; nothing built" — concluded the xml module had never
  been wired, and filed it as a release blocker. The module is fully built:
  every entry point in `docs/reference.md` (parse/parse_file/parse_html,
  find/find_all/attr/text/encode, reader/read/skip_to/subtree/close) runs, and
  six `examples/xml_*_test.bas` goldens have been in `tests/run_examples.sh` the
  whole time.
- **Workaround:** none needed — put `load` inside the block, which every shipped
  example already does. Documented in `docs/reference.md` §Libraries with the
  wrong/right pair and the hoisted-declaration set; the four stale status lines
  (xml, pbi, ari, statistics) corrected.
- **The general shape:** TWO stale artifacts pointed the same way and became
  evidence for each other — a status line that said "nothing built" and an error
  message that said "not loaded". Neither was about the other, and the module
  was fine. A status line is a claim with no test behind it; the goldens that
  contradicted it were passing in the same suite the whole time. Worth trusting
  the suite over the prose: `grep -l` for the feature's tests before believing a
  doc that says it does not exist.

## 2026-08-15 — CC — while: release prep (clean-container builds)
- **Type:** bug
- **Severity:** high (gBASIC did not build on the current Ubuntu LTS)
- **What:** three independent portability defects, none visible on this dev box,
  all found by building in a clean `ubuntu:24.04` / `ubuntu:25.04` container:
  1. **`gi_repository_dup_default` does not exist before ~2.88.** The Makefile
     sets `HAVE_GIR=1` on `pkg-config --exists girepository-2.0` with **no
     version floor**. Measured: symbol absent in 2.80.0 (24.04 LTS) and 2.84.1
     (25.04), present in 2.88.0 (here). So on the LTS the build got `HAVE_GIR=1`
     and then **failed to LINK — killing the whole `gbasic` binary**, not just
     the gi module. Fixed by using `gi_repository_new()`, present in all three.
  2. **The libxml2 structured-error handler gained a `const` in 2.12.0.** Our
     handler used the new form; against 2.9.14 (what BOTH containers ship) that
     is `-Wincompatible-pointer-types` — a *warning* under GCC 13 (24.04) and an
     **ERROR** under GCC 14 (25.04), so 25.04 could not compile at all. Fixed by
     selecting the signature on `LIBXML_VERSION`.
  3. **The GTK 3 `gui` module had not compiled since 2026-07-23.** It still read
     `value->as.array.count` / `.items`, the layout PLAT-ARRIDX replaced with a
     refcounted `as.array.store->`. `gtk+-3.0` is absent on this box, so
     `HAVE_GTK=0` and nobody had compiled that code in three weeks.
- **Workaround:** all three fixed. `ubuntu:24.04` now builds `gbasic` +
  `gbasic-lsp` with every optional module enabled.
- **Also found, same sweep:** `tools/check-deps.sh` named two packages that do
  not exist on Debian/Ubuntu — `libxcrypt-dev` (it is `libcrypt-dev`) and
  `libgirepository1.0-dev` for a `girepository-2.0` module (it is
  `libgirepository-2.0-dev`). Because `--install` runs ONE
  `apt-get install -y $missing`, a single bad name aborts the whole command, so
  following our own instructions installed **nothing**.
- **The general shape, and it is the one already written in the entry above:**
  "optional dependency" means the configuration nobody on the team builds — and
  the same is true of the LIBRARY VERSION nobody on the team has. Every one of
  these was invisible to `make` here and to every test, because a `#if` guard
  and a `pkg-config --exists` check are both blind to what they did not select.
  A version floor is part of a dependency declaration; `--exists` is not enough.

## 2026-08-15 — CC — while: release prep (suite on a minimal build)
- **Type:** bug
- **Severity:** medium
- **What:** `tests/run_examples.sh` and `tests/run_negative.sh` list every case
  unconditionally, so on a build without the optional libraries they FAILED for
  cases doing exactly what the contract promises — raising "X support is not
  available in this build". Measured on a no-optional-deps build: **34 of 182
  examples failed**, i.e. the minimal configuration could not pass its own test
  suite, while the module-specific runners (`run_sqlite.sh` etc.) skip cleanly.
- **Workaround:** both runners now SKIP a case whose stderr carries the runtime's
  own "not available in this build" / "is unavailable" message. Matched on that
  message rather than a name→capability table, because a table is a second list
  to keep in step with the first and rots silently when an example picks up a new
  module. `run_negative.sh` additionally requires that the EXPECTED text is not
  itself that message, so `negative_xml_not_loaded` and friends — which assert an
  unavailable module on purpose — are still compared, not skipped past.
  Behaviour-neutral with deps present: 216 PASS / 0 SKIP before and after.
  Without deps: examples 180 PASS / 36 SKIP / 0 FAIL, negative 240/48/0.

## 2026-08-15 — CC — while: release prep (riscv64 sweep, second pass)
- **Type:** doc-gap (testing)
- **Severity:** medium
- **What:** two negative goldens pinned a message **libxml2 wrote**, not one we
  wrote, and libxml2 rewords its diagnostics between releases. Neither was a
  riscv defect — both fail identically on any x86 box with the older library:
  - `negative_xml_malformed`: our input made libxml2 record TWO errors and we
    report the LAST, so 2.9.14 said "Premature end of data" where 2.15.2 said
    "Opening and ending tag mismatch". Fixed properly, by choosing an input that
    produces ONE error and therefore the same message on both.
  - `negative_xml_reader_depth`: 2.15.2 writes "…: 256, use XML_PARSE_HUGE…
    column 774", 2.9.14 drops the comma and says column 777. **No input makes
    this stable** — the column differs too — so the case now declares the
    libxml2 it was captured against in a sidecar `tests/NAME.libxml2min` and is
    skipped on anything older. A sidecar rather than a name hardcoded in the
    runner, so the constraint lives next to the golden it constrains.
- **Workaround:** resolved. riscv64 is now examples 215 PASS/1 SKIP/0 FAIL and
  negative 276 PASS/12 SKIP/0 FAIL, every skip explained (11 pg = module
  compiled out, 1 = the libxml2 wording).
- **Two general shapes worth keeping.**
  (a) *A golden that quotes a third-party library is a golden about that
  library's version.* It will fail on someone else's machine and the failure
  will look like a defect in your code. Prefer an input whose message is stable;
  where none exists, declare the dependency instead of hiding it.
  (b) **`run_negative.sh` exits on the FIRST failure**, so it reports exactly one
  no matter how many exist. Fixing that one revealed the next, which had been
  invisible the whole time. A sweep that reports "1 failure" from this runner
  means "at least 1" — to get the true list, loop the cases without the early
  exit, as was done here.

## 2026-08-15 — CC — while: release prep (packaging)
- **Type:** bug
- **Severity:** high (silently installed a broken binary)
- **What:** the stdlib search path is COMPILED IN (`GBASIC_DEFAULT_STDLIB`,
  derived from `PREFIX`), but `make` cannot see a changed `-D`. So the sequence
  the Makefile's own comment recommends was broken:

      make                              # bakes /usr/local/share/gbasic/stdlib
      make install PREFIX=$HOME/.local  # installs there; binary still says /usr/local

  `gbasic` was already built and up to date, so make installed the OLD binary
  under the new prefix. Nothing errors. Either `load frame` fails later for no
  visible reason, or — worse — it silently resolves against a DIFFERENT gBASIC's
  stdlib in `/usr/local`, which is the failure that does not look like one.
  This also made my first attempt to verify a prefixed install meaningless: it
  "worked", because it was reading /usr/local, not the tree I had just installed.
- **Workaround:** resolved. A `.stdlibdir-stamp` records the STDLIBDIR each
  object was built with; only `main.o` and `eval.o` read the macro, so only they
  rebuild when it changes. Verified four ways: changing PREFIX rebuilds, changing
  back rebuilds, a no-op `make` stays a no-op (0 compile commands), and `clean`
  removes the stamp. Note the rule had to be defined BELOW `all` — a target
  above it becomes the default goal, and a bare `make` then builds only the
  stamp, which is how I first broke the build entirely.
- **The general shape:** a value injected through `CFLAGS` is invisible to
  make's dependency graph. Any `-D` that changes program BEHAVIOUR (not just
  optimisation) needs something in the graph to represent it, or the build
  silently keeps the old answer. `HAVE_*` flags have the same exposure — they
  change with the environment rather than the command line, which is why the
  DOGFOOD entries above about conditional compilation keep recurring.
- **Also found here:** `make install` did not install `LICENSE`/`NOTICE` (an
  Apache-2.0 distribution must carry them), `uninstall` did not remove the doc
  directory, and `gbasic-lsp` had no install target at all — `make dev` built a
  language server with no supported route onto a PATH.

## 2026-08-16 — CC — while: release prep (dual-licensing the stdlib)
- **Type:** bug
- **Severity:** medium (wrong diagnostics, not wrong execution)
- **What:** an error raised inside a `load`ed library reports the LIBRARY'S LINE
  NUMBER against the ROOT FILE'S PATH. `tests/negative_edgar_unset_identify.bas`
  is 12 lines long and the diagnostic reads:

      runtime error at tests/negative_edgar_unset_identify.bas:170:13: edgar: ...

  Line 170 does not exist in that file. 170 is the line in `stdlib/edgar.bas`
  where the `error` statement actually is. So every diagnostic from a stdlib
  library names a file/line pair that cannot be looked up — and gBASIC's entire
  standard library is `.bas`, so this is the common case, not an edge one.
- **Why it surfaced now:** adding a 23-line licence header to `stdlib/edgar.bas`
  moved the number from 147 to 170 and the golden went red. **The golden had
  been recording a wrong answer since it was written** — the path was always
  wrong, only the number was stable. A golden pins whatever the binary says,
  which is the same lesson as PLAT-EQ: no golden can catch a defect whose output
  is stable.
- **Diagnosis:** `current_import_path` is set only while a library is being
  IMPORTED and restored immediately after, so by the time one of its functions
  is CALLED it is NULL and `runtime_error_path()` falls back to
  `root_source_path`. Meanwhile `current_line` comes from the library's own AST
  node. Line and path come from different places and nothing reconciles them.
- **Workaround:** none needed by users; the message is still readable, just
  unlookupable. Golden rebaselined deliberately (listed in the commit).
- **Two candidate fixes, neither done:**
  1. *Correct but invasive* — give every AST node a source-file identity, set at
     parse time. `include/ast.h` nodes carry `line`/`column` and no file, so this
     touches the parser, ast.c and eval.c.
  2. *Cheaper seam* — `function_register_def(stmt, imported, library)` already
     knows a function came from a library. Store the resolved path on the
     function record and set `current_import_path` for the duration of the call.
     Smaller, but needs care around recursion, nested library calls and actors.
- **Deliberately NOT fixed during release prep.** It is pre-existing, affects
  diagnostics rather than results, and both fixes touch error plumbing — which
  is not something to rush the same day a tag is cut. Recorded here so it is a
  known defect with a diagnosis rather than a surprise.

### RESOLVED 2026-08-16 — library diagnostics name the library
The entry above (an error inside a `load`ed library reporting the library's line
number against the ROOT file's path) is fixed. The cheaper of the two candidate
fixes was taken: the file a function was PARSED from is stamped onto its AST node
at registration -- the one moment both the node and `current_import_path` are in
hand -- and `invoke_function` swaps `current_import_path` for the duration of the
call, saved and restored exactly like `current_env` and `current_this` beside it.

Two things worth keeping:

- **The swap is UNCONDITIONAL**, including when the stamped path is NULL. The
  first version only assigned when a path existed, which left a LIBRARY's path in
  place while a root-program function it called was executing -- the same defect
  one level further in. Proven with a fixture where a library calls back into a
  root function that raises; it now correctly names the root file.
- **The test had to be built so the answer could not be a coincidence.** The
  first draft put the library's `error` on line 6 and the caller was 6 lines
  long, so a wrong answer and a right one looked identical. The fixture now pads
  the library so the raise sits at line 17 while the caller is 6 lines, making
  the old behaviour impossible to read as correct.

Nine goldens moved, all in the same direction: from naming the calling test file
to naming `stdlib/edgar.bas`, `stdlib/ownership.bas` or `stdlib/llm.bas`. Those
same nine had been rebaselined the day before for the line NUMBERS shifting, and
were re-recording a wrong path both times -- which is the standing lesson about
goldens, twice over on the same nine files.

**Known cosmetic follow-up:** the reported path is the resolved one as given, so
a library loaded as `"../stdlib/edgar.bas"` from `tests/` prints as
`tests/../stdlib/edgar.bas`. It resolves and editors open it, but it is not
normalised. Left alone deliberately rather than adding path canonicalisation to
a diagnostics fix.

## 2026-08-22 — CC — while: gBASIC Studio STU-8 (rich viewers) — a wrong-arity call does not stop the program
- **Type:** bug
- **Severity:** high
- **What:** calling a function with the wrong number of arguments prints a bare
  message to stderr, **keeps running**, and yields `nothing`. There is no file or
  line on the message, and the poisoned value flows on until it happens to be
  used arithmetically — so the eventual error points somewhere else entirely.

  ```basic
  function two(a, b)
    return a + b
  end function

  program main(args)
    x = two(1, 2, 3)          ' <-- the actual mistake, line 6
    print "still running, x = " + string(x)
    y = x + 1
  end program
  ```
  ```
  two expects 2 arguments
  runtime error at arity.bas:8:9: arithmetic operator '+' expected number but got nothing
  still running, x = nothing
  ```

  Note the ordering, too: the arity message and the runtime error land on stderr
  while `print` output is still buffered, so the transcript reads as though the
  program failed before it printed.

  This cost real time in Studio. I called `action_notice(app, action, detail)` on
  a two-parameter function; `app` became `nothing`, and the failure surfaced
  three frames away as `for in expects an array` inside `studio_docs`, plus
  `field access expects a record` in a third file. Three cascading errors, none
  of them at the mistake.
- **Why it matters:** arity is known statically for a named function. This is the
  one class of error a BASIC can catch before running a line, and instead it is
  reported at run time, non-fatally, without a location.
- **Workaround:** none. Read the FIRST stderr line and ignore the located errors
  after it — the located ones are downstream damage. A wrong-arity call should
  raise like any other runtime error (with file:line), and ideally be rejected at
  parse time for a call to a function whose definition is in scope.

## 2026-08-22 — CC — while: gBASIC Studio STU-8 — `string(number)` precision differs between releases
- **Type:** language-surprise
- **Severity:** medium
- **What:** the installed 2026 build renders a number through `string()` at **6
  significant digits**, trailing zeros stripped; the current development build
  renders the full value. Same source, same input, different text:

  ```
                    installed          dev
  1.2031044      -> 1.2031             1.2031044
  0.1140233      -> 0.114023           0.1140233
  10.55142       -> 10.5514            10.55142
  round(v, 6)    -> already truncated  full 6 places
  ```

  `round(v, 4)` agrees on both, because four decimals of a small number is
  inside six significant digits either way.
- **Why it matters:** any golden holding a computed float silently encodes which
  interpreter recorded it. Studio's release check (`GBASIC=/usr/local/bin/gbasic
  tests/run_studio.sh`) is what caught this — a viewer golden passed on the dev
  build and failed on the installed one, and the diff looked like a rounding bug
  in my own code rather than a platform difference.
- **Workaround:** keep displayed precision inside six significant digits. Studio's
  bundled `stats.viewers` declares `places: 4` for coefficient columns and says
  why in `studio_viewers._cell`. If the dev build's behaviour is the intended
  one, this is worth a release note: it changes the text of every program that
  prints a computed float.

### RESOLVED 2026-08-22 — the `string(number)` precision skew, by decision

The entry above asked for a decision before either build changed, and the
decision is made: **full expansion stands** — precisely, the shortest decimal
that reads back as the same double, which is what PLAT-NUMFMT implemented on
2026-08-14 and what rc3 ships. The six-significant-digit rendering was the
OLD release's `%g`; it is history, not an alternative. With rc3 now installed,
the development and installed interpreters agree byte-for-byte, and Studio's
five viewer goldens were already verified identical on both.

What remains true and is worth keeping from the entry: any golden holding a
computed float encodes the interpreter that recorded it, and the release check
is what catches that class. Studio's bundled `stats.viewers` keeps `places: 4`
on coefficient columns — that was always the better display, independent of
the skew that prompted it.

### RESOLVED 2026-08-22 — the read-then-shadow warning exists

The entry above asked for "even a warning", and the warning is in — with a
sharper trigger than the entry proposed. Warning on EVERY same-name shadow
would be noise (a plain local counter named like a global is idiomatic), so it
fires only when the frame READ the name from an enclosing scope and then
assigned it: the read-compute-store-back shape, which is what the G_app bug
was. Once per name per process; behavior unchanged; file:line:col named.

The noise question was measured, not argued: zero warnings across gBASIC's 232
examples and 333 negative cases (the latter compare stderr byte-exact), a
direct stderr sweep of all 212 example files, and Studio's 163+29 — the
largest gBASIC codebase there is, whose display drivers capture stderr into
goldens and moved not one of them. Five run_core.sh checks pin the contract:
warns once, names the site, exits 0, stdout unchanged, and an unread same-name
local stays silent.

## 2026-08-22 — CC — while: gBASIC Studio STU-10 (agent act tools) — a global rebind inside a function silently becomes a local, and the code still "works"
- **Type:** language-surprise
- **Severity:** high
- **What:** UNLEARN documents that a function cannot rebind a top-level scalar —
  the assignment creates a local. What is worth adding is how that FAILS: not
  with an error, and not obviously.

  ```basic
  function call_gated(name, a)
    r = studio_tools.invoke(G_app, G_log, G_pol, name, a, "")
    G_app = r.app          ' <-- silently a local
    G_log = r.log          ' <-- so is this
    return encode(r.value)
  end function
  ```

  The tool reported success. The return value was correct. The test printed
  `edit_document -> true`. And the edit had not happened: the buffer still held
  its old text, `dirty` was false, and the audit log was empty — three symptoms,
  one line, none of them pointing at it.

  What made it hard was that the function's OWN result was right. Everything the
  caller could see about the call succeeded; only the world had not changed. A
  raise would have been far cheaper.
- **Why it matters:** this is the shape of every callback in a GUI or agent
  program — a function the framework calls, whose whole purpose is to update
  state the caller holds. gBASIC's answer is to mutate FIELDS of a global record
  (`G.app = ...`), which works; the trap is that the scalar form is not rejected.
- **Workaround:** keep callback state in one global RECORD and write fields.
  Studio does this everywhere (`G.redrawing`, `_DATAGRID`, `_STUDIO_TABLE`) —
  this driver was the one place that used bare globals, and it took three
  contradictory symptoms to find out.

  A diagnostic would pay for itself: assigning a bare identifier inside a
  function when a top-level variable of that name exists is, in practice, always
  a mistake. Even a warning would have turned twenty minutes into ten seconds.

## 2026-08-22 — CC — while: gBASIC Studio STU-10 (teaching, secrets) — three smaller platform edges
- **Type:** missing-feature
- **Severity:** low
- **What:** three things Studio wanted and worked around, recorded together
  because none is worth its own entry:
  1. **No static-class calls through `gi`.** `gi.invoke("Gtk.StyleContext.add_provider_for_display", ...)`
     and `gi.invoke("Gdk.Display.get_default")` both fail with "unknown
     function". Instance methods resolve fine. Studio installs its CSS provider
     per widget via `w.get_style_context().add_provider(...)` instead — which is
     better scoping, but was not a choice.
  2. **No `chmod`.** A credential store cannot be created 0600 from gBASIC. This
     shaped Studio's design away from a key file entirely (the key now comes from
     the environment and is never written), so the outcome is better — but a
     program that must write a private file has no way to say so.
  3. **No password-based key derivation.** `crypto` has sha256/hmac/aes-gcm but
     no PBKDF2/scrypt/argon2, so there is no safe way to turn a passphrase into a
     key. Studio declines to offer passphrases rather than ship a single-round
     hash that looks like one.
- **Workaround:** as above, all three. (1) is the only one where a platform fix
  would clearly help; (3) would be a good addition to `crypto` if secrets become
  common in gBASIC applications.

## 2026-08-22 — CC — while: gBASIC Studio STU-11 (optional git) — `process.run` raises on a missing executable, so "is it installed?" cannot be asked by asking
- **Type:** language-surprise
- **Severity:** medium
- **What:** `process.run` raises when the executable is missing (documented, and
  correct as a default), and gBASIC cannot catch a raise. Together those mean a
  program cannot discover whether an optional tool is present by trying to run
  it — the attempt is fatal for exactly the users who do not have it.

  ```basic
  ' What you want to write, and cannot:
  r = process.run({ command: "git", args: ["--version"] })   ' raises if absent
  ```

  Studio's git integration is OPTIONAL by design, so this shaped the whole
  module: it finds git by walking `PATH` and asking `file_type` on each
  candidate, because `file_type` answers `unknown` instead of raising.

  ```basic
  for each dir in split(env("PATH"), ":")
      if file_type(dir + "/git") = "file" then ...
  ```
- **Why it matters:** every optional external tool has this shape — a formatter,
  a linter, a language server, a version-control binary. The workaround is
  correct but each program reinvents it, and one that forgets gets a crash rather
  than a degraded feature.
- **Workaround:** the PATH walk above. What would remove the need: either a
  `process.which(name)` returning the resolved path or `unknown`, or an option on
  `process.run` making a launch failure a RESULT (`success: false`, a distinct
  `why`) rather than a raise. The second fits the existing result record, which
  already distinguishes a child that exited 127 from one that never started.

## 2026-08-22 — CC — while: gBASIC Studio STU-11 — `file_type` is newer than the released interpreter, and only the release check said so
- **Type:** doc-gap
- **Severity:** medium
- **What:** `file_type(path)` exists in the development build and not in the
  installed 2026 release. Studio's git support used it (it is the only way to ask
  "is this a file" without raising) and worked perfectly against `../gbasic/gbasic`.
  The same source, on `/usr/local/bin/gbasic`:

  ```
  runtime error at tests/drivers/git.bas:64:36: undefined variable: file_type
  ```

  Nothing in the reference marks which builtins are newer than the last release.
  A program written against the development interpreter can therefore be
  correct, fully tested, and broken for every user — with no signal until
  somebody runs it on a released build.
- **Why it matters:** this is the second version-skew defect this project hit in
  one session. The other was `string(number)` rendering at 6 significant digits
  on the release and in full on the development build, which silently made a
  golden a golden about which interpreter recorded it. That one was cosmetic;
  this one is a crash.
- **Workaround:** Studio uses `exists` instead, which is in both, accepting a
  narrower guarantee (it cannot tell a program from a directory) and saying so in
  the source. The general workaround is the release check itself:

  ```sh
  GBASIC=/usr/local/bin/gbasic GBASIC_STDLIB=/usr/local/share/gbasic/stdlib tests/run_studio.sh
  ```

  What would help more than a workaround: a "since" note on builtins in
  `docs/reference.md`, or a `--since` / feature-probe the way a program can ask
  whether a capability is present. Right now the only way to find out is to run
  on an old build and read the crash.

### RESOLVED 2026-08-22 — three of the entries above, in one pass

**Wrong-arity call keeps running** (STU-8 entry): a wrong-arity call to a user
function is now a LOCATED runtime error that halts (or follows `on error`, like
anything else raised):

    runtime error at prog.bas:6:3: two expects 2 arguments, got 3

The spawn path had raised for this all along; plain calls were the outlier and
now match. Negative test: `tests/negative_user_function_arity`. No positive
golden moved, which answers the question the fix raised — nothing in the suites
relied on the old keep-running behaviour.

**`process.run` raises on a missing executable** (STU-11 entry): both proposed
solutions are in.

  * `process.which(name)` — the path execvp would run, or `unknown`. Mirrors
    exec's own rules (literal path when the name has a `/`, `$PATH` walk
    otherwise, empty component = current directory), and requires a REGULAR
    file with execute permission — a directory named like the tool is not a
    hit, which is the mistake the hand-rolled `exists`-based walks make.
    Studio's git module can drop its PATH walk for this.
  * `process.run({..., launch_failure: "result"})` — a failed launch comes back
    as a record (`launch_failed: true, why: "..."`) instead of a raise, closing
    the check-then-run race for callers who want to just run it. The default
    still raises, and the default record shape is unchanged — nobody who did
    not opt in sees a new field, so no existing golden moves. A child that ran
    and exited 127 stays distinguishable.

    Tests: `nap6_which`, `nap6_launch_result`, `negative_nap6_launch_bad`.

**No way to know which builtins postdate the release** (second STU-11 entry):

  * `has_builtin(name)` — answers from the parser registry PLUS the
    dispatch-only names (`exists`, `read`, `list`, ... — the file/dir call
    families eval.c dispatches without registering; the first draft consulted
    the registry alone and answered FALSE for `exists`, a builtin that has
    worked forever, which is exactly the wrong-answer failure the entry warned
    about). Dotted module names are REFUSED rather than answered: there is no
    unified table of module functions and a probe that can be wrong is worse
    than one that says it cannot answer.
  * `docs/reference.md` now carries "*since 0.1.0-rc3*" notes on everything
    added after the rc2 tag — the convention the entry asked for. The release
    check itself (run the suite against the installed interpreter) remains the
    standing advice and is now written into the process.run docs' neighborhood.

Suites after the change: examples 232 PASS, negative 333 PASS (both grew by the
new cases), process suite green, pre-registration tripwire green.

Still open from the 2026-08-22 entries: the `string(number)` precision skew
(needs a decision about which rendering is intended before either build
changes), the global-rebind-becomes-local silence (a parse-time warning is the
right shape; not attempted in this pass), the `gi` static-class-call gap, no
`chmod`, and no PBKDF2.

## 2026-08-22 — CC — while: PLAT-WEB-4, composing a `web.static` response with a request id
- **Type:** language-surprise
- **Severity:** low
- **What:** `web.static(rel, base) + { id: req.id }` — the natural spelling for
  "this record, plus one more field" — raises `arithmetic operator '+' expected
  number but got record`. There is no record merge: not via `+`, and no
  `merge(a, b)` builtin either (checked the eval dispatch). The same shape as
  the no-array-concat item, one type over.
- **Workaround:** bind and assign: `r = web.static(rel, base)` then
  `r.id = req.id`. Three lines for one, fine in a fixture; a route handler
  composing headers onto a library response will hit this constantly.

## 2026-08-22 — CC — while: PLAT-WEB-5, giving web.serve a dispatch loop
- **Type:** language-surprise
- **Severity:** medium
- **What:** `watch` may only be registered at top level ("for now", says the
  runtime's own message), so a LIBRARY FUNCTION cannot install a serving
  loop: web.serve had no way to arrange "when a request arrives, dispatch
  it" and return. Combined with the two known constraints — functions
  cannot mutate caller state, and record copies do not alias — every
  callback-flavored design inside the library was structurally impossible.
- **Workaround:** a designed native capability rather than a dodge:
  `webserver.on_request(server, fn, context)` registers a function VALUE
  the event loop calls per finished request as fn(context, request), return
  value delivered as the response. It also decoupled serving from the
  global-binding convention (`serve(edge)` unassigned works), which the
  watcher path never could. If `watch` in function scope ever ships, this
  entry is the record of what grew around its absence.

## 2026-08-23 — CC — while: auditing the `server` block's error surface — a dropped return value silently disables a supervisor
- **Type:** language-surprise
- **Severity:** high
- **What:** functions cannot mutate caller state, so every gBASIC API that
  "updates" a record RETURNS the new one. `web.pool_start(p)` and
  `web.pool_tick(p)` both do. `web._serve_pool` called them for effect and
  dropped both returns:

  ```
  r = pool_start(p)          ' r.pool has the worker handles; p does not
  while not webserver.stopping()
      pool_tick(p)           ' iterates p.workers -- still []
      sleep(0.5)
  end while
  ```

  `p.workers` stayed empty forever, so the supervisor polled nothing,
  reported nothing and respawned nothing. `workers: N` started N workers and
  then supervised none of them — and because the pool exists to make
  let-it-crash survivable, a handler raise took a worker out permanently.
  Every worker died in turn and the service went dark with the supervisor
  still running and its stderr blank.

  What makes this one worth logging is that it is **invisible at the call
  site and silent at run time**. There is no unused-result diagnostic, and
  the code reads exactly like the mutating version it resembles in every
  other language. All four other callers in the tree
  (`tests/web_pool/*.bas`, `tests/web_tls/tls_pool.bas`) bind the result
  correctly, so the idiom was known — it was one function that forgot, and
  no test noticed because none of them killed a worker.
- **Workaround:** assign the returns (`p = r.pool`, `t = pool_tick(p)` then
  `p = t.pool`). Resolved in this pass, with
  `tests/run_web_handler_errors.sh` crashing more workers than the pool holds
  so an unsupervised pool runs out and fails the tier.
- **Shape of a real fix:** a warning for a discarded non-`nothing` return
  from a user/library function would have caught this at parse time. It is
  the same family as the read-then-shadow warning that already exists, and
  the same family as the `r + {...}` and no-array-concat items: gBASIC's
  value semantics are right, and the tooling around forgetting them is thin.

### RESOLVED 2026-08-23 — the error model, root and branch (PLAT-ERR)

The single largest item in this log's history is closed, and it is worth
recording what it actually cost before it was.

**What was wrong.** `on error` was a PROCESS-GLOBAL mode. Everything broken
followed from that one property: because any frame could be subject to a mode
any other code had set, `resume next` needed generation-counter abandonment to
stop a half-finished callee corrupting its caller — and that is exactly what
made it useless. A function could not catch a raise and return a clean
fallback; the caller's statement was abandoned regardless of what the callee
returned, and `error.clear()` did not rescue it (proven in
`examples/on_error_resume_next_test.bas`, now rewritten as the new model's
proof).

**What it cost.** The resulting doctrine — pre-validate, never rely on
`on error` — is TOCTOU-broken for anything external: `exists(f)` then `read(f)`
has a hole in the middle, and when the file loses the race the process dies.
And it made the platform grow the same missing feature four times, each as a
local emergency:

- `try_decode` (2026-08-14) — because a hand-written JSON validator in gBASIC
  is quadratic: 256 KB took 291 s.
- `process.which` (STU-11) — because `process.run` raises on a missing binary
  and "is git installed?" could not be asked by asking.
- `web.pool`'s `{ok, pool, why}` records — a result convention, unblessed.
- the webserver's `err_out` plumbing (2026-08-23, one day before this) —
  because a handler's malformed response raised and killed the listener.

Four independent inventions of a catchable-error convention the language never
had. That is the shape of a missing feature: it does not announce itself, it
shows up as unrelated workarounds that turn out to rhyme.

**The fix.** `on error` is FRAME-scoped (`docs/error_model_design.md`). It
governs the function that executed it and nothing else; a callee starts in the
default state whatever its caller set. Absorption restores the generation to
the absorbing frame's entry value, which is the single move that turns "poison
the caller" into "catch". Two rules keep deferral honest — one pending error at
a time, and pending errors do not survive the frame — so no raise can vanish:
forgetting a check makes noise, never silence. `on error resume next` is
deleted and `resume` is an identifier again (net keyword count: −1).

**Evidence.** `tests/run_error_model.sh`, 17 cases, written from the design
BEFORE the implementation. All 333 negative cases passed unchanged — the fatal
stderr line stayed byte-identical, which was the compatibility hinge, since
reporting moved from raise time to whichever frame declines. 232 examples pass.
The migration touched 16 files and was mostly one substitution; the interesting
part was the three doctrine comments in `stdlib/llm.bas` that had to stop
asserting a limitation that no longer exists.

**What is NOT resolved:** the workarounds above stay as they are. Each has a
reason of its own now (a scanner reports WHERE the JSON is malformed; `which`
answers a real question; a tool result is data the model reads). What changed
is that they are choices rather than the only option. `stdlib/llm.bas`'s
preflight architecture could now be simplified and deliberately was not — a
separate decision, not fallout from this one.


### RESOLVED 2026-08-23 — the silent-trap class gets a channel (PLAT-WARN)

Ledger item 11 is closed, and the way it closed is the point.

**The blocker was never the warning; it was the absence of an opt-out.** Every
warning gBASIC shipped had to be near-zero-false-positive, because a program had
no way to say "I meant that here". That constraint is why the worst traps stayed
silent: `contains(s, "b*")` searching for a literal asterisk could not warn
without nagging anyone genuinely searching for `b*`, and a discarded-return
warning firing at 173 sites would have been deleted within a day.

Measured before building, across both repos: 2,215 bare call statements; 173
discarding a non-`nothing` return from a gBASIC function; roughly 68 of those
deliberate. At runtime — reachable code only — 54 fire.

So the feature built was the CHANNEL: `on warning print | ignore | goto next |
stop`, frame-scoped, read exactly like errors. `stop` is `-Werror` for a
language with no build step; `ignore` is what makes an aggressive diagnostic
survivable.

**Two things worth recording for whoever extends this.**

The warning pending flag is SEPARATE from the error's, not a severity tag on it.
Sharing it would put warnings under PLAT-ERR rule 2 and re-raise every
unacknowledged one at frame exit — turning all advice into failure. The test
tier `no_rule2` exists to keep that from being "simplified" later.

`warning` is a SOFT name — resolved after the environment walk, so a variable
shadows it. That single placement is why the channel added ZERO reserved words
and why `board.warning` in `examples/edgar/monitor_harness_test.bas` never
noticed. The statement form `warning <expr>` was measured at 4 shift/reduce
conflicts and rejected in favour of the `warning(...)` builtin; `error` gets a
statement form only because it is a hard token, which is exactly the
noun-squatting this project has been trying to stop doing.

**What it found on day one:** three standard-library sites. `web._refuse_ties`
and `llm._check_tool` were writing `return true` where the void convention
(`return nothing`) was meant, and `web._serve_pool` was dropping `pool_stop`'s
returned pool — the same shape, in the same function, as the bug that started
this whole line of work.


### RESOLVED 2026-08-24 — mod, concat and merge

Three of the oldest ledger items, closed together because the ledger itself
said items 3 and 10 had to be decided together, and item 1 shared their
question: builtin or operator?

**Builtin, all three.** `%`, array `+` and record `+` stay unanswered. `%` is
lexer work; `+` on a container is a real semantics question (concatenate, or
add element-wise?) and settling it as a side effect of adding a convenience is
how languages acquire regrets. The convenience was the urgent part.

**The one decision with a wrong answer available: `mod`'s sign.** Truncated
(QBasic, C, JavaScript) and floored (Python, Ruby) are both defensible, and
BASIC tradition says truncated. Floored won on evidence rather than taste:
`docs/ai/UNLEARN.md` has been telling people to write `a - floor(a/b)*b` for
months, which IS floored, and `stdlib/forensics.bas`'s civil-date algorithm
computes `yoe = y - floor(y/400)*400` and is correct for negative years only
under floored semantics. Shipping truncated would have silently disagreed with
every workaround written against the advice the builtin replaces. The
divergence from QBasic is documented in UNLEARN and the reference rather than
left to be discovered.

## 2026-08-25 — `round` was the one numeric builtin that coerced instead of refusing

**What I was doing.** Building `stdlib/market.bas` and checking that its price
frame feeds the existing stats stack, with
`round(stats.max_drawdown(prices), 6)`.

**What was surprising.** It printed `0`. On a monotonically falling series that
reads as "this never dropped", which is a plausible answer and a false one. The
real cause was mine — `max_drawdown` returns a RECORD
(`{max_drawdown, peak, trough}`), not a scalar — but the language turned my
mistake into a wrong number instead of an error.

**The actual defect.** `round` ran its arguments through
`value_number_or_zero`, so a record, an array or a numeric string all became
`0`. Every other numeric builtin refuses: sqrt, abs, floor, ceil, exp, log,
log10, erf, erfc, lgamma, sign and pow all raise `<fn> expects a number`, and
they refuse numeric strings and booleans too. Measured across all twelve —
`round` was the sole outlier. Nothing chose that; it is the neighbouring
inconsistency that makes it a bug rather than a policy, the same signature
`tests/run_silent_traps.sh` was built around.

**Fixed the same day** rather than worked around: `round` now raises
`round expects a number` and `round places must be a number`, matching its
neighbours exactly. Pinned as four negatives. Zero test movement across the
examples and negative suites, which is the evidence that nothing in the tree
depended on the coercion.

**Worth generalising.** The bug class is "a builtin that coerces where its
siblings refuse", and it is invisible until a caller believes the answer. A
sweep of the remaining builtin families for the same shape would be cheap and
is not done.

**SWEEP DONE 2026-08-27, and it found something else.** All 176 builtins were
probed with wrong-typed arguments — a record, a numeric string, a boolean and a
plain number in the first position, then the same four in the second — and the
result was classified by whether the call RETURNED or raised. On the coercion
question the surface is **clean**: every builtin that accepted a wrong-looking
type legitimately takes it (`len("5")`, `number("5")`, `type(x)`, the `is_*`
family), and no numeric builtin accepts a string or a boolean. `round` really
was the sole outlier.

But the probe was written to detect *any* call that succeeded when it should
not have, and that caught a different defect the reading-based search would not
have looked for: `list`, `files` and `folders` each had TWO bare
`fprintf(stderr, ...)` refusals — wrong arity, and a non-directory argument —
followed by `return value_null()`. Unlocated line, result `nothing`, **exit
code 0**, not catchable. That is the silent-trap signature of
docs/warning_model_design.md §7, which the 2026-08-23 sweep promoted for
`goto`/`gosub` and out-of-range reads and which this family was missed by.
Promoted to raises; `tests/run_silent_traps.sh` covers them.

**The lesson is about the search, not the bug.** The follow-up was written as
"sweep the builtin FAMILIES for the same shape", which is a reading task and
would have found nothing, because the coercion class was already clean. Probing
every builtin for *any* wrong success found a defect in a different class in
the same afternoon. Ask the binary what it does; do not ask the source what it
looks like.

## 2026-08-26 — CC — while: a documentation sweep — `dim` is the one keyword a record cannot use as a field, and there was no list of reserved words anywhere

**What I was doing.** Sweeping the docs after the statistics field expansion,
and writing a `market.daily` recipe into the econometrics cookbook with a date
range spelled `from` / `to`.

**What was surprising, twice.**

*First:* `to{date} = "2024-12-31"` is a parse error — `to` is reserved. That is
entirely reasonable, but there was **no list of reserved words in any document**.
The docs named them one at a time and scattered: `end` in a match-record note,
`unwatch` in a 2026-08-20 bullet, `on` in a webserver aside, `stop` nowhere. The
only stated rule was `reference.md`'s "Keywords include:" — an explicitly
partial list of 36 that omitted `do`, `each`, `loop`, `new`, `next`, `nothing`,
`spawn`, `true`, `false`, `unknown` and `until` — followed by "Some keyword
tokens may be accepted as identifiers in specific grammar positions, such as
`end` and `next`", which is true but leaves a reader unable to tell which.

Measured rather than read: there are **47** keywords, and exactly **four** —
`end`, `loop`, `next`, `until` — can be ordinary names. `server`, `warning`,
`default`, `resume` and `from` are NOT reserved, which is worth stating too,
since `CLAUDE.md` describes the `server` block as adding "exactly ONE reserved
word, `server`" while `reference.md` says "Zero new reserved words" — the
second is the one that matches the binary.

*Second, and the actual defect:* every keyword is legal as a record field, in a
literal and after a dot — that is documented (UNLEARN, 0.1.0-rc6) and it holds
for 46 of them. **`dim` is the exception.** `r = { dim: 7 }` fails at
**1:7** — the position of `dim` inside the literal — with

```
`dim` is not a gBASIC statement; assign to create a variable (x = 0)
```

so does `r.dim`. That message exists to help someone arriving from QBasic, and
it is a good message; it simply fires in a position where no statement is
possible and the word is an ordinary field name. `r["dim"]` works, which is what
makes this a leak rather than a policy: the field is perfectly constructible,
just not by the two spellings every other keyword accepts.

**RESOLVED 2026-08-27.** `dim` is delivered as a token now and the grammar
decides: a statement-position `DIM` reports the same byte-identical message,
and `dim` joins the other 46 keywords in `dot_field_name`, so `{ dim: 7 }` and
`r.dim` work. Zero new grammar conflicts, measured. Covered by
`tests/parse_exit/dim_as_field.bas` (the positive half) and `dim_consider.bas`
(the refusal in the second statement position, which the lexer-side check used
to get for free).

**Worked around at the time** by documenting it (reference.md "Reserved words",
UNLEARN) rather than fixing it, because that was a documentation pass and the
fix is a parser change that deserves its own tested commit. The shape is the same
one `round` had on 2026-08-25: a neighbouring inconsistency across a family
where nothing chose the odd one out.

**Worth generalising.** A helpful diagnostic keyed on a bare word is a lexical
decision wearing a syntactic message. The check should ask where the word
appeared, not only what it was — and the same question is worth asking of any
other "did you mean" diagnostics that match on a token rather than a position.

## 2026-08-27 — CC — while: starting Transward (secure file transfer manager) M0
- **Type:** missing-feature
- **Severity:** high
- **What:** There is no way for a gBASIC program to set its own process exit
  status. No `exit(code)`/`quit(code)` builtin exists (`exit(3)` →
  `invalid function call: exit`), and nothing in the reference offers another
  route. A program ends 0 on normal completion or 1 on an unhandled raise, so
  those are the only two statuses it can produce — and raising to get a nonzero
  status also prints a runtime error to stderr, which is not what a CLI wants.

  This matters beyond convenience for anything meant to be *called by other
  software*. Transward's CLI contract is a stable exit-code family (0 success,
  20 auth failure, 21 host-key failure, 41 verification failure, 51 policy
  violation, …) that enterprise schedulers such as OpCon branch on; that is the
  integration surface of the product. `process.run` already models a child's
  status richly (`exit_code`, `signal`, `success`), so gBASIC can *read* an exit
  status it cannot *write* — a program cannot be the child in a pipeline it can
  itself orchestrate.
- **Workaround:** a shell wrapper (`bin/transward`) runs the interpreter,
  reads a machine-readable final line (`TRANSWARD_EXIT=<n>`) from the program's
  output, and exits with that code. Tolerable because a launcher script is a
  packaging artifact anyway, but it puts the product's most externally-visible
  contract in shell rather than in the language, and any gBASIC program invoked
  by a scheduler, a Makefile, CI, or `set -e` has the same problem.
- **Would have been better:** `exit(n)` (0–255), flushing and running normal
  cleanup — or, if the objection is an abrupt jump out of the middle of a
  program, allowing `program main(args)` to `return` a number that becomes the
  status.

## 2026-08-27 — CC — while: designing the Transward SQLite schema (M0)
- **Type:** missing-feature
- **Severity:** medium
- **What:** The SQLite module does not support BLOB in either direction —
  binding a blob parameter is unsupported and reading a BLOB column is a
  runtime error (reference, SQLite type mapping). But gBASIC strings are
  explicitly binary-safe, and the crypto builtins *return raw binary strings*
  (`aes_gcm_encrypt`, `random_bytes`, `sha256`). So the natural pipeline —
  encrypt a credential, store the ciphertext — has no direct storage form,
  even though both halves of it are first-class.
- **Workaround:** every binary column is stored as text: ciphertext, nonce and
  salt `base64_encode`d, digests `hex_encode`d. Correct and readable in
  `sqlite3`, at ~33% size overhead plus an encode/decode on every access, and
  it puts an encoding convention between the crypto layer and the database that
  a blob column would not need.

## 2026-08-27 — CC — while: choosing Transward's timestamp storage (M0)
- **Type:** bug + missing-feature
- **Severity:** high
- **What:** A program cannot obtain a UTC civil timestamp, and the function
  that looks like it does gives a wrong answer silently. Measured on a machine
  in `America/New_York` (EDT, UTC-4) at instant `1787885025`
  (`2026-08-27 22:43:45` local, `2026-08-28 02:43:45` UTC):

  ```
  now()                              -> 2026-08-27 22:43:45   epoch 1787885025
  to_zone(now(), "UTC")              -> 2026-08-27 22:43:45   epoch 1787885025   ' WRONG
  to_zone(now(), "America/Chicago")  -> 2026-08-27 17:43:45                      ' WRONG (true CDT is 21:43:45)
  from_zone(now(), "America/New_York")-> 2026-08-28 02:43:45  epoch 1787899425   ' right wall clock, wrong instant
  ```

  The two halves of the datetime API disagree about what a value *is*.
  `epoch()`/`number(dt)` treat it as an **absolute instant** (they match
  `date +%s` exactly), while `to_zone` treats the same value's rendered wall
  clock as **naive civil time already in UTC** — so it shifts a value that was
  never in UTC, and `to_zone(x, "UTC")` on a local value is a no-op that looks
  like a conversion. `from_zone(now(), <local zone>)` is the only route to the
  right UTC wall clock, and it returns it attached to the wrong instant.

  There is also no way to learn the local zone from within gBASIC (no
  `local_zone()`; `env("TZ")` is routinely empty, as here), and
  `zone_offset` requires the zone name you are trying to discover — so even the
  working route is unreachable without reading `/etc/timezone` through
  `process.run`. Separately, `d{datetime} = "2026-08-27 22:43:45"` parses naive
  text as **local**, so text round-trips are local-flavored too.

  This is load-bearing for anything auditable. Transward records evidence a
  bank's examiner may read years later; "22:43:45" with no zone and no offset,
  written by a service whose host may since have moved zones, is not evidence.
- **Workaround:** store **epoch seconds** (INTEGER) as the canonical value for
  every timestamp column — absolute, unambiguous, correctly sortable, and immune
  to the above. Civil-time rendering is confined to display edges, where a
  display zone is applied deliberately. Costs human readability in `sqlite3`
  (`1787885025` rather than a date), which for an audit database is a real loss.
- **Doc gap found alongside:** the reference documents **`epoch(datetime)`** as
  a conversion taking an argument, but the binary implements `epoch()` with
  **no arguments** (`epoch expects no arguments`); `number(dt)` is the actual
  datetime→epoch conversion. `from_epoch(n)` exists as documented.
- **Would have been better:** a `utc_now()` (or `now("UTC")`), a
  `local_zone()`, and `to_zone`/`from_zone` defined against the instant the
  value already carries — so that `to_zone(x, "UTC")` converts rather than
  reinterprets.

## 2026-08-27 — CC — while: writing Transward's SQL schema in gBASIC (M0)
- **Type:** missing-feature
- **Severity:** medium
- **What:** gBASIC has **no line continuation in any form**. An expression must
  fit on one physical line. All four candidate spellings fail:

  ```basic
  s = "hello " +          ' parse error: unexpected NEWLINE
      "world"
  s = "a" + \             ' lexer error: unexpected token
      "b"
  s = "a" + _             ' parse error: unexpected STRING
      "b"
  a = [ "x" +             ' parse error: unexpected NEWLINE
          "y" ]           '   -- brackets do not suspend it either
  ```

  Most languages that end statements at a newline suspend that rule inside
  brackets (Python), or offer a marker (VB's `_`, C's `\`). gBASIC does
  neither, so the newline is absolute. It bites hardest exactly where gBASIC is
  strongest on paper: a schema migration, an HTML template, a long
  `webserver` response — anything built from text that a human must also read.
  A `create table` with fifteen columns is one 900-character line.
- **Workaround:** build long text as an array of lines and `join` it, which a
  multi-line array literal makes tolerable (those DO span lines, and accept
  `'` comments between elements):

  ```basic
  ddl = join([
      "create table vendors (",
      "  id integer primary key,",
      "  name text not null unique)"
  ], " ")
  ```

  It reads well enough for SQL and is what Transward's migrations use, but it
  is a workaround: every long string pays an array literal, a `join` call, and
  the reader's awareness that the quotes are not the string's real shape. It
  also cannot help a long *condition* or a long call argument list, where
  there is nothing to join.
- **Would have been better:** suspend the newline inside unclosed `(`/`[`/`{`,
  the rule that needs no new syntax and no marker to forget.

## 2026-08-28 — CC — while: the Secure File Transfer Manager (another session) — no way to set an exit status, and no way to get UTC

Two gaps reported from the SFTM build, both reproduced here before being
fixed, and both now closed.

### 1. A program could not set its own exit status

**What was surprising.** There was no `exit` at all. A gBASIC program could
report 0, or 1 by failing, and nothing else. SFTM's design §15 defines an
exit-code family precisely because that is how OpCon branches on a transfer's
result, so this hit the product's main integration surface. The workaround was
to print a final `TRANSWARD_EXIT=<n>` line and have a wrapper script exit with
it — which works, and which puts the most externally-visible contract of the
program in the shell rather than in the language.

**RESOLVED.** `exit(code)` unwinds through `runtime_stopped`, the same path the
`stop` statement already uses, so every frame, watcher and lock tears down
exactly as it did — `exit` is `stop` that also names a status. 0..255, and a
wider value is REFUSED rather than truncated, because the kernel keeps only the
low byte and `exit(256)` would report success from a program that meant to fail.

**The first version was wrong in a way worth recording.** Setting the flag gave
the right status but did not stop execution: the statement after `exit` still
ran. `runtime_stopped` was only ever consulted on paths that already produced a
stop result, and a bare call — which is how `exit` is spelled — is not one. The
check now sits once in `eval_stmt_list`, so it holds for every statement kind
rather than per kind. Right code, wrong work done, is worse than no `exit` at
all.

### 2. `to_zone(now(), "UTC")` is a no-op that reads like a conversion

**What was surprising.** `now()` renders local. `to_zone(dt, "UTC")` reads its
input as ALREADY UTC and renders it in the target — so on a local value it
returns that value unchanged. Measured here at 06:56 EDT while UTC was 10:56: a
four-hour error, labelled UTC, with nothing to see. For an audit product that
was disqualifying, and SFTM switched to storing epoch seconds.

**And it is sharper than reported.** The two halves of the datetime API get
their zone from different places: `number(dt)`/`epoch(dt)` read a datetime as
LOCAL, while `to_zone` reads it as UTC. So the DOCUMENTED route to UTC —
`from_zone(now(), <your zone>)` — produces a value whose `number()` is wrong by
the offset, because UTC civil text is being read as local. Verified: 14400
seconds out. An audit trail built that way stores timestamps hours in the
future.

**RESOLVED by addition, not by changing either contract.** Both functions are
self-consistent; what was missing was any way to say which zone a value is in.

  * `now(zone)` — the current civil time in a named zone. `now("UTC")` is the
    primitive both broken idioms were standing in for.
  * `epoch(dt, zone)` — a datetime placed on the timeline as civil-in-that-zone.
    `epoch(dt)` keeps the local reading, so nothing existing moves.

`epoch()`, `epoch(now())` and `epoch(now("UTC"), "UTC")` now agree to the
second and match `date +%s`. `tests/run_core.sh` asserts all of it against the
SYSTEM clock rather than against a string gBASIC printed, because that is the
only oracle that can tell a real conversion from a no-op; `Asia/Tokyo` is used
as the non-local zone because it has no DST, so the check cannot pass by
accident half the year.

**Not changed: `to_zone` and `number` still read their input the way they
always did.** Changing either would silently move every existing program's
answers. The trap is now documented at each site instead — reference.md under
`now`, and UNLEARN, which is where an agent meets it.

**Worth generalising.** A value that carries no zone but is routinely converted
needs the zone stated at every boundary, or two boundaries will each pick a
different default and the disagreement will be invisible. gBASIC datetimes are
zoneless by design (docs/datetime_design.md §9 argues that well); the defect
was not the design, it was that one of the boundaries did not let you say.

## 2026-08-28 — CC — while: giving a Transward runner a liveness callback (M4)
- **Type:** missing-feature
- **Severity:** medium
- **What:** A **qualified** function cannot be used as a value. `lib.fn(x)`
  calls fine, but `f = lib.fn` raises `undefined variable: lib` — the
  qualified name is recognised only in call position, so a library's functions
  are not first-class outside their own module. Bare top-level function names
  *are* values, as documented, and so are functions in a record field, which is
  what makes the gap visible: everything about first-class functions works
  except reaching one through the module qualifier that the reference otherwise
  requires you to use.

  Hit while passing a heartbeat callback down two layers (supervisor → runner →
  process supervision loop) so a long transfer can prove it is still alive.
  Since gBASIC function values are references rather than closures, a callback
  cannot capture the database handle it needs either, so the natural spelling —
  hand the callee `runs.heartbeat` and let it call it — fails twice over.
- **Workaround:** pass a record carrying both the state and the function, and
  have the callee invoke it as a **method** so `this` supplies the captured
  values:

  ```basic
  ' in the program (a bare name IS a value)
  function beat()
      runs.heartbeat(this.db, this.run_id)
  end function
  ' ...
  ticker = { db: db, run_id: run_id, tick: beat }
  ' the library calls ticker.tick(), and `this` carries the state
  ```

  This works and reads acceptably, but the callback must live in the *program*
  rather than in the library that conceptually owns it, which puts a piece of
  the runner's logic in main.bas purely for reachability.
- **Would have been better:** let `lib.fn` evaluate to a function value in
  non-call position, matching how bare names already behave.

### 2026-08-28 — `lib.fn` could not be used as a value

**What was surprising.** `lib.fn(x)` calls fine; `f = lib.fn` raised
`undefined variable: lib`. A qualified name was recognised only in CALL
position, so passing a library function as a callback had no direct form at
all. Combined with function values being references rather than closures, the
natural way to hand a heartbeat callback to a runner fails twice over — and the
workaround, a record carrying state and function together invoked as a method
so `this` supplies the state, forces a piece of runner logic to live in the
caller purely for reachability.

**RESOLVED.** A bare name has evaluated to a function value since first-class
functions landed, and `value_function` has carried a LIBRARY all along — the
machinery was complete, the qualified spelling simply never reached it. Field
evaluation now falls back to `function_resolve(receiver, field)` when the
receiver is not a variable, which is the same shape the soft names `warning`
and `error` already use one branch above. So a variable named `heartbeat`
still shadows the library `heartbeat`: this adds a fallback and takes nothing.

**And the diagnostic was blaming the wrong thing.** A loaded library with no
such function reported `undefined variable: heartbeat` — the receiver, for the
field's mistake, sending the reader to look for a variable they never wrote.
The call position had always named both. It now says
`library 'heartbeat' has no function 'nosuch'`.

**Two limits found while testing and left alone**, because both are separate
from this and neither is silent:
  * `table[0](7)` does not parse — calling a function value straight out of a
    SUBSCRIPT. Bind it first; a FIELD call (`reg.beat(3)`) works directly.
  * A variable holding a function value cannot be called if its name matches a
    builtin: `first = table[0]` then `first(7)` reaches the BUILTIN `first`.
    That is the collision hazard fixed for library functions earlier the same
    day, in its variable form — and it was met by walking into it while writing
    this fixture. **RESOLVED the same day**: it warns at the CALL site, not at
    the assignment, because `list = [1, 2]` is a perfectly good variable and
    only calling it is the mistake — which also means the warning cannot fire
    on the common harmless case. Once per site, since the trap lives in loops.

## 2026-08-28 — CC — while: bringing up Transward's web interface (M5)
- **Type:** language-surprise
- **Severity:** high
- **What:** Keeping a `serve()`d program alive with the obvious loop **stops it
  serving**, silently and in the worst possible shape:

  ```basic
  h = serve(app)
  print "listening on " + string(h.port)
  while h.running          ' <- looks like every service main() ever written
      sleep(0.25)
  end while
  ```

  The listener binds, the banner prints, `h.running` is true, and the port
  ACCEPTS connections — `curl` connects and then hangs forever with no
  response, no error, and nothing on stderr. Removing the loop entirely fixes
  it: `serve()` keeps the process alive itself, and the shipped
  `examples/notes_server/notes.bas` ends its program block right after the
  `print`.

  What makes this expensive is the failure mode rather than the rule. Every
  other language's server needs the main thread parked, so the loop is what a
  developer writes by reflex; and because the socket is open and the process is
  up, every external check short of an actual request says the service is
  healthy. I lost the diagnosis to my own handlers twice before reducing it to
  a four-line program.

  The reference says dispatch "is installed natively (`webserver.on_request`)
  rather than through a watcher, so an unassigned call does still serve" — true,
  and it explains the mechanism, but it never says that gBASIC code running
  afterwards **prevents** it. `h.running` existing as a readable field actively
  invites the loop.
- **Workaround:** end the program block after `serve()`. The service exits on
  SIGTERM by the documented drain path, so nothing is lost.
- **Would have been better:** either document the rule where `h.running` is
  documented ("do not loop after serve; the runtime owns the process"), or make
  the native loop run during `sleep` so the reflexive spelling works. A warning
  when a program loops on a live server's `running` field would also have
  caught it instantly.

## 2026-08-28 — CC — while: testing Transward's login flow over HTTP (M5)
- **Type:** missing-feature
- **Severity:** medium
- **What:** `webclient` **always follows redirects** and offers no way to turn
  that off. `webclient.request` documents `method`, `headers`, `body` and
  `timeout` — there is no `follow`/`max_redirects` option — and the effect is
  that a redirect becomes invisible to the caller. Measured against a local
  service, same URL, same instant:

  ```
  webclient.request({url: ".../login"})  -> status 200, no location header,
                                            body of the FOLLOWED page
  curl -s -o /dev/null -w '%{http_code}' -> 303, location: .../setup
  ```

  Two consequences beyond tidiness. A caller cannot assert on a redirect at
  all, which is most of an authentication flow (`303 -> /login` when
  unauthenticated is the security property worth testing). And the intermediate
  response's headers are lost with it, so the **`Set-Cookie` that accompanies a
  login redirect never reaches the caller** — the session cookie is dropped on
  the floor and the client cannot stay signed in. Note libcurl's own default is
  the opposite (`CURLOPT_FOLLOWLOCATION` is off), so this is a deliberate choice
  in the binding rather than an inherited one.
- **Workaround:** drive the HTTP tests with `curl` through `process.run` and
  parse `-i` output. It works and tests the real wire, but a gBASIC program
  cannot be an HTTP client for any protocol where a redirect carries meaning —
  OAuth, POST-redirect-GET, or any API that answers 302 — and the project's own
  web tests now shell out rather than dogfood the module.
- **Would have been better:** a `follow` option (default whatever preserves
  today's behavior), plus `redirects` on the response so a caller can see what
  was followed.

## 2026-08-29 — CC — while: reviewing gBASIC's money type against gdash's evidence
- **Type:** bug
- **Severity:** high
- **What:** `money` stores exact int64 cents, and every path in and out of that
  core degrades it. Reported by the gdash session (the type's first real
  consumer); each defect re-verified here against the source rather than taken
  on trust.

  **1. There is no exact way to construct one.** `{USD}` accepts a number,
  which is already a double, and refuses text:

  ```basic
  big {USD}= 92233720368547.75      ' -> 92233720368547.76, silently
  b   {USD}= "92233720368547.75"    ' -> raises: USD modifier expects a number
  ```

  So the int64 range is unreachable through the type's own constructor.

  **2. `*` and `/` by a number leave integer arithmetic.** Both route through
  `(double)left.as.cents * number` and then `round_to_cents(amount / 100.0)`
  (src/eval.c) — a divide by 100 and a multiply by 100 in floating point, for
  an operation that needs neither. gdash calls this the worst of the three and
  that is right: it corrupts a value the caller already got correct, with no
  error. Multiplying by an *integral* scalar is not exact:

  ```basic
  b {USD}= 0.03
  print (b / 3) * 3          ' 0.03 here, but 1000000000.05 / 7 * 7 -> 1000000000.02
  ```

  **3. Scale is hardcoded to cents.** `round_to_cents`, `format_money` and
  `odbc_money_text` all assume `/ 100` and `% 100`. JPY (exponent 0) and
  KWD/BHD/TND (exponent 3) have no correct representation, and neither does
  anything needing sub-cent precision.

  **A fourth, found here:** the rounding rule at the `.5` boundary is not
  well-defined. `0.125 -> 0.13` but `0.145 -> 0.14`, which looks like
  banker's rounding and is not: `round_to_cents` is half-away-from-zero, and
  `0.145` as a double is `0.14499999999999999001`. So the rule depends on the
  binary representation of the literal, not on the text the author wrote. This
  is the same wound as defect 1 — a double sitting at the entrance to an exact
  type — and an exact decimal-text constructor closes both at once.

  **Defect 2 does not reproduce on a casual probe.** `x {USD}= 1000000000.01`
  then `x * 3` gives `3000000000.03`, exactly right — the double has enough
  precision at ordinary magnitudes, and an implementer whose first probe passes
  may conclude the finding is wrong. The threshold is 2^53 *units*, so it moves
  with the scale: at cents that is $90,071,992,547,409.92 (unreachable), but at
  the proposed guard scale it is **$9,007,199,254.74** — reachable by any
  program aggregating a mortgage book or a fund. Guard digits cost 10,000x of
  headroom, which is what makes fixing the arithmetic a prerequisite for the
  representation change rather than a tidy-up after it.

  **Interlock worth knowing before sequencing:** defect 1 blocks the *test* for
  defect 2. The double path only diverges above 2^53 cents
  ($90,071,992,547,409.92), and no exact value that large can be constructed
  from source while defect 1 stands. Attempted and confirmed: the fixture has
  to assert on a value that is already wrong.

- **Workaround:** none needed downstream — gdash carries money as decimal text
  and int64 in SQLite and never constructs a `money` value, which is itself the
  finding: the platform type was not usable for the job it exists for. Open;
  design ruling pending on guard digits and on whether a money value carries
  its currency (per-currency scale requires it). `serialize`/`deserialize` are
  public builtins and the reader requires an exact `SER_VERSION` match, so a
  representation change must accept v1 payloads and rescale rather than bump
  and reject.

## 2026-08-29 — CC — while: scanning a watched directory in Transward (M11)
- **Type:** doc-gap + language-surprise
- **Severity:** medium
- **What:** `list_files(path)` returns **file references**, not strings, but the
  reference describes it as returning "the **files** in one directory, as full
  paths" — and they *render* as paths, so nothing gives the game away until a
  path-taking operation refuses them:

  ```basic
  for each path in list_files(dir)
      f{file} = path          ' runtime error: file modifier expects a path string
  end for
  ```

  `type(entry)` is `"file"` and `string(entry)` is the path, so the fix is to
  use the entry directly for `file_size`/`file_mtime` and `string()` when text
  is wanted. The trap is that "as full paths" reads as a promise about the
  element type, and the error arrives at the `{file}` modifier rather than at
  the listing, so it looks like the modifier is broken.

  Worth comparing with its neighbour: `list(d)` is documented as returning
  `{name, type}` records, which is precise. Either wording `list_files` as
  "file references, which render as full paths", or having `{file}` accept a
  file reference as a no-op, would have removed this.
- **Workaround:** use the entry directly; call `string(entry)` for a path.
- **Also:** the file-deletion builtin is **`delete(f)`**, which appears nowhere
  in the reference's "Files and Directories" list (`exists`, `read`, `write`,
  `append`, `bytes`, `lines`, `chars`, `lock`, `unlock`, `file_size`,
  `file_mtime`, `atomic_replace`, `real_path`, `file_type`, `list`, `files`,
  `folders`). I found it by guessing four names. A destructive operation is the
  one that should be easiest to look up.

## 2026-08-29 — CC — while: preparing Transward's askpass round trip (M12)
- **Type:** missing-feature + language-surprise
- **Severity:** high
- **What:** Two problems, one of which hides the other.

  **1. A child process's environment cannot be set.** `process.run` and
  `process.start` take `command`, `args`, `cwd`, `timeout` and `listen_fds`;
  there is no `env`. The child inherits the parent's environment and nothing
  can be added, removed or overridden.

  That blocks a whole category of integration, because the standard way to
  configure a Unix tool for one invocation is an environment variable:
  `SSH_ASKPASS` and `SSH_ASKPASS_REQUIRE` (how OpenSSH is handed a password
  without putting it in argv, which is exactly what Transward needs),
  `GIT_SSH_COMMAND`, `SSL_CERT_FILE`, `TZ`, `LANG`, `DOCKER_HOST`,
  `no_proxy`. Without it, a program must write a wrapper script that exports
  the variables and `exec`s the real command — which puts a shell back in the
  path that argument-vector execution existed to remove, and for a secret-
  bearing variable it means writing that value to disk.

  **2. An unknown option is silently ignored.** Passing `env:` does not raise;
  it is dropped, so the mistake looks like the feature working until the child
  reports an empty variable:

  ```basic
  r = process.run({ command: "sh", args: ["-c", "echo $MY_VAR"],
                    env: { MY_VAR: "hello" } })    ' stdout is empty
  r = process.run({ command: "true", nonsense_option: 42 })  ' also accepted
  ```

  This is the opposite of `webserver.listen`, which the reference singles out
  for refusing an unknown option **by name** on the grounds that "a
  misspelling that was ignored would leave a server the author asked to publish
  sitting on loopback". The same argument applies here with a sharper edge: a
  silently-dropped `env` leaves a credential unset rather than a port unbound.
- **Workaround:** generate a small wrapper script per run that exports the
  variables and execs `sftp`, in the run's private directory, removed
  afterwards. Every byte of it is Transward-generated, but it reintroduces a
  shell to a code path whose design principle (§25) is that no vendor- or
  operator-supplied value is ever parsed as shell syntax.
- **Would have been better:** an `env` option (merged over the inherited
  environment, with an explicit way to unset), and unknown option names refused
  the way `webserver.listen` refuses them.

## 2026-08-29 — CC — while: extracting a shared test helper in Transward (M6)
- **Type:** language-surprise
- **Severity:** medium
- **What:** A **local** function shadows a library's function of the same name
  so completely that even the **qualified** call fails:

  ```basic
  program main(args)
      load httpc from "helpers/httpc.bas"
      srv = httpc.start_server()   ' invalid function call: httpc.start_server
  end program

  function start_server()          ' a leftover local of the same name
      ...
  end function
  ```

  Deleting the local `start_server` makes `httpc.start_server()` resolve. This
  is a relative of the builtin-collision family already logged, but sharper:
  the qualifier is precisely the escape hatch one reaches for when a name
  collides, and here it does not escape. The diagnostic also points at the
  *call* as invalid rather than naming the local that shadowed it, so the
  reader looks in the library.

  It surfaced while moving harness functions into a shared library and leaving
  the originals behind for a moment — an ordinary refactoring step, and the
  program was broken in a way that read as "the library did not load".
- **Workaround:** delete the local, which was the intent anyway.
- **Would have been better:** let a qualified name always mean the module's
  function, or warn that a local shadows a loaded library's function the way a
  library/builtin collision now warns.

## 2026-08-29 — CC — while: building Transward's credential vault (M12)
- **Type:** bug
- **Severity:** HIGH — silent wrong value from a cryptographic primitive
- **What:** **`aes_gcm_decrypt` does not raise on authentication failure. It
  returns `unknown`.** The reference promises the opposite, in as many words:

  > `aes_gcm_decrypt(key, nonce, blob, aad)` — verifies and decrypts; a wrong
  > key, nonce, `aad`, or tampered blob **raises** rather than returning
  > garbage.

  Measured, all three cases, with `on error goto next` armed:

  ```basic
  blob = aes_gcm_encrypt(key, nonce, "secret text", "")
  aes_gcm_decrypt(key, nonce, blob + "x", "")        ' -> unknown   (no raise)
  aes_gcm_decrypt(key, nonce, <one byte flipped>, "")' -> unknown   (no raise)
  aes_gcm_decrypt(random_bytes(32), nonce, blob, "") ' -> unknown   (no raise)
  ```

  Why this is worse than an ordinary API mismatch: the whole reason to choose
  an **authenticated** cipher is that tampering is detected rather than
  decrypted into something else, and the documented contract is what a caller
  builds on. A program written against the reference does no check, because the
  reference says a check is unnecessary — so the failure flows onward as a
  value. And `unknown` is not obviously wrong at a glance: `string(unknown)` is
  `"unknown"`, so a credential vault following the docs would hand OpenSSH the
  literal password `unknown`, and a program storing the result would persist
  that string. Silent, plausible, and wrong is the worst combination for a
  primitive whose only job is to refuse.

  Note the neighbouring behaviour is right: `pbkdf2`/`scrypt` raise on an empty
  salt, and the reference says AEAD failures "raise structured errors ... these
  builtins pre-validate rather than degrade". This one degrades.
- **Workaround:** every call site treats `unknown` as a failure and raises
  itself:

  ```basic
  plain = aes_gcm_decrypt(key, nonce, blob, "")
  if is_unknown(plain) then
      error { message: "vault: decryption failed", source: "vault" }
  end if
  ```

  That works, but only for callers who discovered the discrepancy. Anyone
  following the reference has an undetected failure path.
- **Would have been better:** raise, as documented. If returning a value is
  deliberate, the reference must say so loudly and the value should be
  `nothing` rather than `unknown` — but raising is the right answer for a
  primitive whose contract is authentication.

## 2026-08-29 — CC — while: implementing PLAT-MONEY phases 0 and 1
- **Type:** bug
- **Severity:** high
- **What:** A fifth money defect, found while fixing the four already filed and
  not in gdash's report: **`money + money` and `money - money` were unchecked
  signed overflow.** That is undefined behaviour in C, not a defined wrap. At
  cents scale:

  ```basic
  a {USD}= "92233720368547758.07"   ' int64 max
  b {USD}= "0.01"
  print a + b                        ' -92233720368547758.08  -- a SIGN FLIP
  ```

  It had been unreachable for the type's whole life, because until phase 0
  landed there was no way to construct a value that large. Fixing exact
  construction is what exposed it — which is the general shape worth
  remembering: **closing one defect can make a second one reachable**, and the
  fixtures for the first are where the second shows up.

  A second instance of the same pattern in the same afternoon: `format_money`
  rendered `LLONG_MIN` as `--92233720368547758.-8` because negating it
  overflows. Also pre-existing, also unreachable until exact construction
  existed. Both came from int64's asymmetric range — the most negative value is
  one greater in magnitude than the most positive — which is worth treating as
  a standing hazard anywhere money is negated or its magnitude taken.

- **Workaround:** resolved in the phase 1 commit. `+`/`-` are now
  `__builtin_*_overflow`-checked and raise `money value is out of range`;
  `format_money` builds the magnitude unsigned. `odbc_money_text` was checked
  against the same boundary and was already correct, since dividing `LLONG_MIN`
  by 100 does not overflow.

## 2026-08-29 — CC — while: running the full gate during PLAT-MONEY phase 1
- **Type:** perf
- **Severity:** low
- **What:** `run_stridx.sh`'s `byte_at loop` shape tier is **load-sensitive
  enough to flake**. Its normal ratio on this machine is ~3x against an 8x
  gate, but during a full `run_all.sh` — where suites run back to back and the
  machine is busy — it measured **8.57x and failed**. Standalone immediately
  afterwards: 3.58x.

  Verified as pre-existing rather than a regression: three runs on the phase-0
  binary gave 2.96x / 2.94x / 3.19x and three on phase 1 gave 3.20x / 2.57x /
  2.77x — indistinguishable.

  The tier is doing its job (it is a ratio, not an absolute time, which is the
  right design) but ~2.5x of headroom is thin for something whose denominator
  is a 0.4s measurement. A longer baseline run, or a repeat-best-of-N, would
  make it robust without weakening the gate.
- **Workaround:** none applied; re-running passed. Recorded so the next person
  who sees a red `run_stridx` in an otherwise-green gate checks the ratio
  before hunting a regression.

## 2026-08-29 — CC — while: testing the money v1 serialization migration
- **Type:** missing-feature
- **Severity:** medium
- **What:** **gBASIC has no binary-safe file read.** `read(path)` truncates at
  the first NUL, and `bytes(path)` returns the file's SIZE rather than its
  content, so there is no way to get arbitrary bytes off disk intact:

  ```basic
  s = "a" + from_bytes([0]) + "b"
  write(p, s)
  print byte_count(read(p))      ' 1, not 3
  ```

  Verified pre-existing (identical on the phase-0 binary), and it has a
  consequence worth naming: `serialize` produces binary, so **a serialized
  value cannot be persisted to a file and read back**. It can only travel in
  memory between actors, or hex-encoded by hand. That corrected a claim in
  `docs/money_design.md`, which had justified the v1 migration partly on
  payloads "sitting in files right now" — they cannot be.

  `write` is binary-safe (the three bytes reach the file); only the read side
  is lossy.
- **Workaround:** `hex_encode`/`hex_decode` around the payload, which is what
  `tests/money/v1_payload.hex` does. A `read_bytes` builtin returning an array
  of byte values, or making `read` binary-safe, would close this properly.
