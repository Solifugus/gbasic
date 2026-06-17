# Date/Time Comparison Progress

Updated: 2026-06-16

## Summary

Phases 1 and 2 of `docs/datetime_comparison_plan.md` are complete.

Bare date/time comparison is now exact. The previous lower-of-two-precisions
comparison was removed from bare operators.

Precision-aware date/time comparison remains available only through explicit
lenses. Both current lens syntaxes continue to work:

```basic
a(day)= b
a {day}= b
```

Old comparison syntax was not removed. Watcher behavior was not changed.

Phase 2 verified that date/time comparison lenses truncate both operands to
the named precision before applying the exact comparison relation, across all
comparison operators.

## Files Changed

Production:

* `src/eval.c`

Tests:

* `examples/datetime_test.gb`
* `examples/datetime_test.out`
* `examples/datetime_exact_comparison_test.bas`
* `examples/datetime_exact_comparison_test.out`
* `examples/datetime_lens_operator_test.bas`
* `examples/datetime_lens_operator_test.out`
* `tests/negative_datetime_string_comparison.bas`
* `tests/negative_datetime_string_comparison.err`
* `tests/run_examples.sh`
* `tests/run_negative.sh`

Documentation:

* `docs/reference.md`
* `docs/datetime_comparison_progress.md`

## Comparison Behavior Changed

Bare equality now requires the same date/time fields and the same stored
precision.

These are no longer equal under bare `=`:

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"
```

Use an explicit lens for same-period comparison:

```basic
if d {day}= t then
    print("same day")
end if
```

Bare ordering now uses the strict weak ordering from the plan:

1. calendar date/time values sort before time-only values
2. represented start instant is compared first
3. stored precision breaks ties
4. less precise values sort before more precise values at the same start
   instant

Example ordering:

```text
2026
2026-01
2026-01-01
2026-01-01 00:00:00
```

Bare date/time-to-non-date/time comparison now raises a runtime error instead
of falling through to numeric coercion.

## Collection Behavior

`sort()` and `unique()` now accept date/time values and use the same exact
date/time equality and ordering as bare comparisons.

No collection-specific date/time rules were added. Same-day or same-month
collection behavior still requires explicit projection/lens-oriented code in a
future design.

`min()` and `max()` remain numeric-array builtins. Their behavior was not
expanded in this phase.

## Tests Changed

`examples/datetime_test.gb` now:

* expects bare same-day different precision to be unequal
* expects bare same-month different precision to be unequal
* uses `{day}` and `{month}` lenses for precision-aware comparison
* preserves ordinary date ordering coverage

Added `examples/datetime_exact_comparison_test.bas` covering:

* transitive exact equality
* bare same-day different precision is not equal
* bare same-year different precision is not equal
* the old non-transitive equality chain no longer exists
* explicit `{day}` and old `(day)` lens syntax both still work
* explicit `{month}` and old `(month)` lens syntax both still work
* exact instant ordering
* date/time `sort()` order
* date/time `unique()` behavior

Added `tests/negative_datetime_string_comparison.bas` covering:

* bare date/time-to-string comparison raises a runtime error

## Phase 2 Lens Coverage

Added `examples/datetime_lens_operator_test.bas` covering:

* `{day}=`, `{day}<`, `{day}<=`, `{day}>`, `{day}>=`, `{day}!=`,
  `{day}!<`, and `{day}!>`
* `{month}=` and `{month}<`
* `{year}=` and `{year}<`
* `{hour}=` and `{hour}<`
* `{minute}=` and `{minute}<`
* `{second}=` and `{second}<`
* bare exact comparison remains exact after lens checks
* old explicit `(day)` comparison syntax still works

No evaluator changes were required for Phase 2. The existing lens path already
applies `apply_datetime_lens_to_value()` to both operands and then compares the
truncated results through exact bare comparison.

## PostgreSQL Impact

PostgreSQL date/time value mapping was not changed.

Current implications:

* PostgreSQL `date` still maps to day precision.
* PostgreSQL `time` still maps to time-only date/time values.
* PostgreSQL `timestamp` still maps to second precision.
* PostgreSQL `timestamptz` still remains a string.
* Bare comparison between PostgreSQL `date` and `timestamp` values now follows
  exact language comparison after mapping.
* Same-day comparison between PostgreSQL date/time values should use an
  explicit lens.

`tests/postgres_integration.bas` did not require updates because it prints
date/time values but does not compare them.

## Verification Results

Passed:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webclient.sh
./tests/run_webserver.sh
```

PostgreSQL runner:

```sh
./tests/run_postgres.sh
```

Result:

```text
SKIP tests/postgres_integration.bas (set GBASIC_POSTGRES_TEST=1 and standard PG* connection variables)
```

## Next Recommended Phase

Remaining datetime comparison work:

* update any remaining tutorial examples that imply bare precision-aware
  comparison
* add PostgreSQL date/time comparison integration coverage when a configured
  PostgreSQL test environment is available
* decide whether `compare(a, operator, b)` needs an explicit source-level
  lens-capable helper form, or whether lens syntax remains the only public
  precision-aware comparison API
