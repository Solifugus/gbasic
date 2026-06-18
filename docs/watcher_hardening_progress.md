# Watcher Hardening Progress

## Final Watcher Semantics

Watcher hardening phases 1 through 5 are complete. The implemented watcher
contract is:

- watchers run once when registered
- later watcher execution remains immediate and synchronous on mutation
- watcher triggering is guarded by storage equality; equal-value writes do not
  notify watchers
- during one active watcher-drain cycle, an already pending watcher is not
  enqueued again and runs once against the latest live state
- runaway cascades raise structured runtime error code `1005` from source
  `watcher` with message
  `watcher cycle exceeded 10000 executions in one drain cycle`
- collection mutators notify once after the completed stored mutation through
  the lvalue-aware watcher path
- watcher path matching is symmetric at dot boundaries, so parent watchers see
  child changes and child watchers see parent replacement
- GUI batching remains GUI-local; once GUI-originated mutations reach gBASIC
  storage, ordinary watcher behavior applies
- WebServer `server.requests` and `server.responses` are ordinary live queues
  that use ordinary watcher behavior

## Phase 1 Status

Watcher Hardening Phase 1 is implemented and verified.

The runtime now suppresses watcher notification for assignment writes whose
stored value is unchanged according to the new internal storage-equality helper.
Watcher registration, watcher queue ordering, immediate synchronous drain
behavior, collection mutator notification behavior, GUI queue timing, and
WebServer request/response queue behavior were not redesigned.

## Files Changed

- `src/eval.c`
- `tests/run_examples.sh`
- `examples/watcher_value_change_guard_test.bas`
- `examples/watcher_value_change_guard_test.out`
- `docs/watcher_hardening_progress.md`

`docs/watcher_phase0_validation.md` also exists from the Phase 0 validation
pass and remains part of the watcher hardening documentation set.

## Helper Implementation

Added:

- `record_find_const()` at `src/eval.c:1019`
- `value_storage_equal()` at `src/eval.c:1055`

`value_storage_equal()` is a pure runtime storage predicate used only for
watcher change detection. It does not call `values_equal()` and does not change
language comparison semantics.

Implemented equality rules:

- kinds must match
- `nothing` equals `nothing`
- `unknown` equals `unknown`
- numbers use exact C `==`; `NaN`, if introduced by native code, is treated as
  changed
- strings compare exact byte content
- booleans compare stored boolean value
- arrays compare deeply and in element order
- records compare deeply by field name, independent of insertion order
- date/time values compare every stored field, `time_only`, and precision
- durations compare all components
- money compares stored cents
- file and directory values compare stored paths
- PostgreSQL connection values compare native wrapper identity

The existing language-facing `values_equal()` behavior was intentionally left
unchanged.

## Mutation Paths Updated

Updated assignment handling:

- `assign_lvalue()` now returns `LValueAssignResult`
- `eval_stmt()` only calls `watcher_trigger_change()` after
  `LVALUE_ASSIGN_CHANGED`

Covered assignment paths:

- variable assignment
- record field assignment
- array element assignment
- record string-index assignment
- nested record assignment through field lvalues
- nested array assignment through index lvalues

New variables and new record fields still count as changed. Equal replacement
writes succeed normally but free the unused incoming value and skip watcher
notification.

## Tests Added

Added and registered:

- `examples/watcher_value_change_guard_test.bas`
- `examples/watcher_value_change_guard_test.out`

Coverage includes:

- number equal write suppression
- string equal write suppression
- boolean equal write suppression
- deep array equality
- deep record equality with different field insertion order
- record field equal write suppression
- nested record assignment
- nested array element assignment
- changed writes still triggering watchers
- watcher run-on-registration behavior remains intact

Each section prints `2`: one registration execution plus one actual changed
write.

## Phase 2 Status

Watcher Hardening Phase 2 is implemented and verified.

The watcher queue now deduplicates only pending entries within the currently
active drain cycle. If a watcher is already pending, later triggers skip adding
another queue entry. The watcher still reads live values when it eventually
executes, so latest state wins.

This is queue-management-only deduplication. It does not batch ordinary
top-level mutations, does not coalesce mutation records, and does not change
collection mutator behavior.

## Phase 2 Files Changed

- `src/eval.c`
- `tests/run_examples.sh`
- `examples/watcher_cascade_dedup_test.bas`
- `examples/watcher_cascade_dedup_test.out`
- `docs/watcher_hardening_progress.md`

## Phase 2 Implementation

Updated watcher state and queue management:

- `WatcherDef` now stores a `pending` flag.
- `watcher_enqueue()` returns without enqueueing when the watcher is already
  pending.
- `watcher_enqueue()` marks the watcher pending when it appends a queue entry.
- `watcher_drain()` clears a watcher's pending flag immediately before
  executing that watcher body.
- `watcher_clear_pending()` clears any remaining pending flags when a drain
  completes or is abandoned by the current queue limit behavior.
- `watcher_register()` initializes the pending flag before the immediate
  registration enqueue.

Clearing pending before execution preserves cascade behavior: a watcher that
has already run can still be enqueued again by a later mutation in the same
drain. This phase does not change the existing 10,000 execution limit or its
direct stderr reporting.

## Phase 2 Tests Added

Added and registered:

- `examples/watcher_cascade_dedup_test.bas`
- `examples/watcher_cascade_dedup_test.out`

Coverage includes:

- watcher A triggers watcher B twice while B is already pending
- B runs once for that cascade
- B observes the latest value
- separate top-level mutations still trigger separate B executions
- distinct pending watchers preserve source/registration order
- run-on-registration remains immediate

## Phase 3 Status

Watcher Hardening Phase 3 is implemented and verified.

The watcher execution cap is still 10,000 executions per drain cycle, but
reaching the cap now raises a normal structured runtime error instead of
printing a watcher-specific stderr line and silently discarding the queue.

## Phase 3 Files Changed

- `src/eval.c`
- `tests/run_examples.sh`
- `tests/run_negative.sh`
- `examples/watcher_cycle_error_test.bas`
- `examples/watcher_cycle_error_test.out`
- `tests/negative_watcher_cycle.bas`
- `tests/negative_watcher_cycle.err`
- `docs/watcher_hardening_progress.md`

## Phase 3 Implementation

Added watcher error constants:

- `WATCHER_EXECUTION_LIMIT = 10000`
- `WATCHER_CYCLE_ERROR_CODE = 1005`

Updated watcher dispatch:

- `watcher_drain()` now returns success/failure.
- `watcher_trigger_change()` now returns success/failure.
- `watcher_register()` now returns success/failure for run-on-registration
  execution.
- `watcher_drain()` captures the line and column of the mutation or
  registration that started the drain, and uses that location for the cap
  error.
- Hitting the cap calls `runtime_error_raise()`, stops the current drain,
  discards remaining queue entries, clears pending flags, and clears the
  draining flag.
- Assignment and lvalue-aware `append`/`prepend`/`take_first`/`take_last`
  paths now observe watcher trigger failure and let existing statement error
  handling decide whether to stop, goto, or resume.
- GUI and WebServer native trigger sites now observe watcher trigger failure
  instead of continuing after a cap error.

Exact structured error contract:

- symbolic name: `watcher_cycle`
- code: `1005`
- source: `watcher`
- message: `watcher cycle exceeded 10000 executions in one drain cycle`

Uncaught formatting uses the normal runtime error path, for example:

```text
runtime error at tests/negative_watcher_cycle.bas:2:1: watcher cycle exceeded 10000 executions in one drain cycle
```

## Phase 3 Tests Added

Added and registered:

- `examples/watcher_cycle_error_test.bas`
- `examples/watcher_cycle_error_test.out`
- `tests/negative_watcher_cycle.bas`
- `tests/negative_watcher_cycle.err`

Coverage includes:

- watcher loop exceeds the cap
- uncaught cycle exits nonzero through the negative runner
- stderr uses normal `runtime error at file:line:column` formatting
- catchable `on error resume next` path exposes `error.message`,
  `error.code`, `error.source`, and source line information
- a later independent finite watcher drain still works after a caught cycle
  error

## Phase 4 Status

Watcher Hardening Phase 4 is implemented and verified.

Language-level mutating array builtins now use a unified lvalue-aware
notification contract. When the first argument is an identifier, field, or
index lvalue, the builtin mutates the stored value, returns the same kind of
result as before, and notifies watchers exactly once after the final stored
value is complete.

No non-mutating helpers were changed. `remove_key()` was audited and left
unchanged because it is documented as immutable and returns a new record.
Native WebServer response queue consumption remains unnotified because it is
internal transport maintenance, not execution of a language mutator.

## Phase 4 Files Changed

- `src/eval.c`
- `tests/run_examples.sh`
- `examples/watcher_mutator_notification_test.bas`
- `examples/watcher_mutator_notification_test.out`
- `docs/watcher_hardening_progress.md`

## Phase 4 Implementation

Added common dispatch helpers:

- `notify_lvalue_mutation()`
- `expr_is_lvalue_path()`

Converted symbol-only in-place helpers to lvalue-reference helpers:

- `insert_into_array_ref()`
- `remove_from_array_ref()`
- `remove_value_from_array_ref()`
- `reverse_array_ref()`
- `unique_array_ref()`
- `sort_array_ref()`

Mutator call dispatch now recognizes identifier, field, and index lvalues for
all audited mutating array builtins. Watch notification happens once after a
successful operation through `notify_lvalue_mutation()`.

Change/no-change behavior:

- `append`, `prepend`, `insert`, `remove`, `take_first`, and `take_last`
  always notify after successful stored mutation.
- `remove_value` notifies only when it finds and removes a matching value.
- `reverse` notifies only when the reversed stored value is storage-different.
- `sort` notifies only when sorting changes stored order.
- `unique` notifies only when at least one duplicate is removed.

## Mutators Audited

Included and unified:

- `append`
- `prepend`
- `insert`
- `remove`
- `remove_value`
- `take_first`
- `take_last`
- `reverse`
- `sort`
- `unique`

Audited and excluded:

- `remove_key`: immutable record helper; returns a new record and does not
  mutate stored values.
- `first`, `rest`, `contains`, `find`, `find_by`, `join`, and related helpers:
  read-only or functional.
- file `append`: file I/O operation, not a collection mutator.

## Phase 4 Tests Added

Added and registered:

- `examples/watcher_mutator_notification_test.bas`
- `examples/watcher_mutator_notification_test.out`

Coverage includes:

- `watch state.items` on a nested record path
- exact one notification for `append`, `prepend`, `insert`, `remove`,
  `take_first`, `take_last`, `reverse`, `sort`, `unique`, and `remove_value`
- watcher sees the final state after each mutation
- existing return-value behavior for array-mutating builtins
- no notification for no-op `reverse`, already-sorted `sort`, already-unique
  `unique`, and no-match `remove_value`
- root array mutation still notifies exactly once

## Phase 5 Status

Watcher Hardening Phase 5 is implemented and verified.

Watcher path matching is now symmetric at dot boundaries. A watcher matches a
change when the watched path and changed path are equal, when the watched path
is a dot-boundary prefix of the changed path, or when the changed path is a
dot-boundary prefix of the watched path.

This preserves parent-observes-child behavior and adds child-observes-parent
replacement behavior. Array indexes remain collapsed to their containing array
path; this phase did not add index/key-aware watcher identities.

## Phase 5 Files Changed

- `src/eval.c`
- `tests/run_examples.sh`
- `examples/watch_symmetric_path_test.bas`
- `examples/watch_symmetric_path_test.out`
- `docs/watcher_hardening_progress.md`

## Phase 5 Implementation

Updated watcher path matching:

- added `path_is_dot_prefix()`
- changed `watcher_name_matches_change()` to match either prefix direction

The dot-boundary rule prevents false prefix matches such as `state` matching
`statement` or `state.name` matching `state.namespace`.

## Phase 5 Tests Added

Added and registered:

- `examples/watch_symmetric_path_test.bas`
- `examples/watch_symmetric_path_test.out`

Coverage includes:

- `watch(state)` fires when `state.value.inner` changes
- `watch(state.value)` fires when `state` is replaced
- `watch(state.value.inner)` fires when `state` is replaced
- unrelated paths do not fire
- sibling paths do not notify each other
- multiple child watchers run in source/registration order after parent
  replacement
- equal parent replacement remains suppressed by Phase 1
- nested array mutation behavior from Phase 4 still passes

## Verification Results

Commands run:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webclient.sh
./tests/run_webserver.sh
```

Results:

- `make clean && make`: pass
- `./tests/run_examples.sh`: pass, including
  `examples/watch_symmetric_path_test.bas`,
  `examples/watcher_value_change_guard_test.bas` and
  `examples/watcher_cascade_dedup_test.bas`, and
  `examples/watcher_cycle_error_test.bas`, and
  `examples/watcher_mutator_notification_test.bas`
- `./tests/run_negative.sh`: pass, including `tests/negative_watcher_cycle.bas`
- `./tests/run_webclient.sh`: pass
- `./tests/run_webserver.sh`: pass

## Compatibility Notes

Preserved behavior:

- watchers still run once when registered
- watcher queue order for distinct pending watchers is preserved
- watcher execution remains immediate and synchronous on mutation
- `without watchers` behavior is unchanged
- collection mutator return values and validation behavior are preserved
- GUI queued mutation timing is unchanged
- WebServer request/response queue behavior is unchanged
- language `=` and `values_equal()` are unchanged
- watcher execution cap value remains 10,000

Intentional behavior change:

- assignment of a storage-equal value no longer notifies watchers
- within one active watcher-drain cycle, a watcher already pending is not
  enqueued again
- hitting the watcher execution cap raises a structured runtime error with
  code `1005` and source `watcher`
- mutating array builtins now use the same lvalue-aware watcher notification
  path and notify exactly once after successful stored mutation
- watcher path matching is symmetric at dot boundaries, so parent replacement
  invalidates descendant watchers

Known remaining limitations and follow-up work:

- array indexes and dynamic keys are still collapsed to their containing array
  or record path; index/key-aware watcher identities remain outside this
  hardening pass
- GUI behavior still has manual coverage only

## Next Recommended Phase

Watcher hardening phases 1 through 5 are complete. The public watcher
semantics are now documented in `README.md`, `docs/reference.md`, and
`docs/tutorial.md`.

## Remaining Watcher Phases

- none for the planned hardening pass

## Documentation Pass Verification

The final watcher semantics documentation pass updated `README.md`,
`docs/reference.md`, `docs/tutorial.md`, and this progress document. No runtime
source files were changed in this pass.

Commands run:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webclient.sh
./tests/run_webserver.sh
bash tests/run_bag_smoke.sh
./tests/run_postgres.sh
```

Results:

- `make clean && make`: pass
- `./tests/run_examples.sh`: pass
- `./tests/run_negative.sh`: pass on sequential rerun
- `./tests/run_webclient.sh`: pass
- `./tests/run_webserver.sh`: pass
- `bash tests/run_bag_smoke.sh`: pass; the script is not executable, so it was
  run through `bash`
- `./tests/run_postgres.sh`: skipped by design because
  `GBASIC_POSTGRES_TEST=1` and the standard `PG*` connection variables were
  not set
