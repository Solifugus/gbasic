# The frame-scoped error model (PLAT-ERR)

Status: **approved 2026-08-23**, replacing the process-global `on error` modes.
This is a deliberate compatibility break; the migration inventory is §9.

## 1. What was wrong

The old model is documented precisely in `docs/ai/ERRORS.md`: `on error` was a
**process-global mode**, and everything broken about it followed from that one
property. Because every frame was subject to whatever mode any code had set,
`resume next` needed the generation-counter abandonment semantics to keep a
half-completed callee from corrupting its caller — and those same semantics are
what made it useless: *a function could not catch a raise and return a clean
fallback*, because the caller's statement was abandoned regardless. The
resulting doctrine ("pre-validate, never rely on `on error`") is TOCTOU-broken
for anything external and forced the platform to grow one-off escape hatches
(`try_decode`, `process.which`, the webserver's `err_out`) — four independent
inventions of a catchable-error convention the language never blessed.

## 2. The model

One construct, two targets, **frame-scoped**:

```basic
on error goto next       ' raises in THIS frame: statement abandoned, fall
                         ' through; check with `if error then`
on error goto cleanup    ' raises in THIS frame: jump to the label
on error stop            ' restore this frame's default (propagate)
```

- **Scope is the executing frame** — a function invocation, or the top-level
  program (the program block body and loose top-level statements are one
  frame). The setting takes effect at the statement that executes it, may be
  changed mid-frame, and dies with the frame. Every function starts in the
  default state *regardless of its caller's mode*.
- **Default state: propagate.** A raise in a frame with no active `on error`
  unwinds the call stack — frames between are destroyed normally — to the
  nearest ancestor frame with an active handler, whose policy applies **at
  that frame's own statement** (the call that was in flight is abandoned; under
  `goto next` execution falls through to the statement after it). A raise that
  reaches the top unhandled is **fatal**, reported in the same single-line
  format as before (byte-identical), with the trace available to
  `--json-diagnostics`.
- **`resume` is no longer a keyword.** `on error resume next` is a parse
  error; `resume` becomes an ordinary identifier. Net keyword count: −1.

### 2.1 `goto next` — the checked-statement style

```basic
cfg_text = read(path)
if error then
    print to error "config unreadable: " + error.message
    cfg_text = "{}"
end if
```

A raise under `goto next` abandons the raising statement (its assignment
target is left untouched — unbound, or holding its prior value; assign the
fallback inside the check block), records the error, marks it **pending**, and
continues at the next statement *in the same statement list* (inside a loop
body, that means the next statement of the body).

Two rules make deferral safe, and they are the point of the design:

1. **One pending error at a time.** A raise while an unacknowledged error is
   pending is *not* absorbed — it escapes the frame as if unhandled. The
   deferral privilege is spent until the pending error is checked. This kills
   the classic silent pattern where the last of several errors shadows the
   rest.
2. **Pending errors do not survive the frame.** If a function returns (or
   falls off its end) with an unacknowledged pending error, the return
   converts into a **re-raise at the call site** in the caller. Between rules
   1 and 2, no raise can ever vanish: every one is either acknowledged by a
   check or surfaces upward. Forgetting a check produces noise, never silence.

### 2.2 `goto label`

The label is resolved in the frame's own statement lists — inside a function
body, or against the top-level list, so `on error goto <label>` works at the
top level too (`examples/error_test.gb` relies on this and predates the
redesign). When the handler fires, the frame is **disarmed** — a raise inside
the handler section propagates instead of looping. Re-arming is executing
`on error goto` again, deliberately. A label cannot be named `next` (the token
is claimed by the other form; the grammar enforces this for free).

**The jump is the acknowledgment.** Inside the handler there is nothing
*unacknowledged* left to check, so bare `error` is `false` there while
`error.message` still describes what happened. This is why the label form is
not subject to rules 1 and 2: an error routed to a handler has been handled by
construction, and a handler that returns normally does not re-raise.

### 2.3 Reading `error`

Bare `error` and `error.field` are grammatically distinct and deliberately
answer different questions:

- **Bare `error`** answers *"is there an unacknowledged error?"* — and claims
  it. If the current frame has a pending error: acknowledged (pending flag
  cleared), returns the **error object** (a record — truthy). Otherwise
  returns `false`. So `if error then` is true exactly once per raise; a later
  `if error then` in the same frame is false, with no stale-state trap. And
  `e = error` acknowledges-and-snapshots in one move.
- **`error.field`** reads the most recent error's details *without touching
  the pending flag* — which is what makes `error.message` work inside the
  check block after the condition consumed the flag. Fields: `message`,
  `code`, `source`, `line`, `column`, `details` (record, §2.4), `trace`
  (§2.5).
- `error.clear()` clears both the stored error and the pending flag. It is
  rarely needed now; the check itself is the acknowledgment.

### 2.4 Structured raises

`error <expr>` accepts a string (as before) or a **record**:

```basic
error { message: "insufficient funds", balance: b, needed: amt }
```

`message` is required (a record without one is itself an
`invalid argument` raise). `code` and `source` are honored if present
(defaults `2000` / `"explicit error"`); `trace` is preserved if present; every
other field lands in the error object's `details` record. Because a snapshot
(`e = error`) carries `message`, **re-raising is just `error e`** — one rule
covers both structured raises and re-raises.

### 2.5 Traces

Every error object carries `trace`: an array of `{name, path, line, column}`
records, innermost first, capped at 64 frames — captured at raise time from
the live call stack. (The field is `name`, not `function`: `function` is a
keyword and a keyword cannot follow a dot, so `fr.function` would not parse —
the same constraint that named the match record's `length`.) The **fatal stderr line does not change**
(single line, byte-identical with the previous format); the trace is program-
visible data and rides `--json-diagnostics`.

## 3. Semantics in full (the rules an implementation must honor)

1. A raise sets the error state (message/code/source/line/column/details/
   trace) and begins unwinding: the raising statement is abandoned at every
   frame the unwind crosses, exactly as the old abandonment semantics did.
2. Unwinding stops at the first frame, from innermost outward, that has an
   active handler **and no pending unacknowledged error**. That frame absorbs:
   - `goto next`: mark pending, continue at the next statement of the list
     containing the frame-level statement that was abandoned.
   - `goto label`: disarm, jump to the label.
   Absorption makes the raise invisible to all outer frames (their in-flight
   statements complete normally). This is what the old model could not do.
3. A frame that declines (no handler, disarmed, or rule 1) passes the unwind
   outward. The outermost frame declining = fatal: report and stop.
4. Frame exit (return, or falling off the end) with a pending unacknowledged
   error re-raises at the call site in the caller (rule 2). The returned
   value, if any, is discarded. The top-level frame is no exception: a
   program that ends with a pending unacknowledged error is reported and
   exits nonzero — its last statements ran, but the error did not vanish.
5. Bare-`error` evaluation acknowledges (pending → checked) and yields the
   object or `false` (§2.3). Acknowledgment does not erase the stored error;
   a new raise or `error.clear()` does.
6. Watcher bodies and modifier bodies execute within the current frame: a
   raise inside them unwinds by the same rules — there is no separate watcher
   error channel. The watcher-cascade cap (code 1005) is an ordinary raise.
7. A raise crossing into native code (a request handler invoked by the
   webserver event loop, a GUI signal) is a raise that found no armed gBASIC
   frame on its path: native code observes it via the existing
   `error_action_pending()` contract and treats it as it always has
   (let-it-crash for a worker). Nothing about §7b changes; what changes is
   that the *handler function itself* can now arm and survive.

## 4. What is deleted

- `on error resume next` — removed; parse error. (`resume` freed as an
  identifier.)
- The process-global mode and the generation-counter *cross-frame*
  abandonment contract. (The counter may survive as an implementation detail;
  its observable "poisons the caller" behavior must not.)
- `on error goto` as process-global single-use — replaced by the frame-scoped
  form above.

`on error stop` survives with a sharper meaning: restore *this frame's*
default.

## 5. What is deliberately unchanged

- Let-it-crash is still the default everywhere. Nothing is caught unless the
  reader can see the `on error` line in the same function.
- The fatal report format (single stderr line) is byte-identical.
- `error.clear()`, `error.code`/`error.source` discrimination, and the
  diagnostic-kind system (`GB_DIAG_*`, frozen) are untouched.
- Pre-validation checkers that answer a genuine question (`process.which`,
  `exists`) remain good API; what retires is pre-validation as *doctrine* —
  stdlib functions may now raise freely, because callers can afford it.

## 6. Why not `try`

A `try <expr>` form (and an error value kind) was designed first and rejected
in favor of this model: the check-after-statement style is native BASIC, needs
**zero new keywords** (`if error then` already parses), and migrating old
resume-next code is mostly *deleting the mode line* — the parser then points
at any check that no longer has a raise to consume, so the migration doubles
as an audit. The `try X else Y` inline-fallback ergonomics are the one real
loss; if wanted later, that is pure sugar over a checked statement and can be
added without disturbing this model.

## 7. Test obligations (tests/run_error_model.sh)

Positive: catch-and-return of a fallback (the previously-impossible case);
check-consumes (second `if error then` false); fall-through inside a loop
body; propagation to the nearest armed ancestor across two unarmed frames;
`goto label` + disarm-in-handler (a raise in the handler propagates);
re-arm after firing; rule 1 (second raise escapes an armed frame); rule 2
(return-with-pending re-raises at the call site); top-level `goto next`;
`e = error` snapshot + `error e` re-raise preserving code/source/trace;
structured `error {record}` with `details`; trace contents (function names,
innermost first); `resume` as an ordinary identifier; `error.clear()`.

Negative: `on error resume next` is a parse error; `error {no_message:1}`
raises; unhandled raise still fatal with the byte-identical single line.

Regression: the 333-case negative suite must not move except where a fixture
itself used the deleted modes (each such rebaseline listed in the commit).

## 8. Implementation map (for the reader of src/eval.c)

- `ErrorFrame { mode, label, pending, entry_generation, parent }`, a stack
  pushed in `invoke_function` beside `current_env`; a base frame for
  top-level/program/actor entry.
- `EvalResult` gains `did_raise`; `eval_result_exits_block` includes it, so
  every existing block loop propagates raises like `did_stop`.
- `runtime_error_raise` no longer reports or jumps: it records state, bumps
  the generation, sets raise-in-flight. `error_action_pending()` includes
  raise-in-flight, so every native bail-out keeps working.
- Absorption (in the statement-list loops): restore `error_generation` to the
  frame's entry snapshot — this is the single move that makes the caller's
  before/after checks pass, i.e. what turns "poison the caller" into
  "catch". Decline: propagate `did_raise`. Outermost decline: report (same
  `gb_report` call, same bytes) and stop.
- `invoke_function` exit: pending → bump generation (re-raise into caller).

## 9. Migration inventory (at design time)

26 occurrences of the deleted forms across both repos: `stdlib/llm.bas` (the
one library), `examples/on_error_goto_next_test.bas` (rewritten to prove
the new semantics), sqlite/postgres integration tests, a handful of gui and
cookbook examples, `inline_if_test`, `json_strict_test`, `xlsx_write_test`,
`reflect_test`, `watcher_cycle_error_test`, `modifier_library_regression_test`.
Each migrates to `on error goto next` + adjacent checks or to the new default
(delete the line, let it propagate).
