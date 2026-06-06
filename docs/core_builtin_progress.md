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

- [x] `number(value)`
- [x] `boolean(value)`
- [x] `array(value)`
- [x] `record(value)`
- [x] example coverage
- [x] negative coverage

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

- [x] `replace(text, from, to)`
- [x] `starts_with(text, prefix)`
- [x] `ends_with(text, suffix)`
- [x] `repeat(text, count)`
- [x] example coverage
- [x] negative coverage

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

- [x] `keys(record)`
- [x] `values(record)`
- [x] `has(record, key)`
- [x] `remove_key(record, key)`
- [x] example coverage
- [x] negative coverage

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

- [x] `count(value)`
- [x] example coverage
- [x] negative coverage

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

- [x] `examples/type_builtin_test.bas`
- [x] `examples/conversion_builtin_test.bas`
- [x] `examples/string_helpers_test.bas`
- [x] `examples/record_helpers_test.bas`
- [x] `examples/count_builtin_test.bas`
- [x] negative tests for invalid conversion and helper usage
- [x] `README.md`
- [x] `docs/reference.md`
- [x] `docs/tutorial.md`

Documentation explains the distinction between:

- `string(value)` - canonical string conversion for any value
- `number(value)` - strict string-to-number conversion
- `boolean(value)` - strict string-to-boolean conversion
- `array(value)` - JSON string decoding to arrays
- `record(value)` - JSON string decoding to records
- `encode(value)` - JSON serialization for structured data
- `decode(text)` - JSON parsing to recreate values  
- `quote(value)` - gBASIC source code literal with escaping

Documentation clarifies that these are core built-ins and do not require loading a library.

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

Phase 2 completed:

- registered new conversion function names in `src/builtins.c`
- added Phase 2 dispatch in `eval_call(...)` in `src/eval.c`
- implemented strict conversion with proper error handling
- reused existing `builtin_decode_text()` for `array()` and `record()` functions
- added `examples/conversion_builtin_test.bas` with positive coverage
- added negative test cases for all conversion functions
- all existing tests continue to pass

Verification results:

- `make clean && make` passed
- `./tests/run_examples.sh` passed
- `./tests/run_negative.sh` passed
- All Phase 2 functions work according to specification

Phase 3 completed:

- registered new string helper function names in `src/builtins.c`
- added Phase 3 dispatch in `eval_call(...)` in `src/eval.c`
- added `#include <math.h>` for integer validation in repeat()
- modified Makefile to include `-lm` math library for linking
- implemented strict argument validation for all functions
- added `examples/string_helpers_test.bas` with comprehensive positive coverage
- added negative test cases for all error conditions
- all existing tests continue to pass

Files changed for Phase 3:

- `src/builtins.c` - added function names to registry
- `src/eval.c` - added function implementations with strict validation
- `Makefile` - added `-lm` to LDLIBS for math library
- `examples/string_helpers_test.bas` - positive test cases
- `examples/string_helpers_test.out` - expected output
- `tests/negative_replace_empty_search.bas/.err` - empty search string error
- `tests/negative_replace_type.bas/.err` - wrong argument types
- `tests/negative_starts_with_type.bas/.err` - wrong argument types
- `tests/negative_ends_with_type.bas/.err` - wrong argument types  
- `tests/negative_repeat_type.bas/.err` - wrong count type
- `tests/negative_repeat_negative.bas/.err` - negative count
- `tests/negative_repeat_fractional.bas/.err` - fractional count

Verification results:

- `make clean && make` passed
- `./tests/run_examples.sh` passed
- `./tests/run_negative.sh` passed
- All Phase 3 functions work according to specification with strict validation

Phase 4 completed:

- registered new record helper function names in `src/builtins.c`
- added Phase 4 dispatch in `eval_call(...)` in `src/eval.c`
- implemented strict argument validation for all functions
- `keys()` returns array of key strings in record order
- `values()` returns array of record values in record order  
- `has()` returns boolean for key existence
- `remove_key()` returns new record without the specified key (no mutation)
- missing key in `remove_key()` returns copy of original record (no error)
- proper memory management for array and record creation
- added `examples/record_helpers_test.bas` with comprehensive positive coverage
- added negative test cases for all error conditions
- all existing tests continue to pass

Files changed for Phase 4:

- `src/builtins.c` - added function names to registry
- `src/eval.c` - added function implementations with strict validation
- `examples/record_helpers_test.bas` - positive test cases
- `examples/record_helpers_test.out` - expected output
- `tests/negative_keys_type.bas/.err` - keys() type validation
- `tests/negative_values_type.bas/.err` - values() type validation
- `tests/negative_has_record_type.bas/.err` - has() record type validation
- `tests/negative_has_key_type.bas/.err` - has() key type validation
- `tests/negative_remove_key_record_type.bas/.err` - remove_key() record type validation
- `tests/negative_remove_key_key_type.bas/.err` - remove_key() key type validation

Verification results:

- `make clean && make` passed
- `./tests/run_examples.sh` passed  
- `./tests/run_negative.sh` passed
- All Phase 4 functions work according to specification with proper immutability

Phase 5 completed:

- registered new count function name in `src/builtins.c`
- added Phase 5 dispatch in `eval_call(...)` in `src/eval.c`
- implemented strict type validation allowing only strings, arrays, and records
- `count()` returns string length using `strlen()` for strings
- `count()` returns element count for arrays and records
- proper error handling for unsupported types (numbers, booleans, nothing, unknown)
- proper arity validation (exactly one argument required)
- added `examples/count_builtin_test.bas` with comprehensive positive coverage
- added negative test cases for all error conditions
- all existing tests continue to pass

Files changed for Phase 5:

- `src/builtins.c` - added function name to registry
- `src/eval.c` - added function implementation with strict validation
- `examples/count_builtin_test.bas` - positive test cases
- `examples/count_builtin_test.out` - expected output
- `tests/negative_count_number.bas/.err` - number type error
- `tests/negative_count_boolean_true.bas/.err` - boolean true type error  
- `tests/negative_count_boolean_false.bas/.err` - boolean false type error
- `tests/negative_count_nothing.bas/.err` - nothing type error
- `tests/negative_count_unknown.bas/.err` - unknown type error
- `tests/negative_count_arity_zero.bas/.err` - zero argument arity error
- `tests/negative_count_arity_two.bas/.err` - two argument arity error

Verification results:

- `make clean && make` passed
- `./tests/run_examples.sh` passed  
- `./tests/run_negative.sh` passed
- All Phase 5 functionality works according to specification

Phase 6 completed:

- updated `README.md` with core builtin functions overview and examples
- updated `docs/reference.md` with comprehensive core builtin function documentation
- reorganized built-ins section with clear categorization and usage examples
- added detailed explanations of type inspection, strict conversion, string helpers, record helpers, and counting
- updated `docs/tutorial.md` with comprehensive core builtin functions tutorial section
- documented differences between core conversion functions and serialization functions
- clarified arithmetic behavior and strict type checking
- explained that core functions are always available without library loading
- maintained consistency across all documentation files

Files changed for Phase 6:

- `README.md` - added "Core Builtin Functions" section with examples
- `docs/reference.md` - comprehensive "Core Builtin Functions" section with detailed documentation
- `docs/tutorial.md` - added "Core Builtin Functions" tutorial section with practical examples
- `docs/core_builtin_progress.md` - marked all phases complete with final status

Verification results:

- Documentation is comprehensive and consistent across all files
- Examples demonstrate proper usage patterns
- Error conditions and type checking are clearly explained
- Distinctions between similar functions are clarified

## CORE BUILTIN EXPANSION COMPLETE

All phases of the core builtin expansion have been successfully implemented:

✅ **Phase 1:** Type inspection functions (8 functions)  
✅ **Phase 2:** Strict conversion functions (4 functions)  
✅ **Phase 3:** String helpers (4 functions)  
✅ **Phase 4:** Record helpers (4 functions)  
✅ **Phase 5:** Count function (1 function)  
✅ **Phase 6:** Documentation and consistency

**Total: 21 new core builtin functions** added to the gBASIC runtime, all with comprehensive test coverage, strict type validation, clear error messages, and complete documentation.

The core builtin function system provides a solid foundation for gBASIC programs with always-available utilities for type checking, data conversion, text processing, record manipulation, and counting operations.
