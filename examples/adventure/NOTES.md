# The Lantern Room Notes

This example uses only currently implemented gBASIC features.

Useful features used:

- `input(">")`
- `print(...)`
- assignment modifiers: `(trimmed)` and `(split)`
- arrays with `append`, `join`, and `find`
- functions
- `while` loops

Language gaps worked around:

- An earlier version used a function-local `loop:` label and `goto`, which exposed the need for a general `while` loop.
- `if` currently has no `else`, so command handling uses a `handled` flag and separate `if` blocks.
- There are no dictionaries/maps yet, so room navigation and descriptions are direct conditionals instead of data tables.
- There is no `break`/`continue`, so command dispatch remains intentionally linear.
- There is no case-normalization modifier in the example; commands are expected in lower case.
