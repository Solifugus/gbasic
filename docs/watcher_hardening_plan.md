# Watcher Hardening Development Plan

## Scope

This plan hardens ordinary watcher reactivity while preserving the current
immediate, synchronous-on-mutation execution model.

The work is deliberately incremental. Each phase has an independent stop point
and must pass the full regression suite before the next phase begins.

Explicit non-goals:

* batching watcher notifications
* coalescing mutations into transactions
* event-pump or other safe-point execution
* topological or glitch-free reactive ordering
* GUI semantic changes
* removal of run-on-registration behavior
* date/time comparison redesign

## Current Implementation Baseline

### Watcher execution

The watcher implementation is concentrated in `src/eval.c`:

* `watcher_name_matches_change()` performs one-way path matching. A watcher on
  `parent` matches a change to `parent.child`; the inverse does not match.
* `watcher_matches_change()` checks all watched paths for a watcher.
* `watcher_enqueue()` appends every matching watcher index to the queue. It
  does not track whether that watcher is already pending.
* `watcher_drain()` executes the queue synchronously. A mutation made by a
  watcher appends more work to the same queue.
* `watcher_trigger_change()` scans watchers in registration/source order,
  enqueues matches, and immediately drains unless a drain is already active.
* `watcher_register()` enqueues and runs the new watcher immediately.
* `watcher_drain()` currently stops after 10,000 executions by printing
  `watcher queue limit reached` directly to stderr. This is not a structured,
  catchable runtime error.

Watcher bodies read live values when they execute. There is no value snapshot
attached to a queue entry.

### Assignment and watch paths

`eval_stmt()` handles `AST_STMT_ASSIGN` by evaluating the right-hand side,
calling `assign_lvalue()`, deriving a path with `lvalue_watch_path()`, and
calling `watcher_trigger_change()` unconditionally after a successful
assignment.

Relevant functions in `src/eval.c`:

* `assign_lvalue()`
* `resolve_lvalue_ref()`
* `lvalue_root_name()`
* `lvalue_watch_path()`
* `env_set()`
* `record_set()`

`env_set()` and `record_set()` replace an existing value even when the new
value is equal. Static record fields are included in watcher paths. Array
indexes collapse to the containing array path, so watcher matching is not
index-aware.

### Equality

`values_equal()` implements language comparison through `eval_comparison()`.
It must not be reused as the watcher change predicate:

* language equality includes type-specific comparison semantics
* date/time equality can compare at the less precise operand's precision
* that behavior is unsuitable for deciding whether the stored value changed
* the helper can perform evaluation-oriented work that a storage predicate
  should avoid

Values use copy semantics. `value_copy()` recursively copies arrays and
records, so pointer identity is not a useful change test for ordinary
collections.

### Collection mutators

The current mutators are split between functional helpers, symbol-only
in-place helpers, and lvalue-aware helpers.

Lvalue-aware and currently notifying:

* `append()` and `prepend()` through `append_to_array_ref()`
* `take_first()` and `take_last()` through `take_from_array_ref()`

These accept identifiers, record fields, and array-index lvalues and call
`watcher_trigger_change()` after mutation.

Symbol-only in-place and currently not notifying:

* `insert()` through `insert_into_array_symbol()`
* `remove()` through `remove_from_array_symbol()`
* `remove_value()` through `remove_value_from_array_symbol()`
* `reverse()` through `reverse_array_symbol()`
* `unique()` through `unique_array_symbol()`
* `sort()` through `sort_array_symbol()`

For these operations, non-identifier expressions use functional behavior on a
copied value rather than mutating the original lvalue.

Other collection-like builtins such as `first()`, `rest()`, and
`remove_key()` are functional and do not mutate stored arrays. The file
`append()` overload is not a collection mutation and is outside this plan.

### Runtime errors

`runtime_error_raise()` and `error_set_state()` already support:

* runtime error code, source, message, line, and column
* `on error goto`
* `on error resume next`
* normal uncaught runtime reporting
* nonzero process exit for an uncaught runtime stop

Watcher draining does not currently use this machinery. The triggering
assignment or mutator must also observe a watcher error and propagate the
result through the normal statement error path.

### GUI interaction

GUI callbacks enqueue backend mutations and flush them after a GTK iteration.
The flush applies a mutation and then invokes ordinary watcher dispatch. That
GUI queue is separate from the watcher queue and must not be changed by this
work.

The GUI field setters already avoid some equal string and boolean updates
before enqueueing them. The general value-change guard will make equal-value
behavior consistent for mutations that enter through ordinary assignment.

### WebServer interaction

Incoming requests are appended to the live `server.requests` array on the
evaluator thread. The append then triggers ordinary watcher dispatch
synchronously. Watchers commonly consume requests with `take_first()` and
append responses to `server.responses`.

Response validation currently happens before the response append. That order
must be preserved.

Internal removal of a response after the native server has consumed it is
runtime queue maintenance, not a language collection-mutator call. It should
remain outside application watcher notification unless a separate design
decision explicitly exposes native queue consumption as a language mutation.

## Existing Coverage

Relevant permanent examples and tests include:

* `examples/watch_test.gb`
* `examples/watch_test.out`
* `examples/watch_path_test.gb`
* `examples/watch_path_test.out`
* `examples/nested_array_mutation_test.bas`
* `examples/nested_array_mutation_test.out`
* array mutator examples and expected output files
* `tests/webserver_integration.bas`
* `tests/run_webserver.sh`
* GUI manual coverage under `examples/gui/`, including `watch_demo`

`tests/exploratory_watcher_ordering.bas` records useful current ordering
behavior but is not part of a standard regression runner.

Coverage gaps:

* equal-value assignment notification
* deep collection equality as a watcher guard
* duplicate pending watcher entries
* catchable watcher-cycle failure
* exact-once notification across every mutating collection builtin
* no-op mutator notification
* child watcher invalidation after parent replacement
* watcher errors raised from WebServer request processing

## Phase 1: Value-Change Guard

### Required behavior

A mutation triggers watchers only when the stored value actually changes.
Writing an equal value succeeds normally but does not enqueue watchers.

### Recommended equality rule

Add an internal, pure storage equality predicate. Suggested name:

```c
static int value_storage_equal(const Value *left, const Value *right);
```

This predicate is separate from language `=` and uses these rules:

* Kinds must match.
* Null values are equal to null values.
* Unknown values are equal to unknown values.
* Numbers compare by numeric value using exact `==`; `-0` and `0` are equal.
* Strings compare by exact byte content.
* Booleans compare by value.
* Money compares by exact stored cents.
* Durations compare every stored component exactly.
* Date/time values compare every stored field, `time_only`, and precision
  exactly. No less-precise or same-day comparison is used.
* File and directory values compare their stored paths exactly.
* Native connection/resource values compare by native object identity.
* Arrays compare deeply, recursively, and in element order.
* Records compare deeply and recursively by key set and value, independent of
  record insertion order.

NaN is not ordinarily constructible in the language. If it reaches a `Value`
through native code, normal numeric `==` makes NaN unequal to itself and
therefore treats a NaN write as a change. This should be documented in the
implementation comment and revisited only if NaN becomes a supported language
value.

Deep equality is recommended for arrays and records. The runtime uses value
copy semantics, so shallow or pointer equality would report freshly copied but
logically identical collections as changed. That would defeat the guard for
common assignments such as assigning an equivalent literal and would be
surprising to users.

Record equality should be key-based rather than insertion-order-based because
record access is defined by field name, not by visible field position.

### Performance implications

Array comparison is O(total compared elements). A straightforward record
implementation using the current linear field lookup is O(fields squared) in
the worst case. This is acceptable for the first correctness pass, but the
helper should be isolated so record comparison can later use sorting or an
index without changing watcher semantics.

The predicate must not allocate or raise runtime errors. It may recurse through
nested collections. User-created cyclic value graphs are not currently
possible under recursive copy semantics, but deeply nested values can consume
C stack. A recursion-depth guard is not required in this phase unless existing
value operations gain one at the same time.

Mutators should avoid copying an entire collection solely to compare before
and after when they can determine whether they changed it directly. All
change/no-change decisions must nevertheless produce results equivalent to
`value_storage_equal()`.

### Implementation approach

Refactor `assign_lvalue()` to report whether it created or changed the target.
A small result enum is clearer than overloading success:

```c
typedef enum {
    LVALUE_ASSIGN_ERROR,
    LVALUE_ASSIGN_UNCHANGED,
    LVALUE_ASSIGN_CHANGED
} LValueAssignResult;
```

Compare the existing target and incoming value before ownership transfer:

* a new variable is changed
* a new record field is changed
* replacing an existing equal value is unchanged
* replacing an existing unequal value is changed
* an array element assignment follows the same rule

The assignment statement calls `watcher_trigger_change()` only for
`LVALUE_ASSIGN_CHANGED`.

### Files and functions touched

Production:

* `src/eval.c`
* `value_storage_equal()`, new
* `assign_lvalue()`
* assignment handling in `eval_stmt()`
* potentially small internal helpers near `env_set()` and `record_set()`

Documentation after implementation:

* `docs/reference.md`
* `docs/implementation_validation.md`

### Tests added

Add a registered regression example such as:

* `examples/watcher_value_change_guard_test.bas`
* `examples/watcher_value_change_guard_test.out`

Cover:

* equal and unequal scalar assignment
* deeply equal and unequal arrays
* deeply equal records with different field insertion order
* nested collection change
* array order difference
* date/time values with equal full storage
* date/time values that language `=` may consider equal but whose stored
  precision or fields differ
* run-on-registration remains unchanged

### Tests modified

Only expected outputs whose existing behavior intentionally counted
equal-value writes should change. Do not broadly rewrite watcher examples.

### Regression risks

* accidentally using language equality and inheriting lenient date comparison
* treating record insertion order as observable
* comparing a freed or moved right-hand value
* suppressing notification for creation of a missing variable or field
* excessive aggregate comparison cost
* native resource comparison by copied wrapper rather than underlying identity

### Stop point

Stop after value-change guard tests and all existing suites pass. Confirm that
watcher ordering, immediate execution, registration execution, GUI flushing,
and WebServer request handling are unchanged before beginning Phase 2.

## Phase 2: Cascade Deduplication

### Required behavior

Within one watcher-drain cycle, a watcher already pending in the queue has at
most one pending entry. If several changes occur before it runs, it executes
once and reads the latest live state.

This changes queue management only. It does not combine mutations or alter
collection mutator return values.

### Recommended queue rule

Track a pending bit per watcher. `watcher_enqueue()` behaves as follows:

1. If the watcher is already pending, do nothing.
2. Otherwise mark it pending and append its index.
3. Clear its pending bit immediately before executing its body.

Clearing before execution is important. It allows a watcher that has already
run to be enqueued again by a later mutation in the same drain cycle. This
preserves the existing cascading/fixpoint-like behavior and avoids silently
discarding legitimate feedback. The cycle guard in Phase 3 remains necessary.

“Latest state wins” means that a pending watcher reads current runtime values
when it eventually runs. No state snapshot is added to queue entries.

Reset all pending bits whenever a drain is abandoned or the watcher registry is
cleared.

### Files and functions touched

Production:

* `src/eval.c`
* watcher registry state, either a `pending` field on `WatcherDef` or a
  parallel pending array
* `watcher_enqueue()`
* `watcher_drain()`
* `watcher_register()`
* `watcher_clear()`

Prefer a field on `WatcherDef`; it keeps queue membership state sized and
cleared with the watcher it describes.

Documentation after implementation:

* `docs/reference.md`
* `docs/implementation_validation.md`

### Tests added

Add a registered regression example such as:

* `examples/watcher_cascade_dedup_test.bas`
* `examples/watcher_cascade_dedup_test.out`

Cover:

* watcher A changes a value watched by watcher C twice before C runs
* C runs once while pending and sees the second value
* source/registration order among distinct pending watchers remains unchanged
* a watcher that has already run can be enqueued again later in the same drain
* run-on-registration is still immediate

Promote the useful assertions from
`tests/exploratory_watcher_ordering.bas` into permanent output-checked coverage
or replace that exploratory file with the focused regression.

### Tests modified

Update only tests that intentionally demonstrate duplicate pending execution.
Do not alter ordinary source-order expectations.

### Regression risks

* clearing the pending bit after execution and losing self-reenqueue behavior
* failing to clear pending state after an error
* treating “executed during this drain” as permanently deduplicated
* changing source order for distinct watchers
* suppressing registration execution

### Stop point

Stop after confirming pending-only deduplication, latest-state observation, and
preserved cascade behavior. Run the full suite before beginning Phase 3.

## Phase 3: Structured Watcher Cycle Error

### Required behavior

Replace the direct stderr queue-limit message with a catchable runtime error
that carries normal source information and produces a nonzero exit when
uncaught.

### Error contract

Recommended contract:

* symbolic name: `watcher_cycle`
* numeric code: `1005`
* source: `watcher`
* exact text: `watcher cycle exceeded 10000 executions in one drain cycle`

The current public runtime error value exposes code, source, message, line, and
column but not a separate name field. `watcher_cycle` is therefore the
documented symbolic name used in source constants and documentation; adding
`error.name` is outside this phase.

Use named constants in `src/eval.c` for the code and execution limit.

### Reporting and propagation

When the limit is reached:

* call `runtime_error_raise()` with the contract above
* use the line and column of the mutation currently driving watcher execution
* stop the current drain immediately
* discard remaining queue entries and clear their pending state
* leave mutations already completed in place
* clear the draining flag reliably

Normal runtime error policy then applies:

* `on error goto` transfers to the configured handler
* `on error resume next` continues after the triggering statement
* an uncaught error prints through normal runtime reporting with file, line,
  and column and exits nonzero

`watcher_drain()` and `watcher_trigger_change()` should return enough status for
the assignment and mutator call sites to detect a newly raised error. Those
call sites must pass control through the existing statement-level error
handling rather than continuing evaluation after an uncaught watcher failure.

### Files and functions touched

Production:

* `src/eval.c`
* watcher error constants, new
* `watcher_drain()`
* `watcher_trigger_change()`
* assignment handling in `eval_stmt()`
* lvalue-aware collection call handling
* any statement error-generation checks required to route `goto`, `resume`,
  and uncaught stop correctly

Documentation after implementation:

* `docs/reference.md`
* `docs/implementation_validation.md`

### Tests added

Positive catchable-error coverage:

* `examples/watcher_cycle_error_test.bas`
* `examples/watcher_cycle_error_test.out`

Verify error message, code, source, source line availability, and
`on error resume next`. Include `on error goto` coverage if it can be kept
small and deterministic.

Negative coverage:

* `tests/negative_watcher_cycle.bas`
* matching expected stderr file following the existing negative-test naming
  convention
* register it in `tests/run_negative.sh`

Verify an uncaught cycle exits nonzero and uses normal runtime error formatting,
not a direct watcher-specific stderr print.

### Tests modified

Any existing test that expects `watcher queue limit reached` must be updated to
the structured contract. No other runtime error text should change.

### Regression risks

* raising an error but allowing the watcher queue to continue
* stale pending bits after error recovery
* incorrect resume location
* swallowing an uncaught error and returning exit status zero
* replacing the useful triggering source location with an internal runtime
  location
* changing the limit's off-by-one behavior unintentionally

### Stop point

Stop after caught and uncaught watcher-cycle paths pass, including nonzero exit
verification. Run all suites and explicitly verify a later independent watcher
drain works after a caught cycle error.

## Phase 4: Unified Mutator Notification Contract

### Required behavior

Every language-level mutating collection operation must:

* mutate a resolvable lvalue consistently
* notify exactly once
* notify after its final mutation
* use the same lvalue-aware watch-path mechanism
* suppress notification when the stored result is unchanged

Operations in scope:

* `append`
* `prepend`
* `insert`
* `remove`
* `remove_value`
* `reverse`
* `sort`
* `unique`
* `take_first`
* `take_last`

No additional mutating collection builtins were found. Functional operations
on temporary or copied values remain non-notifying because they do not mutate a
stored lvalue.

### Recommended contract and structure

Introduce a common internal path for collection mutation targets:

1. Recognize identifier, field, and index lvalues.
2. Resolve the lvalue with `resolve_lvalue_ref()`.
3. Validate the target type and operation arguments.
4. Perform the complete mutation.
5. Determine whether storage changed.
6. Derive the path with `lvalue_watch_path()`.
7. Call the common watcher notification function once.
8. Return the builtin's existing result.

The common helper should separate lvalue resolution from notification so
native integrations with a known path can share exact-once dispatch without
fabricating an AST node.

Change detection can be operation-specific:

* successful append, prepend, insert, remove, and take operations change array
  structure
* `remove_value()` does not change when no matching element exists
* `reverse()` may be unchanged for empty, singleton, or symmetric arrays
* `sort()` may be unchanged when already in resulting order
* `unique()` may be unchanged when no duplicate is removed

Operation-specific fast paths must agree with Phase 1 storage equality.

Preserve all existing validation and result semantics. In particular,
WebServer response validation must complete before `server.responses` is
mutated or watchers are notified.

### Native WebServer boundary

Incoming native request append changes the live requests array and should
continue to produce one synchronous notification through the shared
path-oriented notifier.

Language-level response append and request `take_first()` should use the same
lvalue-aware contract as ordinary arrays.

Native response removal after transport consumption remains unnotified. It is
internal ownership maintenance rather than execution of a language mutator.
Add a code comment at that boundary if the distinction is not otherwise clear.

### Files and functions touched

Production:

* `src/eval.c`
* collection call dispatch in `eval_call()`
* `append_to_array_ref()`
* `take_from_array_ref()`
* `insert_into_array_symbol()`
* `remove_from_array_symbol()`
* `remove_value_from_array_symbol()`
* `reverse_array_symbol()`
* `unique_array_symbol()`
* `sort_array_symbol()`
* new common mutable-target and notification helpers
* WebServer native request append call site

Symbol-only helpers should be removed, renamed, or reduced to operation logic
once all in-place operations use `Value **` lvalue references. Keep functional
helpers for non-lvalue expressions where that behavior is part of the current
language.

Documentation after implementation:

* `docs/reference.md`
* `docs/implementation_validation.md`
* `docs/webserver_design.md` if the native/language notification boundary needs
  clarification

### Tests added

Add a registered matrix-style regression:

* `examples/watcher_mutator_notification_test.bas`
* `examples/watcher_mutator_notification_test.out`

For every in-scope operation, verify:

* root-array mutation notifies once
* nested field mutation notifies once
* indexed lvalue mutation where valid notifies the containing array path once
* watcher sees final state
* builtin return value remains unchanged

Also verify no notification for:

* `remove_value()` with no match
* reversing an unchanged array
* sorting an already sorted array
* `unique()` on an already unique array

Retain or extend `examples/nested_array_mutation_test.bas` for concise
path-aware regression coverage.

### Tests modified

Update mutator expected outputs only where the new notification contract is the
intended change. `tests/webserver_integration.bas` should remain behaviorally
unchanged and serve as integration regression coverage.

### Regression risks

* double notification from both an operation helper and call dispatch
* notification before mutation completes
* extending lvalue mutation while accidentally removing functional behavior
* returning the mutated array where a take/remove operation currently returns
  an element or count
* notifying failed validation
* breaking WebServer response validation order
* exposing native response consumption as an application mutation
* treating an unchanged sort/reverse result as changed

### Stop point

Stop after the full mutator matrix, nested-path tests, and WebServer integration
pass. Review every in-scope builtin call site to confirm exactly one notifier
owns dispatch.

## Phase 5: Symmetric Prefix Notification

### Required behavior

Keep the existing rule that a watcher on a parent matches mutation of a child,
and add the inverse invalidation rule: replacement of a parent also matches
watchers on descendants.

Examples:

* watcher on `state` matches change to `state.name`
* watcher on `state.name` matches replacement of `state`
* watcher on `state.name` does not match change to `state.age`

### Recommended path rule

Two paths match when any of these is true:

* they are exactly equal
* the watched path is a dot-boundary prefix of the changed path
* the changed path is a dot-boundary prefix of the watched path

Prefix comparison must retain the dot boundary so `state.name` does not match
`state.namespace`.

When a parent replacement invalidates several descendant watchers, enqueue
them in normal registration/source order. Phase 2 pending deduplication applies
normally.

Array indexes remain collapsed to their containing array path. Adding
index-specific watcher identities is outside this plan.

### Edge cases

Replacing a parent can remove a field used by a child watcher. The child
watcher still runs because its dependency was invalidated. A static read of the
now-missing field may raise the normal runtime error. Tests that intentionally
remove fields should use dynamic access or unknown checks when they expect to
continue.

Replacing a whole GUI record can now invalidate watchers on its child fields.
This is an ordinary watcher matching change, not a GUI queue semantic change.

Replacing `server.requests` or `server.responses` as a whole can invalidate
watchers on descendant paths. The existing WebServer type and live-array
constraints remain unchanged; this phase does not add protection against
application replacement of native queue fields.

### Files and functions touched

Production:

* `src/eval.c`
* `watcher_name_matches_change()`
* focused path-prefix helper if extracted for boundary clarity

Documentation after implementation:

* `docs/reference.md`
* `docs/implementation_validation.md`
* `docs/gui_design.md` only if its watcher interaction description needs
  status clarification
* `docs/webserver_design.md` only if parent-replacement implications need
  clarification

### Tests added

Add a registered regression example:

* `examples/watch_symmetric_path_test.bas`
* `examples/watch_symmetric_path_test.out`

Cover:

* child mutation notifies parent watcher
* parent replacement notifies child watcher
* sibling mutation does not notify
* multiple child watchers run in source order after parent replacement
* missing child after parent replacement has documented runtime behavior
* equal parent replacement remains suppressed by Phase 1
* overlapping matches still enqueue a watcher once under Phase 2

### Tests modified

`examples/watch_path_test.gb` may be extended instead of adding a separate
file if its existing output remains readable. Prefer a separate test if
registration-time output makes the original difficult to interpret.

### Regression risks

* false prefix matches without a dot boundary
* unexpectedly broad watcher activation after replacing large records
* duplicate enqueue when several watched paths in one watcher match
* new missing-field errors in child watchers
* confusing whole-queue replacement with normal WebServer element mutation

### Stop point

Stop after symmetric path tests and the full suite pass. Confirm no change to
GUI callback timing, WebServer event-loop timing, registration execution, or
array index path behavior.

## Verification Per Phase

At every phase stop point run:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webserver.sh
./tests/run_webclient.sh
```

Run a script only when it is present and executable. Also run each new focused
test directly while developing so failures are easier to localize.

Before proceeding to the next phase:

* inspect `git diff --check`
* confirm expected output changes are limited to the phase contract
* confirm no GUI or WebServer timing model changed
* update user-facing watcher documentation for completed behavior

## Recommended Delivery Sequence

Each phase should be a separate reviewable commit:

1. strict storage equality and equal-write suppression
2. pending-entry watcher deduplication
3. structured watcher-cycle runtime error
4. unified collection mutator notification
5. symmetric parent/child path invalidation

Do not combine Phase 4 and Phase 5. Mutator notification breadth and path
matching breadth have different regression surfaces and are easier to verify
independently.

## Open Questions

* Should native resource values with distinct wrappers around the same native
  object compare equal by underlying object identity, or should wrapper
  identity be observable? The current ownership model should decide this
  before Phase 1 lands.
* Is O(fields squared) record equality acceptable for the expected record
  sizes, or should Phase 1 include a temporary visited/key index?
* Should the watcher execution threshold remain exactly 10,000, or become a
  named runtime configuration constant while retaining 10,000 as the default?
* For `on error resume next` after a watcher cycle, is retaining all mutations
  completed before the error the intended documented behavior?
* Should native WebServer response consumption remain permanently invisible to
  watchers, or should a future separate design expose transport consumption?
* Should whole replacement of `server.requests` and `server.responses` be
  prohibited by a later WebServer integrity feature? This plan only defines
  watcher consequences if replacement is currently allowed.
