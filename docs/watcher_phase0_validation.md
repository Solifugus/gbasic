# Watcher Phase 0 Validation

This document validates the implementation assumptions needed before Watcher
Phase 1. It documents current source behavior only; no production source was
changed in this pass.

`docs/webserver_progress.md` is not present in the current checkout. WebServer
validation below uses `docs/webserver_design.md`, `docs/reference.md`,
`tests/webserver_integration.bas`, and `src/eval.c`.

## Executive Summary

The Phase 1 value-change guard should not use the existing `values_equal()`
helper. That helper implements language `=` by calling `eval_comparison()`, not
storage equality. It can run compare modifiers, raise runtime errors, and falls
back to numeric comparison for unsupported kinds. Arrays and records do not have
deep equality there.

The recommended Phase 1 predicate is a new pure storage comparator, implemented
near value copy/free helpers:

- arrays compare deeply, in order
- records compare deeply by field name, independent of field insertion order
- date/time values compare by exact stored date/time and precision
- strings, numbers, booleans, money, durations, files, directories, `nothing`,
  and `unknown` compare by stored value
- PostgreSQL connections compare by shared connection identity

No permanent tests found appear to rely on equal-value assignments triggering
watchers. GUI backend-originated value changes already suppress duplicate string
and boolean updates before enqueueing GUI mutations. WebServer request/response
handling relies on live array mutation, not duplicate same-value assignment.

Nested assignment has enough information to compare old and new values before
replacement. Collection mutators are not uniform yet; Phase 1 should limit the
new guard to assignment and leave mutator notification hardening for the planned
later phase.

The watcher queue limit can become a structured runtime error using the current
runtime error machinery, but doing so requires changing watcher dispatch
functions from `void` to status/error-propagating functions. That belongs in
the structured cycle error phase, not Phase 1.

## Current Equality Implementation

Relevant source:

- `src/eval.c:585` `value_copy()` deeply copies arrays and records.
- `src/eval.c:633` `value_free()` recursively frees arrays and records.
- `src/eval.c:704` `value_number_or_zero()` returns `0` for non-number,
  non-boolean values.
- `src/eval.c:4484` `values_equal()` builds a fake binary `=` expression and
  calls `eval_comparison()`.
- `src/eval.c:10428` `eval_comparison()` implements language comparison,
  including comparison modifiers and date/time lenses.
- `src/eval.c:10539` unsupported non-special comparisons fall back to
  `value_number_or_zero()`.

`values_equal()` is currently used for language-facing search helpers such as
array `find` and `remove_value`. It is not a storage-level equality function.
For arrays and records, language `=` does not perform structural comparison;
the current fallback can treat unrelated unsupported values as equal because
both convert to numeric zero.

`unique_values_equal()` and `sort_value_compare()` provide scalar-specific
rules for `unique()` and `sort()`, but they are deliberately narrower than a
general value comparator and reject nested arrays/records.

## Validation Answers

1. **What existing equality function should the value-change guard use?**

None as-is. Phase 1 should add a new pure storage equality helper rather than
using `values_equal()`. The closest reusable behavior is not a function but the
data traversal pattern in `value_copy()`/`value_free()` and the exact scalar
comparisons already used by `unique_values_equal()` and `sort_value_compare()`.

2. **Does equality currently support deep comparison of arrays and records?**

No. `value_copy()` supports deep aggregate traversal, but equality does not.
`values_equal()` delegates to language `eval_comparison()`, which has no array
or record branch and eventually falls back to numeric comparison.

3. **If not, what would be required?**

Add a side-effect-free helper such as:

```c
static int value_storage_equal(const Value *left, const Value *right);
```

It must compare kinds first, recurse through arrays, compare record field sets
by name, and compare each field value recursively. Records should not require
identical insertion order. The implementation should not allocate in the common
case, should not call `eval_comparison()`, and should not raise runtime errors.

4. **Should watcher value-change detection use deep equality or shallow equality?**

Use deep equality. gBASIC assignment stores values, and aggregate assignment
already uses deep copies when values are read. Users will reasonably expect:

```basic
state = {name:"Ada"}
state = {name:"Ada"}
```

to be unchanged for watcher purposes. Shallow equality would treat equivalent
fresh array/record literals as changes, causing surprising watcher churn and
making the guard weak for the common aggregate case.

5. **What current tests rely on equal-value assignments triggering watchers?**

No explicit permanent test was found that relies on equal-value assignment
triggering a watcher.

Existing watcher tests cover registration execution, ordinary changed
assignment, suppression, path matching, and nested array mutators:

- `examples/watch_test.gb`
- `examples/watch_path_test.bas`
- `examples/nested_array_mutation_test.bas`
- `tests/exploratory_watcher_ordering.bas`

These tests do not intentionally assign the same stored value to prove duplicate
watcher execution.

6. **Do GUI watchers rely on duplicate same-value assignments firing?**

No evidence was found. The GUI backend already suppresses duplicate
backend-originated string and boolean updates:

- `src/eval.c:1822` `gui_widget_set_string_field()` returns unchanged when the
  stored string already matches.
- `src/eval.c:1839` `gui_widget_set_bool_field()` returns unchanged when the
  stored boolean already matches.
- `src/eval.c:1856` and `src/eval.c:1876` enqueue GUI mutations only when those
  setters report a change.

GUI watcher examples (`examples/gui/watch_demo.bas`,
`examples/gui/calculator.bas`, `examples/gui/unit_converter.bas`) use watchers
to react to changed inputs/buttons and often reset button values to `false`.
They do not depend on duplicate same-value notifications.

7. **Does WebServer rely on duplicate same-value assignments firing?**

No evidence was found. WebServer uses live arrays:

- incoming requests are appended to `server.requests` and then trigger
  `watcher_trigger_change()` (`src/eval.c:7246`, `src/eval.c:7250`)
- application code removes requests with `take_first(server.requests)` and
  appends response records to `server.responses`
- native response consumption removes records internally with
  `webserver_array_remove()` and does not notify application watchers

`tests/webserver_integration.bas` watches `server.requests`, drains it with
`take_first`, and appends response records. The behavior depends on actual
array mutations, not on equal scalar assignments firing.

8. **Do nested array mutations have enough information to compare before/after values?**

For assignment, yes. `resolve_lvalue_ref()` returns a `Value *` for identifiers,
record fields, array elements, and record string indexes (`src/eval.c:11192`).
`assign_lvalue()` currently replaces the pointed-to value unconditionally
(`src/eval.c:11267`). Phase 1 can compare the existing `Value` with the new
`Value` before freeing/replacing it, including nested paths such as
`state.items[0]`.

For collection mutators, Phase 1 should not generalize the guard yet.
`append()`, `prepend()`, `take_first()`, and `take_last()` use lvalue-aware refs
and notify after mutation. `insert()`, `remove()`, `reverse()`, `sort()`, and
`unique()` still have older symbol-only or value-returning paths. They need the
separate unified mutator notification phase.

9. **Can the watcher queue limit become a structured runtime error without breaking current error handling?**

Yes, but not as a trivial text replacement. Current structured runtime errors
use `runtime_error_raise()`, `error_set_state()`, `error_generation`, and
`eval_error_result()` (`src/eval.c:498`, `src/eval.c:516`, `src/eval.c:546`).
The watcher limit currently prints directly to stderr and breaks the drain loop
(`src/eval.c:2611`), so it is neither catchable nor source-structured.

To preserve error handling, `watcher_drain()` and `watcher_trigger_change()`
need to return status or otherwise propagate `error_generation` to assignment
and mutator call sites. The watcher queue must also be cleared consistently on
structured failure. This is feasible but should remain in the planned cycle
error phase.

10. **Which tests should be added before Phase 1?**

Add permanent output-checked watcher tests before or with Phase 1:

- registration still runs once on watcher declaration
- equal scalar assignment does not trigger
- unequal scalar assignment still triggers
- equal array literal assignment does not trigger
- changed array assignment triggers once
- equal record assignment does not trigger, including same fields in different
  insertion order if record-order-insensitive equality is accepted
- changed nested field or array element triggers
- exact date/time same value and precision does not trigger
- date/time same instant with different precision does trigger
- `without watchers` remains suppression, not deferred notification

Run existing GUI and WebServer suites after Phase 1. No new GUI-specific
automated test is recommended until GUI test automation exists.

## Affected Files And Functions

Expected Phase 1 production scope:

- `src/eval.c`
  - add `value_storage_equal()`
  - possibly add record helper logic near `record_find()`
  - change `assign_lvalue()` to report changed/unchanged/error
  - change assignment handling in `eval_stmt()` to call
    `watcher_trigger_change()` only when `assign_lvalue()` reports changed

Expected Phase 1 tests:

- add `examples/watcher_value_change_guard_test.bas`
- add `examples/watcher_value_change_guard_test.out`
- register the new example in `tests/run_examples.sh`

Do not change collection mutator semantics in Phase 1. That belongs to the
later unified mutator notification contract.

## Risks

- Reusing `values_equal()` would produce incorrect aggregate behavior and could
  raise language runtime errors while deciding whether to notify watchers.
- Deep equality adds O(n) cost to assignment for aggregates. This is acceptable
  for Phase 1 because it is limited to assignments and avoids the larger cost
  and complexity of copying old values for every mutator.
- Record equality needs a deliberate field-order rule. Order-insensitive field
  matching is less surprising but requires name lookup per field unless a more
  complex comparison structure is added.
- The current language comparison fallback for arrays/records remains a
  separate issue. Phase 1 should not change language `=` semantics.
- Watcher errors raised during watcher execution are not fully propagated by the
  current `void` watcher dispatch API. Do not mix the value-change guard with
  structured watcher-cycle work.

## Recommended Phase 1 Scope

Implement only assignment value-change guarding:

1. Add pure deep `value_storage_equal()`.
2. Refactor `assign_lvalue()` to compare the existing stored value before
   replacement and return changed/unchanged/error.
3. Preserve assignment modifier behavior by comparing after the assignment
   modifier has produced the stored value.
4. Notify watchers only on changed assignment.
5. Keep watcher registration, immediate synchronous execution, `without
   watchers`, GUI queue timing, WebServer request/response queues, and
   collection mutators unchanged.
6. Add the focused regression tests listed above.

Stop after the new assignment guard and tests pass. Do not implement cascade
deduplication, structured cycle errors, mutator unification, or symmetric prefix
notification in Phase 1.

## Verification

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
- `./tests/run_examples.sh`: pass
- `./tests/run_negative.sh`: pass when rerun sequentially
- `./tests/run_webclient.sh`: pass when rerun sequentially
- `./tests/run_webserver.sh`: pass

Note: an initial parallel attempt ran `run_examples.sh` concurrently with other
suites. Because `run_examples.sh` performs `make clean`, the negative and
WebClient suites briefly failed with `./gbasic: No such file or directory`.
Sequential reruns passed.
