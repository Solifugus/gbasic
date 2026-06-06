# Core Builtin Progress

## Overall Goal

Add a small set of always-available core built-in functions to the runtime for:

- type inspection
- strict type conversion
- common string helpers
- record helpers
- `count(value)`

These functions should be part of the runtime and should not require loading a library.

## Design Rules

- Do not weaken strict arithmetic.
- Do not reintroduce implicit string-to-number arithmetic.
- Keep the current `+` behavior:
  - if either operand is a string, convert both operands with canonical string conversion and concatenate
  - otherwise perform strict numeric addition
- `-`, `*`, and `/` must remain strict numeric arithmetic only.
- Conversion functions should be explicit and strict.
- Prefer clear runtime errors over surprising coercions.
- Keep implementation small, orthogonal, and test-driven.
- Work in reasonably sized bites and update this file after each bite.

## Phased Checklist

### Phase 1: Type Inspection

- [x] `type(value)`
- [x] `is_string(value)`
- [x] `is_number(value)`
- [x] `is_boolean(value)`
- [x] `is_array(value)`
- [x] `is_record(value)`
- [x] `is_nothing(value)`
- [x] `is_unknown(value)`
- [x] example coverage
- [x] negative coverage

Expected examples:

- `type(55)` returns `"number"`
- `type("hello")` returns `"string"`
- `type(true)` returns `"boolean"`
- `type([1, 2])` returns `"array"`
- `type({x:1})` returns `"record"`
- `type(nothing)` returns `"nothing"`
- `type(unknown)` returns `"unknown"`

Each `is_*` function returns `true` or `false`.

### Phase 2: Strict Conversion

- [ ] `number(value)`
- [ ] `boolean(value)`
- [ ] `array(value)`
- [ ] `record(value)`
- [ ] example coverage
- [ ] negative coverage

Rules:

- `number("55")` returns `55`
- `number("55.5")` returns `55.5`
- `number("abc")` runtime error
- `number(true)` runtime error unless language policy changes explicitly
- `number(nothing)` runtime error
- `boolean("true")` returns `true`
- `boolean("false")` returns `false`
- `boolean("maybe")` runtime error
- `boolean(1)` runtime error unless language policy changes explicitly
- `array("[1, 2, 3]")` returns `[1, 2, 3]`
- `array("{x:1}")` runtime error because decoded value is not an array
- `array("hello")` runtime error
- `record("{x:1}")` returns `{x:1}`
- `record("[1,2]")` runtime error because decoded value is not a record
- `record("hello")` runtime error

Implementation preference:

- `array(value)` and `record(value)` may use `decode(value)` internally when `value` is a string, then verify the decoded type.
- If `value` is already the requested type, returning it unchanged is acceptable.
- Do not make `array()` or `record()` invent wrapper structures around scalar values.

### Phase 3: String Helpers

- [ ] `replace(text, from, to)`
- [ ] `starts_with(text, prefix)`
- [ ] `ends_with(text, suffix)`
- [ ] `repeat(text, count)`
- [ ] example coverage
- [ ] negative coverage

Rules:

- `replace("hello", "l", "x")` returns `"hexxo"`
- `starts_with("hello", "he")` returns `true`
- `ends_with("hello", "lo")` returns `true`
- `repeat("ha", 3)` returns `"hahaha"`

All arguments should be strict:

- `text`, `from`, `to`, `prefix`, and `suffix` must be strings
- `repeat` count must be a number, ideally a non-negative integer
- invalid arguments should produce runtime errors

### Phase 4: Record Helpers

- [ ] `keys(record)`
- [ ] `values(record)`
- [ ] `has(record, key)`
- [ ] `remove_key(record, key)`
- [ ] example coverage
- [ ] negative coverage

Rules:

- `keys({x:1, y:2})` returns `["x", "y"]`
- `values({x:1, y:2})` returns `[1, 2]`
- `has({x:1}, "x")` returns `true`
- `has({x:1}, "z")` returns `false`
- `remove_key({x:1, y:2}, "x")` returns `{y:2}`

Design preference:

- `remove_key` should return a modified copy, not mutate the original record.
- key arguments should be strings.
- non-record values should produce runtime errors.

### Phase 5: Count

- [ ] `count(value)`
- [ ] example coverage
- [ ] negative coverage

Rules:

- `count("hello")` returns `5`
- `count([1,2,3])` returns `3`
- `count({x:1, y:2})` returns `2`

Errors on:

- `count(55)`
- `count(true)`
- `count(nothing)`
- `count(unknown)`

### Phase 6: Tests And Documentation

- [ ] `examples/type_builtin_test.bas`
- [ ] `examples/conversion_builtin_test.bas`
- [ ] `examples/string_helpers_test.bas`
- [ ] `examples/record_helpers_test.bas`
- [ ] `examples/count_builtin_test.bas`
- [ ] negative tests for invalid conversion and helper usage
- [ ] `README.md`
- [ ] `docs/reference.md`
- [ ] `docs/tutorial.md`

Documentation should explain the distinction between:

- `string(value)`
- `number(value)`
- `boolean(value)`
- `array(value)`
- `record(value)`
- `encode(value)`
- `decode(text)`
- `quote(value)`

It should also explain that these are core built-ins and do not require loading a library.

## Implementation Notes

Phase 1 completed:

- registered new core built-in names in `src/builtins.c`
- added Phase 1 dispatch in `eval_call(...)` in `src/eval.c`
- added a small public type-name mapping helper for `type(value)`
- kept the implementation limited to one-argument type inspection only
- did not change arithmetic semantics, GUI behavior, parser behavior, or conversion policy

Files changed for Phase 1:

- `docs/core_builtin_progress.md`
- `src/builtins.c`
- `src/eval.c`
- `examples/type_builtin_test.bas`
- `examples/type_builtin_test.out`
- `tests/negative_type_builtin_arity.bas`
- `tests/negative_type_builtin_arity.err`
- `tests/run_examples.sh`
- `tests/run_negative.sh`

Relevant runtime shape:

- built-in function names are registered in `src/builtins.c`
- built-in dispatch lives in `eval_call(...)` in `src/eval.c`
- runtime values are tagged by `ValueKind`
- `type(value)` uses a dedicated public mapping rather than exposing internal kind names directly
- negative tests compare stderr exactly, so any diagnostic shape changes must be intentional

## Tests Added

Added in Phase 1:

- `examples/type_builtin_test.bas`
- `examples/type_builtin_test.out`
- `tests/negative_type_builtin_arity.bas`
- `tests/negative_type_builtin_arity.err`

Coverage added:

- positive coverage for `type(...)` on number, string, boolean, array, record, `nothing`, and `unknown`
- positive coverage for every `is_*` builtin returning `true` and `false` cases
- negative coverage for `type()` arity validation

## Current Status / Next Bite

Current status:

- progress doc created and updated
- runtime inspected
- Phase 1 implemented and verified

Verification results:

- `make clean && make` passed
- `./tests/run_examples.sh` passed
- `./tests/run_negative.sh` passed

Recommended next bite:

1. Implement Phase 2 strict conversion only: `number(value)`, `boolean(value)`, `array(value)`, and `record(value)`.
2. Keep conversion explicit and strict, with clear runtime errors on invalid input.
3. Reuse existing decode/serialization machinery for `array(...)` and `record(...)` where practical.
4. Add focused example and negative tests before touching later helper phases.
