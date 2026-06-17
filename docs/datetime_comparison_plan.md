# Date/Time Comparison Development Plan

## Executive Summary

Replace the current precision-lenient bare date/time comparison with exact bare
comparison. Move same-day, same-month, same-hour, and similar behavior behind
explicit comparison lenses.

Current behavior:

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"
if d = t then
    print("same day")
end if
```

The comparison uses the lower precision of the two operands, so this prints.
That makes equality non-transitive.

Target behavior:

* Bare `=` is exact and transitive.
* Bare ordering is a strict weak ordering.
* Precision-aware comparison happens only through explicit lenses:

```basic
a {day}= b
a {day}< b
a {month}>= b
```

This plan does not redesign lens syntax beyond relying on
`docs/comparison_lens_syntax_plan.md` if that plan lands first.

## Current Implementation Evidence

Date/time values are first-class runtime values in `src/eval.c`:

* `VALUE_DATETIME`
* `DateTimePrecision`
* `DateTime`
* `parse_date_value()`
* `parse_time_value()`
* `datetime_lens_precision()`
* `apply_datetime_lens_to_value()`

Current comparison behavior lives in `eval_comparison()`:

```c
if (left.kind == VALUE_DATETIME && right.kind == VALUE_DATETIME) {
    DateTimePrecision precision =
        left.as.datetime.precision < right.as.datetime.precision
            ? left.as.datetime.precision
            : right.as.datetime.precision;
    ...
}
```

That block compares only the fields visible at the lower precision. It applies
to every bare comparison operator, not only `=`.

Date/time lenses already truncate both operands before recursively invoking
bare comparison:

```c
Value lensed_left = apply_datetime_lens_to_value(left, lens, ...);
Value lensed_right = apply_datetime_lens_to_value(right, lens, ...);
fake.as.binary.modifier = ast_modifier_none();
return eval_comparison(&fake, lensed_left, lensed_right);
```

After exact bare comparison lands, that recursive path is exactly what should
make lens comparison precision-aware and bare comparison exact.

## Recommended Exact Semantics

### Bare equality

Two date/time values are equal only when all of these match:

* both are date/time values
* both are either calendar date/time values or time-only values
* same represented start instant within that domain
* same stored precision

Examples:

```basic
2026-05-15 = 2026-05-15            ' true
2026-05-15 = 2026-05-15 00:00:00   ' false
2026-05 = 2026-05-01               ' false
14:30 = 14:30                      ' true
14:30 = 14:30:00                   ' false
```

This makes bare `=` a real equivalence relation for date/time values.

### Bare ordering

Use one shared internal comparator for date/time values. Suggested helper:

```c
static int datetime_compare_exact(DateTime left, DateTime right);
```

Recommended ordering:

1. Domain rank: calendar date/time values sort before time-only values.
2. Within a domain, compare represented start instant.
3. If the represented start instant is equal, compare stored precision.
4. Less precise values sort before more precise values at the same start
   instant.

The "represented start instant" is the normalized start of the stored value's
precision. Current storage already represents this:

* year: `YYYY-01-01 00:00:00`
* month: `YYYY-MM-01 00:00:00`
* day: `YYYY-MM-DD 00:00:00`
* hour: `YYYY-MM-DD HH:00:00`
* minute: `YYYY-MM-DD HH:MM:00`
* second: `YYYY-MM-DD HH:MM:SS`
* time-only hour: `HH:00:00`
* time-only minute: `HH:MM:00`
* time-only second: `HH:MM:SS`

Precision tie-break direction:

```text
year < month < day < hour < minute < second
```

at the same represented start instant. For example:

```text
2026 < 2026-01 < 2026-01-01 < 2026-01-01 00 < 2026-01-01 00:00 < 2026-01-01 00:00:00
```

Rationale:

* instant-first ordering keeps chronological order dominant
* precision tie-break makes distinct stored values deterministic
* less-precise-first reads naturally as broader period before narrower detail
* `a = b` is equivalent to comparator result `0`
* `sort()`, `unique()`, and future key usage can share one relation

### Lens comparison

Precision-aware comparison belongs only in lenses:

```basic
a {day}= b
a {day}< b
a {month}>= b
```

Lens truncation applies to both operands first. Then exact bare comparison
applies to the truncated results.

This gives expected same-period behavior without contaminating bare equality:

```basic
d(date)= "2026-05-15"
t(date)= "2026-05-15 12:05:03"

if d = t then              ' false after this plan
if d {day}= t then         ' true
if d {second}= t then      ' error: d lacks second precision
```

The existing `apply_datetime_lens_to_value()` already enforces sufficient
source precision. Keep that behavior.

## Cross-Type Comparison Recommendation

Date/time values should compare directly only with date/time values. Bare
date/time-to-string, date/time-to-number, and date/time-to-boolean comparisons
currently fall through to numeric coercion in some cases because
`value_number_or_zero()` returns `0` for non-number, non-boolean values.

Recommended behavior:

* date/time compared with non-date/time using bare operators is a runtime error
* date/time lens comparison may accept date/time strings because the existing
  lens helper explicitly parses date/time-like strings
* no implicit string parsing in bare comparison

This keeps bare comparison exact and prevents accidental truth from coercion.

## Collection Consistency

### Existing state

`sort()` and `unique()` currently do not support date/time values.

`unique()` accepts only:

* number
* string
* boolean
* `nothing`
* `unknown`

`sort()` accepts only:

* number
* string
* boolean
* `nothing`
* `unknown`

`find()`, `contains()`, `remove_value()`, and `find_by()` use
`values_equal()`, which delegates to `eval_comparison()` with bare `=`.
They therefore currently inherit precision-lenient date/time equality.

### Recommendation

Introduce shared internal date/time comparison helpers and use them
consistently:

```c
static int datetime_equal_exact(DateTime left, DateTime right);
static int datetime_compare_exact(DateTime left, DateTime right);
```

Then:

* `eval_comparison()` uses `datetime_compare_exact()`
* `values_equal()` inherits exact bare date/time equality automatically
* `find()`, `contains()`, `remove_value()`, and `find_by()` become exact for
  date/time values
* `unique()` should accept date/time values and use exact date/time equality
* `sort()` should accept homogeneous date/time arrays and use exact date/time
  ordering

This is preferable to leaving date/time unsupported in collections after
defining a strict weak ordering. It makes the equality and ordering contract
usable for future keys, set-like behavior, and deterministic sorting.

Do not add collection-specific precision rules. Users who want same-day
uniqueness or same-month grouping should map values through explicit lenses or
future projection helpers before calling collection operations.

## PostgreSQL Impact

PostgreSQL result mapping is in `src/eval.c`:

* `pg_result_value()`
* `pg_parse_datetime_result()`
* `pg_datetime_text()`
* `pg_parameter_text()`

Current mappings:

* PostgreSQL `date` OID `1082` becomes a day-precision date/time value.
* PostgreSQL `time` OID `1083` becomes a time-only date/time value.
* PostgreSQL `timestamp` OID `1114` becomes a second-precision date/time value.
* PostgreSQL `timestamptz` OID `1184` remains a string.
* PostgreSQL date/time parameters serialize from the stored date/time value.

No mapping change is required for exact bare comparison. The behavior change is
semantic after values enter the language.

Implications:

* A PostgreSQL `date` value and a PostgreSQL `timestamp` value on the same
  calendar day will no longer compare equal with bare `=`.
* Use `{day}` when comparing a `date` and `timestamp` by day.
* A PostgreSQL `timestamp` result and an equal second-precision gBASIC
  `datetime` value compare equal if their fields and precision match.
* PostgreSQL `time` values compare only within the time-only domain.
* `timestamptz` remains a string and is outside this plan.

Parameter serialization currently requires calendar date/time values to have
at least day precision for PostgreSQL date/timestamp parameters. Keep that
guard unchanged.

## Audit Findings

### `examples/datetime_test.gb`

Current file:

```basic
d (date)= "2026-05-15"
m (date)= "2026-05-15 12:05:03"
month (date)= "2026-05"

if d = m then
    print "same day"
end if

if month = d then
    print "same month"
end if
```

This intentionally depends on lower-of-two-precision bare equality. It must be
migrated.

Recommended new shape:

```basic
if d {day}= m then
    print "same day"
end if

if month {month}= d then
    print "same month"
end if
```

Also add exact negative checks:

```basic
if d != m then
    print "exact different precision"
end if

if month != d then
    print "exact different month precision"
end if
```

Keep the existing `later > d` check, but add a precision tie-break check such
as `d < m` or a clearer focused ordering example.

### `examples/datetime_lens_test.bas`

This already represents the target conceptual behavior, but it uses current
comparison-modifier syntax:

```basic
if d(day)= "2026-05-15" then
```

If the comparison-lens syntax plan lands first, migrate to:

```basic
if d {day}= "2026-05-15" then
```

If exact comparison lands before brace syntax, keep the old parenthesized
comparison syntax temporarily and migrate later with the syntax phase.

Add ordering checks:

```basic
if d {day}< later then
if d {month}>= start_of_month then
```

### PostgreSQL integration

`tests/postgres_integration.bas` currently prints a timestamp result but does
not compare date/time values.

Add PostgreSQL coverage when `GBASIC_POSTGRES_TEST=1`:

* selecting `date '2026-06-06'` and `timestamp '2026-06-06 12:34:56'`
* bare `=` is false
* `{day}` equality is true
* exact timestamp equality is true for matching second precision
* `time` values compare within the time-only domain

Do not require PostgreSQL server-side timezone behavior; `timestamptz` remains
string.

### Cross-type comparisons

Add negative tests for bare date/time compared to string or number:

```basic
d(date)= "2026-05-15"
if d = "2026-05-15" then
end if
```

Lens comparisons may continue to parse compatible strings:

```basic
if d {day}= "2026-05-15" then
```

### Ordering expectations

Add permanent examples for:

* exact equality same fields and same precision
* inequality same start instant but different precision
* chronological ordering by represented start instant
* tie-break ordering by precision
* time-only ordering
* domain ordering between calendar date/time and time-only values, if that
  comparison is allowed

Prefer making date/time-vs-time-only ordering defined rather than erroring,
because a strict weak ordering for date/time values is simpler to reuse in
`sort()` and future keys. Document the domain rank explicitly.

## Phase 1: Shared Exact Date/Time Comparator

### Goal

Introduce internal exact date/time equality and ordering helpers without
changing syntax.

### Files touched

Production:

* `src/eval.c`

Tests:

* no standard test migration yet unless the helpers are wired immediately

Documentation:

* implementation comments only in this phase

### Comparison functions touched

Add:

```c
static int datetime_compare_exact(DateTime left, DateTime right);
static int datetime_equal_exact(DateTime left, DateTime right);
static int comparison_result_from_cmp(const char *op, int cmp);
```

The exact comparator should:

* handle calendar date/time values and time-only values deterministically
* compare represented start instant before precision
* sort less precise before more precise at the same start instant
* return `0` only when the values are exactly equal under the target equality

### Tests affected

No user-visible behavior should change until the comparator is wired into
`eval_comparison()`.

### PostgreSQL implications

None in this phase.

### Migration strategy

Land helper code with focused internal use only if the project accepts helper
code before behavior changes. Otherwise combine this phase with Phase 2.

### Stop point

Stop when the code builds cleanly and the helper is isolated enough to reuse in
comparison, `sort()`, and `unique()`.

## Phase 2: Exact Bare Comparison

### Goal

Replace lower-of-two-precision comparison in `eval_comparison()` with exact
date/time comparison.

### Files touched

Production:

* `src/eval.c`

Tests:

* `examples/datetime_test.gb`
* `examples/datetime_test.out`
* new negative cross-type comparison tests
* possibly `tests/exploratory_precision_equality.bas`, promoted or replaced

Documentation:

* `docs/reference.md`
* `docs/implementation_validation.md`, if maintained as current evidence

### Comparison functions touched

Modify `eval_comparison()`:

* if both operands are `VALUE_DATETIME`, call `datetime_compare_exact()`
* map the comparator result through the same operator table as other ordered
  values
* if exactly one operand is `VALUE_DATETIME`, raise a runtime error for bare
  comparison

Do not change the date/time lens branch yet except as needed to keep it calling
back into exact bare comparison after lens truncation.

`compare(a, operator, b)` will automatically inherit the new behavior because
it delegates to `eval_comparison()`.

`values_equal()` will automatically inherit exact date/time equality because it
delegates to bare `=`.

### Tests affected

Update `datetime_test.gb` from bare same-day/same-month equality to explicit
lens equality and exact bare inequality checks.

Add coverage for non-transitivity removal:

```basic
year_value(date)= "2026"
may_value(date)= "2026-05-15"
june_value(date)= "2026-06-20"

if year_value != may_value then print("year differs from day")
if year_value != june_value then print("year differs from other day")
if may_value != june_value then print("days differ")
```

Add ordering coverage:

```basic
start(date)= "2026"
detail(date)= "2026-01-01 00:00:00"
if start < detail then print("less precise first")
```

Add negative tests for bare date/time-to-string and date/time-to-number
comparison.

### PostgreSQL implications

PostgreSQL values immediately follow exact language comparison. Existing
PostgreSQL integration output should not change because it currently prints
date/time values but does not compare them.

### Migration strategy

If brace comparison lenses are already implemented, migrate date/time examples
to `{day}`, `{month}`, etc.

If not, use the existing comparison modifier syntax temporarily:

```basic
if d(day)= m then
```

and leave syntax migration to `docs/comparison_lens_syntax_plan.md`.

### Stop point

Stop when exact bare comparison, lens comparison, non-transitivity regression,
and cross-type negative tests pass, and all standard suites pass.

## Phase 3: Lens-Only Precision-Aware Coverage

### Goal

Make explicit lenses the documented and tested way to perform precision-aware
date/time comparison.

### Files touched

Production:

* `src/eval.c`, only if lens behavior needs small fixes after exact bare
  comparison

Tests:

* `examples/datetime_lens_test.bas`
* `examples/datetime_lens_test.out`
* possibly `examples/datetime_test.gb`
* new lens ordering example if separate

Documentation:

* `docs/reference.md`
* `docs/tutorial.md`, if tutorial date/time examples mention comparison
  modifiers

### Comparison functions touched

Confirm `apply_datetime_lens_to_value()` still:

* accepts typed date/time values
* accepts parseable date/time strings
* rejects insufficient source precision
* rejects year/month/day lenses on time-only values
* truncates both operands before comparison

No lower-of-two-precision logic should remain in lens comparison. The lens
itself performs the truncation; exact comparison then compares the truncated
values.

### Tests affected

Add or update tests for:

* `{year}`
* `{month}`
* `{day}`
* `{hour}`
* `{minute}`
* `{second}`
* `=`, `<`, and `>=` with lenses
* insufficient precision errors
* time-only lens behavior

### PostgreSQL implications

Add PostgreSQL integration tests only if PostgreSQL is available in the
environment:

* `date` versus `timestamp` differs under bare `=`
* same values match under `{day}`
* exact timestamp result matches exact timestamp literal

### Migration strategy

Coordinate with the comparison lens syntax migration:

* target syntax: `a {day}= b`
* temporary syntax if needed: `a(day)= b`

Documentation should avoid presenting the old parenthesized comparison syntax
as canonical once brace syntax is implemented.

### Stop point

Stop when the only precision-aware date/time comparison behavior in tests and
docs is lens-driven.

## Phase 4: Collection Consistency

### Goal

Make collection helpers consistent with exact date/time equality and ordering.

### Files touched

Production:

* `src/eval.c`

Tests:

* `examples/array_unique_test.bas`
* `examples/array_unique_test.out`
* `examples/array_sort_test.bas`
* `examples/array_sort_test.out`
* negative sort/unique tests if mixed date/time domains are rejected

Documentation:

* `docs/reference.md`

### Comparison functions touched

Update:

* `value_unique_comparable()`
* `unique_values_equal()`
* `value_sort_comparable()`
* `array_all_sort_comparable()`
* `sort_value_compare()`

Recommended behavior:

* `unique()` accepts date/time values and uses exact equality
* `sort()` accepts homogeneous date/time arrays and uses exact ordering
* mixed ordinary types remain rejected as today
* `nothing` and `unknown` keep their existing rank ahead of ordinary values
* date/time-vs-time-only values may coexist in one date/time array because the
  comparator defines a domain rank

Do not add same-day or same-month collection behavior.

### Tests affected

Add `unique()` tests:

```basic
d1(date)= "2026-05-15"
d2(date)= "2026-05-15 00:00:00"
d3(date)= "2026-05-15"
vals = unique([d1, d2, d3])
print(len(vals))   ' 2
```

Add `sort()` tests:

```basic
y(date)= "2026"
m(date)= "2026-01"
d(date)= "2026-01-01"
s(date)= "2026-01-01 00:00:00"
vals = sort([s, d, y, m])
```

Expected order:

```text
2026
2026-01
2026-01-01
2026-01-01 00:00:00
```

### PostgreSQL implications

PostgreSQL date/time query results can now participate in `sort()` and
`unique()` using the same exact rules. Add integration coverage only if it does
not make the PostgreSQL suite too environment-sensitive.

### Migration strategy

Because date/time values were previously rejected by `sort()` and `unique()`,
this is an additive behavior change. Keep it separate from Phase 2 so any
collection regressions are easy to isolate.

### Stop point

Stop when array sort/unique examples pass, negative mixed-type behavior is
unchanged, and exact date/time collection behavior is documented.

## Phase 5: PostgreSQL Regression Coverage and Documentation

### Goal

Document PostgreSQL date/time comparison implications and add integration
coverage.

### Files touched

Tests:

* `tests/postgres_integration.bas`
* `tests/postgres_integration.out`

Documentation:

* `docs/postgres_design.md`
* `docs/reference.md`

Production:

* none expected

### Comparison functions touched

None expected.

### Tests affected

Extend the integration test with stable SQL literals:

```basic
pg_dates = pg.query(db, "select date '2026-06-06' as d, timestamp '2026-06-06 12:34:56' as t, time '12:34:56' as tm")
```

Verify:

* `pg_dates[0].d != pg_dates[0].t`
* `pg_dates[0].d {day}= pg_dates[0].t`
* timestamp exact equality with a matching gBASIC value
* time-only exact equality and precision inequality

If brace lenses are not implemented yet, use temporary parenthesized
comparison syntax in this phase and migrate later.

### PostgreSQL implications

Update PostgreSQL docs:

* `date` maps to day precision
* `time` maps to time-only precision
* `timestamp` maps to second precision
* bare comparison is exact
* precision-aware comparisons require lenses
* `timestamptz` remains string until timezone semantics exist

### Migration strategy

Keep PostgreSQL value mapping unchanged. Only tests and docs should change in
this phase unless a mapping bug is discovered.

### Stop point

Stop when PostgreSQL integration passes with `GBASIC_POSTGRES_TEST=1` and
skips cleanly otherwise.

## Verification

For every implementation phase, run:

```sh
make clean && make
./tests/run_examples.sh
./tests/run_negative.sh
./tests/run_webserver.sh
./tests/run_webclient.sh
```

Run PostgreSQL integration when available:

```sh
GBASIC_POSTGRES_TEST=1 ./tests/run_postgres.sh
```

If comparison-lens syntax changes are in the same branch, also run:

```sh
bison -Wall -Wcounterexamples -v -o /tmp/gbasic-parser-proof.c src/parser.y
```

## Migration Summary

Old meaning:

```basic
a = b        ' lower-of-two-precision comparison for date/time
a(day)= b    ' explicit day comparison, but same bare comparison underneath
```

New meaning:

```basic
a = b         ' exact date/time comparison
a {day}= b    ' day-truncated comparison of both operands
```

Migration rule:

* if code relied on same-period date/time comparison, add an explicit lens
* if code relied on exact timestamp comparison, bare `=` now expresses that
  correctly
* if code compared date/time values to strings, either parse the string through
  an assignment modifier first or use a lens where string parsing is explicitly
  supported

## Open Questions

* Should calendar date/time values sort before time-only values, as recommended
  here, or should comparing those two domains be a runtime error outside
  explicit normalization?
* Should `sort()` and `unique()` date/time support land with exact bare
  comparison, or remain a separate additive phase?
* Should date/time-to-string bare comparison become a runtime error immediately,
  or should that be a separate compatibility phase?
* Should `compare(a, operator, b)` gain an explicit lens-capable form such as
  `compare(a, "{day}", operator, b)`, or should user code rely on source-level
  lens syntax only?
* Should PostgreSQL `time` values preserve fractional seconds later, and if so,
  does `DateTimePrecision` need a subsecond precision level before that mapping
  changes?
