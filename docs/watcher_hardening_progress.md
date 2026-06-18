# Watcher Hardening Progress

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
  `examples/watcher_value_change_guard_test.bas`
- `./tests/run_negative.sh`: pass
- `./tests/run_webclient.sh`: pass
- `./tests/run_webserver.sh`: pass

## Compatibility Notes

Preserved behavior:

- watchers still run once when registered
- watcher queue order and duplicate enqueue behavior are unchanged
- watcher execution remains immediate and synchronous on mutation
- `without watchers` behavior is unchanged
- collection mutators are unchanged
- GUI queued mutation timing is unchanged
- WebServer request/response queue behavior is unchanged
- language `=` and `values_equal()` are unchanged

Intentional behavior change:

- assignment of a storage-equal value no longer notifies watchers

Known remaining limitations:

- collection mutators are still not uniform; `append`/`prepend` and
  `take_first`/`take_last` use lvalue-aware notification, while `insert`,
  `remove`, `remove_value`, `reverse`, `sort`, and `unique` still need the
  later mutator contract phase
- watcher queue duplicate entries are still possible
- watcher cycle exhaustion is still the old direct stderr limit, not a
  structured runtime error
- path matching is still one-way prefix matching

## Next Recommended Phase

Proceed to Watcher Hardening Phase 2: cascade deduplication within a single
watcher drain cycle.

Do not combine Phase 2 with mutator unification or structured cycle errors.
Keep the next change limited to queue pending-state management so any ordering
regressions are easy to isolate.
