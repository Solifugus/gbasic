# gBASIC: a number compared with text that is a number

**Status:** Shipped 2026-11-03 as a WARNING, held by `run_equality.sh`'s
number-text tiers (`tests/number_text_equality_test.bas`). §2's conversion was
built 2026-11-02 and reverted the next day on Matthew's second ruling (§7);
§2-§6 are kept as the record of what was tried and why it was undone.
Asked for by gdash (its S4); Matthew: *"reasonable consistencies with gBASIC
would be good. Should we allow gBASIC to do the same?"*, after ruling that
G-SQL reads text as the other side's kind (`gsql_compare_conversion_design.md`).

## 1. Today

`1 = "1"` is **false**, silently, and `1 < "2"` is refused. That was a fix: a
string met a number at the numeric fallthrough and every string was ZERO, so
`0 = "stop"` was true (the comment above the string branch of
`eval_comparison`). The fix made the answer honest for a string that is no
number, and false for one that is.

The trap gdash names: a form value is text, a stored id is a number, they never
match, and nothing says why. G-SQL now answers that comparison; gBASIC still
says false.

## 2. Proposal

When one side of a comparison is a number and the other is text:

- **The text is read with the reader G-SQL uses** (trimmed; a number written
  plainly; hex, `inf`, separators refused). If it reads, the two are compared as
  numbers: `1 = "1"`, `1 = " 1 "`, `1.5 = "1.5"`, `2 < "10"` are true.
- **Text that is no number:** `=` answers **false** and `!=` true, as today,
  where G-SQL refuses. A program writes `if x = ""` and `if answer = 0` after
  `input()` legitimately, and "these are not equal" is the true answer; in a
  query a refusal catches a typo, in a program it would break correct code.
  Ordering against text that is no number stays refused.
- **Nothing else changes:** a boolean against text, a date against text, money
  against text are untouched (gBASIC has `{date}`, `{USD}` for those).

## 3. The operator reads, the routes do not

As proposed, `contains`, `find`, `remove_value`, `unique` and `consider` would
have followed the operator, since they answer through it (`values_equal`,
`values_equal_for_consider`), and `unique([1, "1"])` would have been one value.
**Matthew ruled against that collapse**, so each route answers a number against
text as "not the same value" before it asks the operator: `unique([1, "1"])` is
two values, `contains([1], "1")` and `find(["1"], 1)` find nothing, and
`consider "1"` does not take an `if 1` branch. Watchers already compare stored
kinds, so `x` going from `1` to `"1"` is a change. `compare(a, "=", b)` is the
operator and reads the text.

A compound's `=` compares stored values (`value_storage_equal`, which watchers
use), so `[1] = ["1"]` and `{a: 1} = {a: "1"}` are false. Only a scalar
comparison of a number with text reads the text.

`consider` is placed with the routes because the code already classes it as
one; it is the one placement Matthew's ruling did not name, so it is the one to
revisit if dispatch on a form value wants the number branch.

## 4. Measured before building, 2026-11-02

The whole gate run on a build that logged every comparison of a number with
text (the instrument was never committed): **97 comparisons**.

- **93 are with text that is no number**, 37 of them in `stdlib/rules.bas`
  (ordering against a rule's text operand, which it handles), 27 in one suite's
  scratch script, the rest in the equality suites. Under §2 every one answers as
  it does today: `=` false, ordering refused.
- **4 are with text that is a number**, and all 4 are assertions written to pin
  today's rule: `tests/equality_scalar_test.bas:56-57` (`1 = "1"` and
  `0 = "0"` are false, "no coercion, as before") and
  `tests/same_value/same_value_test.bas:72` (`contains(["1"], 1)` is false, run
  twice). They would flip, as a deliberate, listed rebaseline.

So **no program or library in the tree depends on a number being unequal to
the same number written as text**. Under the ruling only the two
`equality_scalar_test.bas` assertions flip (a listed rebaseline); the
`same_value` one is a route and still holds.

## 5. Rulings (Matthew, 2026-11-02)

1. Adopt §2: **yes**.
2. `=` against text that is no number stays false: **yes**.
3. `unique([1, "1"])` collapsing to one value: **no**, so the routes keep the
   kinds apart (§3).

## 6. Built

- **One reader.** `include/gb_number_text.h` holds the trim and the
  "is this a number written plainly" check that G-SQL's store and comparison
  conversions used; `sql_exec.c` now includes it, and `eval_comparison` calls it
  through `number_from_plain_text`, which reads integer text exactly as
  `number("...")` does.
- **Where.** In `eval_comparison`, after the modifiers and lenses and before
  the kind chain, so a lens still sees the text, and text that does not read is
  left for the string branch, which answers as before.
- **Evidence.** `tests/number_text_equality_test.bas`, 46 checks: answers by
  hand for reading, ordering, exactness past 2^53, text that is no number (hex,
  `inf`, `1,000`, `1 2`, blank, a NUL), the kinds that do not convert, and every
  route, each beside a control. Four perturbations, each red: no conversion
  (11 mismatches), `values_equal` converting (4: `unique`, `contains`), the
  consider route converting (1), and a reader that takes whatever `strtod`
  takes (2: hex read). Clean under valgrind.

## 7. Reverted, and a warning instead (Matthew, 2026-11-03)

Matthew asked the next morning whether `1 = "1"` was a good idea in gBASIC --
"my brain was in SQL mode". My answer was no, and the reasons are the record:

- **SQL can afford the conversion because one side declares a kind.** A
  column says what `'7'` should become; a gBASIC variable says nothing, so the
  conversion in gBASIC guessed.
- **Ruling 3 made it half a rule.** `1 = "1"` was true while
  `contains([1], "1")` and `[1] = ["1"]` were false, and `"1" = 1`, `1 = "1.0"`
  but `"1" != "1.0"`: equality stopped being transitive. That is more to
  remember, against `less-to-remember`, and gdash's own trap,
  `contains(ids, form_value)`, still missed in silence.
- **What gdash reported was the silence.** Matthew's standing rule for a silent
  fumble is to make it loud, not to make it succeed.

Ruled: **revert, and warn.** Built:

- `eval_comparison` converts nothing. `=` and `!=` between a number and text
  that the shared reader (`include/gb_number_text.h`, kept) reads as a number
  say **warning 2122**, naming both sides and the remedy, `number()` or
  `{number}`. Once per site. The answer is unchanged: unequal.
- **Silent**: text that is no number (`x = ""`, `answer = 0` after `input()`),
  every route (they answer a number against text before asking the operator),
  and compounds. Ordering refuses as it always did.
- **G-SQL keeps its conversion** (`gsql_compare_conversion_design.md`), where
  the column is the declared kind.
- **Evidence.** `tests/number_text_equality_test.bas`, 42 checks, each warning
  beside a silent control, plus a stderr tier: a loop of three says it once, at
  its line, and the program runs on. Three perturbations red: warning on every
  number-versus-text pair (10 failures: the silent controls), never warning,
  and the routes warning (`unique`, `contains`, `find`, `remove_value`).
  `tests/equality_scalar_test.bas`'s `1 = "1"` pair is back as it was.
