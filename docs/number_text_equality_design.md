# gBASIC: a number compared with text that is a number

**Status:** Proposal, 2026-11-02, for Matthew's review before anything changes.
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

## 3. The routes follow, the watchers do not

`contains`, `find`, `remove_value`, `unique` and `consider` answer through the
operator (`values_equal`), so they follow it with no change of their own:
`contains(ids, form_value)` finds the id, which is the same trap one call away.
**Watchers do not**: they decide "changed" with storage equality, which compares
kinds, so `x` going from `1` to `"1"` is still a change. `sort` keeps its own
order across kinds.

The one surprise this buys, stated so it is a decision: `unique([1, "1"])` is
one value, the first.

## 4. Measured, 2026-11-02

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
the same number written as text**. The change costs those four assertions and
the `unique([1, "1"])` decision in §3.

## 5. For Matthew

1. Adopt §2: text that reads as a number equals that number in gBASIC too?
2. `=` against text that is no number stays false (where G-SQL refuses)?
3. The routes follow `=`, so `unique([1, "1"])` is one value; watchers still
   see `1` to `"1"` as a change?
