# Exact decimal numbers

**Status:** increment 1 shipped (2026-11-08, `tests/run_exact_decimal.sh`), with
the changes measurement forced recorded in §10; increments 2 and 3 open. Design
written 2026-11-08, from Matthew's ruling on book gap G50: *exact
decimal numbers*. A number written or read with a decimal point is exact, as
integers already are; `+`, `-` and `*` stay exact; division and functions
degrade to a double, as integer division already does. Increment 1 is the
language, increment 2 is `serialize` and G-SQL, and increment 3 is the database
drivers. Each can ship alone.

**Why:** gBASIC's exact numbers are exact integers (`exact_number_design.md`).
`{number}"6331.475"` is the nearest double, with nothing said, and arithmetic
on such values drifts: `6331.475 - 2182.855` is `4148.620000000001`, and
`412.34 - 412.3350` is `0.0049999999999954525`. Money is exact decimal
arithmetic, but only with a currency, so a quantity (`numeric(12,3)`), hours
(`decimal(6,2)`), a rate or a discount can't be carried exactly through gBASIC
or a G-SQL `number` column. `pg` and `odbc` hand `numeric` over as exact text,
and the first conversion to a number loses what they kept.

## 1. The representation (increment 1)

**A number is exact or inexact, as now, and an exact number has a scale.** An
exact number is `mantissa × 10^−scale`, the mantissa the `int64` the struct
already holds and the scale a new small integer. An exact integer is scale 0,
so every existing exact integer is unchanged, bit for bit.

- **Memory cost: none.** `num` is a double, an `int64` and an `int` (24 bytes);
  the union is 32 bytes because `DateTime` is, so a scale fits in the padding.
- **`type()` still answers `number`.** No new kind, for the reason
  `exact_number_design.md` gives: two numeric kinds need a promotion matrix,
  and its mistakes are silent.
- **The scale is at most 18,** the most an `int64` mantissa can carry with any
  integer part at all. A value that needs more degrades, as below.
- **Normalised:** trailing zeros are removed, so `1.50` is held as `15 × 10^−1`,
  and two equal exact values have the same representation.

## 2. Where exactness comes from

- **A decimal literal** (`6331.475`, `0.1`) is exact when its digits fit an
  `int64` with scale ≤ 18. An exponent literal (`1e-3`) stays inexact, as
  `1e20` already is: the notation means "approximately this magnitude".
- **Decimal text:** `number("6331.475")` and `{number}"6331.475"`, by the same
  rule; this is the door `pg`, `odbc` and `csv` values come through.
- **Not:** a function's result (`sqrt`, `log`, `pow` with a fractional exponent,
  the trigonometry gBASIC does not have) or a division. Those are inexact, as
  integer division is today (`6 / 2` is `3.0`, inexact).

## 3. Arithmetic

| operation | exact × exact | with an inexact operand |
|---|---|---|
| `+`, `-` | exact: rescale to the larger scale, then add | inexact |
| `*` | exact: multiply mantissas, add scales | inexact |
| `/`, `mod`, functions | inexact | inexact |
| unary `-` | exact | inexact |

**Overflow degrades with warning 2112,** as exact integers already do: a
mantissa past `int64`, or a scale past 18, gives the double and says so. It is
never refused: refusing would stop a program that answers today.

## 4. Comparison: the decision that changes answers

**Exact against exact is mathematical.** `0.1 + 0.2 = 0.3` is now **true**
(it is false today, in doubles), and `9007199254740993 = 9007199254740992.0`
stays false.

**Exact decimal against inexact compares as doubles.** The alternative, a
mathematical comparison, would make `sqrt(0.01) = 0.1` false. The double from
`sqrt` is the nearest double to 0.1, and the literal is now exactly 0.1, so
the two differ in the seventeenth digit, where today both are the same double
and equal. Comparing as doubles keeps every comparison a program makes today
between a computed value and a decimal literal. The cost is that exact
decimals compared with inexact values are only as exact as a double, which is
what they were before.

**Exact integers against inexact keep their mathematical rule,** since that is
shipped, and its reason (`2^53 + 1`) is about integers.

## 5. Printing and encoding

- **`print`, `string`, `encode` show the shortest decimal,** as they do now. An
  exact `4148.620` prints `4148.62`, which is what the double printed when it
  was right, so no golden moves for a value that was already correct. What
  changes is the values that were wrong: `4148.620000000001` prints `4148.62`.
- **`json_encode`** writes the same digits.

## 6. Increment 2: `serialize` and G-SQL

- **`serialize`:** the number's exactness byte is 0 (inexact) or 1 (exact
  integer) today. **2 means an exact decimal,** followed by its scale byte and
  its `int64` mantissa. A reader that does not know 2 refuses rather than
  guessing, so data written by 0.11 is refused by 0.10, never misread.
- **G-SQL `number` columns hold exact decimals** through that encoding.
- **The index key is the hard part.** A key encodes a number as a binary
  magnitude (`m × 2^e`), so exact `5` and double `5.0` share bytes and order
  agrees with `=`. Most decimals have no binary magnitude. The proposal: key a
  decimal by its nearest double's magnitude, then a tie-break suffix that
  orders the decimal against that double exactly. Distinct values that share a
  nearest double then sort by the suffix, and the suffix is empty for an
  integer or a double, so every existing key is unchanged. `run_gsql_key`'s
  memcmp-against-value-order oracle decides whether it holds, before anything
  else is built on it.

## 7. Increment 3: the drivers

`pg` and `odbc` return `numeric`/`decimal` as exact text, and they keep doing
so: text never loses digits, and changing what a column arrives as is a
change to every program reading it. What changes is that converting that text
(`number(row.qty)`) is now exact. A driver option to return such columns as
exact numbers directly is a later question, once increments 1 and 2 have been
used.

## 8. How it will be tested

- **An outside oracle,** as `run_exact.sh` uses python3's integers: Python's
  `decimal` module recomputes every `+ - *` over random operands at random
  scales, and the answer must agree digit for digit.
- **The comparison rule both ways:** `0.1 + 0.2 = 0.3` true, and
  `sqrt(0.01) = 0.1` still true, with each rule's failure shown by a
  perturbation.
- **The goldens:** every existing golden must pass unchanged (§5). A golden
  that moves is a value that was wrong, and each one is listed and checked.
- **Overflow:** degrading with 2112 at the scale and mantissa limits.
- **Increment 2:** `run_serialize`'s independent Python decoder, and
  `run_gsql_key`'s order oracle, before any column holds a decimal.

## 9. What it costs

Increment 1 touches number parsing (lexer and text), the arithmetic operators,
comparison, rendering, and the counting and picking builtins that preserve
exactness (`min`, `max`, `sum`, `abs`, `round`, `floor`, `ceil`, `sort`).
That is the list `exact_number_design.md` increment 5 swept for integers. Its
lesson applies: the rule is "a function that picks a value keeps its
exactness, one that calculates does not", not a list, because a new builtin
silently stays off a list.

## 10. What increment 1 changed in this design, by measurement

- **A decimal never becomes the exact integer by itself** (§1 said trailing
  zeros normalise away, which made `2.0` the integer 2). Normalisation stops at
  one place: `2.0` is held as `20 × 10^−1`, prints `2`, equals `2`, and is not
  `is_exact`. Measured, the other way broke three things, all because `1.0` is
  how code has said "a double": `frame.bas`'s `v * 1.0` signed 2^53 + 1 as
  2^53, `finance.bas`'s rate search (`hi = 1.0`) warned 2112 on `pow(2, 360)`,
  and `numfmt_dump.bas` carried a comment saying its `1.0` was load-bearing.
  Since a decimal is not `is_exact`, every path that knows only integers and
  doubles reads it as the double it always was; only the decimal-aware
  operators see more. `x * 1e0` is how to ask for a double now.
- **Overflow is loud only at 2^53** (§3 said every overflow warns 2112). A
  product past 18 places loses far fractional digits, which a double always
  lost, and compounding does it on every step: the first cut warned on
  `0.005 * pow(1.005, 12)` in `money_arithmetic_test`. At 2^53 and past it the
  double cannot hold every whole number, which is the integer overflow case.
- **`pow` to a whole power is exact** when it fits (§2 listed `pow` as
  inexact): it is repeated multiplication, and `pow(1.05, 3)` is finance's
  compounding. When it does not fit it is the double, silently, since `pow`
  was never promised exact.
- **`round` rounds in the digits**, half away from zero as the double path
  does, so `round(2.675, 2)` is 2.68 (it was 2.67, the double being below).
- **The `for` counter** is a mantissa at the largest scale of start, step and
  limit, so `step 0.1` reaches the limit exactly.
- **`-0.0` stays a double** (a zero with a point is not made a decimal), which
  is what keeps the sign `run_numfmt` pins.
- **`decode` and `json_decode`** give decimals too, by the integer helper's
  rule: they were the doors exact integers already came through.
