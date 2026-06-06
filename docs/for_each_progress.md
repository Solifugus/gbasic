# For-Each Progress

Last verified: 2026-06-06

## Status

For-each is **complete for the current scoped implementation**.

Both supported forms iterate arrays:

```basic
for each item in items
    ...
end for
```

```basic
for item in items
    ...
end for
```

`continue` skips to the next array item. `break` exits only the nearest
for-each loop, and execution continues after `end for`.

Non-array sources are rejected with a location-aware runtime error:

```text
for in expects an array
```

Records can be traversed through `keys(record)` or `values(record)`, which
return arrays. Direct record iteration is intentionally invalid under the
current implementation.

## Files Changed

- `src/eval.c`: consume `break` inside `AST_STMT_FOR_EACH`, matching existing
  `while` loop control-flow handling.
- `src/lexer.c`: add `TOKEN_EACH` to `token_type_name()`.
- `tests/run_examples.sh`: register `examples/for_each_test.bas`.
- `tests/run_negative.sh`: register the number and record invalid-source cases.
- `examples/for_each_test.out`: include `Done`, proving execution continues
  after a for-each `break`.
- `tests/negative_for_each_number.err`: use current location-aware runtime
  error output.
- `tests/negative_for_each_record.err`: use current location-aware runtime
  error output.
- `docs/for_each_progress.md`: record final status and verification.

## Test Coverage

`examples/for_each_test.bas` verifies:

- normal `for each` array iteration
- compatible `for ... in` array iteration
- arrays returned by `keys(record)` and `values(record)`
- `continue` inside for-each
- `break` inside for-each
- execution after `end for`

Negative tests verify:

- `tests/negative_for_each_number.bas`: number source is rejected
- `tests/negative_for_each_record.bas`: direct record source is rejected

## Verification

Commands run on 2026-06-06:

- `make clean && make`: passed with no `TOKEN_EACH` warning.
- `./tests/run_examples.sh`: passed, including
  `examples/for_each_test.bas`.
- `./tests/run_negative.sh`: passed, including both for-each invalid-source
  cases.

No syntax redesign, unrelated runtime behavior, BAG GUI work, or other language
features were included.
