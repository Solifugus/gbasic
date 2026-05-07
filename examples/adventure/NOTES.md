# The Lantern Room Notes

This example uses only currently implemented gBASIC features.

Useful features used:

- `input(">")`
- `print(...)`
- assignment modifiers: `(trimmed)` and `(split)`
- arrays with `append`, `join`, and `find`
- functions
- `while` loops
- `if ... else ... end if`

Language gaps worked around:

- An earlier version used a function-local `loop:` label and `goto`, which exposed the need for a general `while` loop.
- An earlier version used separate `if` blocks for false paths, which exposed the need for `else`.
- There are no dictionaries/maps yet, so room navigation and descriptions are direct conditionals instead of data tables.
- There is no `break`/`continue`, so command dispatch remains intentionally linear.
- There is no case-normalization modifier in the example; commands are expected in lower case.
