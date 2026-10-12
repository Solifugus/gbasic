# Exact numbers — design

**Status: SHIPPED, increments 1-3 (0.6.0, 2026-10-07), plus `number("…")` as a
source of exactness (2026-10-12); increments 4-5 open.**
Decided 2026-10-05 after costing the alternatives; the costs below are measured,
not estimated. Each section is annotated where building it contradicted what was
written here.

## The defect, and why it is this project's problem rather than a limitation

```
pow(2, 53) + 1   answers 9007199254740992      exit 0, no diagnostic
                 the answer is 9007199254740993
```

`number` is the only numeric kind and it is a double, so integer arithmetic goes
silently wrong above 2^53. That is the silent-wrong-answer class this tree
organises against everywhere else.

**What makes it a defect rather than a documented limit is internal.**
`run_odbc.sh`'s exactness tier inserts 2^53+1, reads it back as a *string*, and
asserts both that the digits survived **and** that a double would have changed
them — citing these same two numbers. `BIGINT` and `DECIMAL` columns are
returned as strings for exactly this reason. So gBASIC refuses to lose precision
when a value crosses a driver and loses it in its own arithmetic. One of those
two positions is wrong.

## Measured: nothing in this tree loses integer precision today

Instrumented at the single arithmetic site and swept across **every `.bas` in
`examples/` and `tests/`** — 1288 programs, the two recorder fixtures excluded
because they make live paid model calls:

```
programs run:    1288
INTPREC events:  0
distinct sites:  0
coverage ok:     1288 of 1288
```

Three things follow, and the third is a warning about the evidence rather than
about numbers:

- **The change is invisible in practice.** Nothing in the tree depends on the
  lossy behaviour and nothing currently reaches it, so exactness cannot break
  what is here.
- **A warning alone would fire on nothing**, which is what makes it affordable
  and also what makes it a poor deliverable on its own: it would sit silent
  until somebody wrote the arithmetic that needed it.
- **THE GATE CANNOT VALIDATE THIS WORK.** A corpus that produces zero events
  exercises none of the paths an exactness change touches, so the fixture is the
  *only* evidence increment 1 works — the lesson `run_inbox.sh` records, that a
  tier present in thirty-six suites measures nothing about a shape none of them
  exercises. Every increment below therefore ships with its own cases, and
  "the gate stayed green" is not a claim about any of them.

The sweep's own first run reported `0 events` having executed **8** of those
1288 programs, because `gbasic` reads stdin and the first fixture calling
`input()` consumed the loop's file list. It asserts a coverage floor now. A
probe that cannot fire is indistinguishable from one that found nothing, and
that is the second time the same shape appeared in one evening.

## What was rejected, and what it would have cost

**A new `integer` value kind.** Measured surface of the kinds added this way:
`VALUE_MONEY` 35 sites, `VALUE_DATETIME` 35, `VALUE_DURATION` 27 — so ~30, plus
a `SER_VERSION` bump, the actor wire, a 28th answer from `builtin_type_name`,
and the `TYPEKINDS` tripwire that derives that list from the source.

**The sites are not the cost; the semantics are.** `money` is cheap to live
beside a number because it is a *different kind of thing* — mixing them is
naturally refused, which is why PLAT-EQ could give it one `both` branch and one
`mixed` branch and be done. An `integer` and a `number` are both numbers, so
**every** expression mixing them needs a promotion rule, and a wrong promotion
rule is a silent wrong answer in precisely the class being closed. PLAT-EQ
needed three separate instalments to get comparison right across kinds that
already existed; this would add the one pair whose mistakes are invisible.

**Doing nothing is more defensible than it first looks** and is recorded here so
it is a rejected option rather than an unconsidered one: the things needing
exact large integers in business code are mostly **identifiers**, an identifier
is not a number you do arithmetic on, and that is already why ODBC hands back
strings. Exact currency is `money`, already int64. Digests are hex text. What it
genuinely fails is large counts and non-money sums above 2^53.

## The design: exactness is a property of a number, not a second type

One numeric kind. A `number` is **exact** when it is known to be an integer the
runtime can represent exactly, and **inexact** otherwise. This is Scheme's
exactness model and Lua 5.3's, not an invention.

**Memory cost is zero, measured.** `DateTime` is eight `int`s = 32 bytes, so the
`Value` union is already at least that. A numeric member of
`{ double; long long; flag }` is 24 bytes and fits inside it, so `sizeof(Value)`
does not move.

**Site cost is ~12–20, not the 219 that `as.number` appears at.** Those 219
reads keep working unchanged, because the double stays in sync as a lossy view
of an exact value. Only the places that must *preserve* exactness change: the
single arithmetic site, comparison, `format_number`, encode/serialize, the
lexer's literal scan, and whichever producers opt in. `value_number(double)`
goes on meaning *inexact*, which is the correct default for all 228 existing
construction sites, so nothing has to be audited to stay correct.

**`type()` still answers `number`.** No 28th kind, no promotion matrix, no
reference or tripwire churn.

### Where exactness comes from

**This section is the design, and for a while it was read as the status.** Of
the four sources below, increment 1 shipped **one**. Each now carries what it
actually does, because a list that mixes shipped and unshipped reads as neither.

- **SHIPPED.** An integer **literal** whose digits round-trip: the lexer parses
  to `int64` and confirms the text matches. `10000000000000001` becomes exact;
  `0.1` does not; `1e20` is settled below as **inexact**.
- **NOT SHIPPED — increment 5.** Builtins that **count**: `len`, `count`,
  `byte_count`, a found index, `epoch`. These are integers by nature and would be
  exact for free. Measured 2026-10-12: `len("ab") + 9007199254740991` answers
  ...992.
- **SHIPPED 2026-10-12**, and it was not before. `number("…")` on integer text
  that fits, and the `{number}` modifier with it. **This one is worth a
  paragraph**, because the gap was not merely an unfinished increment: the
  settled ruling on `1e20` below names `number("...")` as *the* way to ask for an
  exact integer, so a settled decision pointed at a remedy that silently handed
  back an inexact number — `number("9007199254740992") + 1` answered ...992.
  `tests/exact/exact_test.bas` even *stated* the remedy, in a comment, beside the
  exponent case, and asserted nothing about it. Found while checking a book
  chapter's claim against the tree rather than by any gate.

  Two things made it survive and both are recorded elsewhere in this project as
  lessons. **Below 2^53 it is invisible**: an exact and an inexact integer render
  identically through `string` and `encode`, so every value check passes on the
  broken build and only carrying the number through `+ 1` separates them — the
  fixture asserts arithmetic for that reason. And **there were two doors**: the
  `number()` call and the `{number}` modifier were two independent copies of
  `strtod`, so they now share one helper, and a tier asserts the modifier agrees.
  A fix reaching one door is the shape `webclient`'s read and write halves had.

  Narrower than `strtod`, each narrowing keeping a settled decision rather than
  being caution: decimal only (an exponent means "approximately this magnitude"
  whichever door it arrives through, which is the `1e20` ruling; `0x` is a C
  quirk of `strtod` that nothing documents), no whitespace tolerance beyond what
  each door already had, and out of `int64` range is inexact rather than an
  error, exactly as the literal rule has it.
- **Not** database columns yet. `BIGINT` returning a string is load-bearing
  today and changing it to a number is a separate, behaviour-visible decision.

### How operations propagate it

| | |
|---|---|
| `+` `-` `*`, both exact | exact when the result fits `int64`; otherwise **degrade to double and warn** |
| either operand inexact | inexact |
| `/` | **always inexact**, even `6 / 3`. `floor(a / b)` is already how gBASIC spells integer division |
| unary `-`, `mod` | preserve exactness |
| `pow`, both exact non-negative integers | exact by repeated multiplication when it fits |

**`pow` being exact is what makes the headline case right rather than merely
loud.** `pow(2,53)` is exact, `+ 1` is exact, and the answer is
**9007199254740993**. The warning is therefore *not* the fix — it retreats to
genuine `int64` overflow past ~9.2e18, which is far rarer than 2^53 and is a
real event worth a sentence.

**Overflow degrades rather than refusing**, deliberately. Refusing would raise
where a program currently answers, and "nothing that ran stops running" is the
rule a patch release is held to. Degrading keeps the old behaviour and makes the
loss loud, which is what the warning channel is for.

### Comparison, which is where a mistake would be invisible

- exact vs exact — integer comparison.
- exact vs inexact — compared **mathematically**, not by converting the exact
  one to a double. `9007199254740993 = 9007199254740992.0` must answer
  **false**, and the obvious implementation makes it true.
- This is the one rule whose failure produces no error and no visible symptom,
  so it gets its own fixture tier with the cases pinned by value.

### What becomes visible

`print` of an exact integer shows its exact digits, so a golden printing a
number above 2^53 moves — to the correct value. PLAT-NUMFMT's shortest
round-trip rendering is unchanged for inexact numbers. The count of affected
goldens is to be measured before the increment that changes rendering.

`encode`/JSON gains accuracy: an exact integer emits its digits, which JSON
represents in text without loss, and `decode` can return exact for integer
text — so a round trip preserves exactness rather than silently flattening it.

`serialize` and the actor wire need the flag and the `int64`, which is a
`SER_VERSION` bump. `money`'s phase 2 did one, and its v1-payload tier is the
pattern for proving the old format still reads.

## Open decisions

- **`1e20`** is integral and exactly representable. Is a literal in exponent
  notation exact? Leaning no — the notation says "approximately this
  magnitude", and `number("100000000000000000000")` is the way to ask for exact.
- ~~**Should `BIGINT` columns become exact numbers** rather than strings?~~
  **CLOSED, 2026-10-12: no.** Matthew's ruling, and the argument is worth keeping
  because the question looked *more* open for an hour and was in fact answered.

  `BIGINT` came back as text so its digits were not lost, and the evidence was
  that `number(the_string)` dropped the last one — `tests/odbc_test.bas` asserted
  exactly that, as the control proving the text return was not vacuous. With
  `number("…")` exact, every value a `BIGINT` can hold converts exactly, int64
  max included. So that half of the rationale is gone, and the first draft of
  this note said the decision had therefore got more live. **That was the wrong
  reading, in two ways.**

  **It mistook the demonstration for the reason.** This document's own
  "doing nothing is more defensible than it first looks" already says why the
  text return exists: *the things needing exact large integers in business code
  are mostly identifiers, an identifier is not a number you do arithmetic on,
  and that is already why ODBC hands back strings.* Precision loss was how that
  was demonstrated, never the argument. The argument is untouched.

  **And changing it would be a silent wrong answer in user code**, which is the
  class this whole document exists to close. Measured: `type(row.id)` moving from
  `string` to `number` flips `row.id = "9007199254740993"` from **true to false**
  — PLAT-EQ deliberately makes number-versus-string equality answer false rather
  than coerce — and turns `row.id > "…"` from a working comparison into a raise.
  A release that did that to fix a precision loss nobody now suffers would be a
  bad trade.

  **The better reading is that the arrangement was COMPLETED rather than
  weakened.** The string used to be a cage: faithful digits you could not get out
  without losing one. It is now a faithful representation *with a lossless door
  beside it*, and the conversion is the caller's explicit decision — right, since
  only the caller knows whether they hold an identifier or a quantity. That is
  the shape `money` reached by the same route, where fixing the entrance exposed
  the exit and `money.text` was added as it.

  Three independent reasons to keep text, then: `type()` stability, identifiers
  are not numbers, and `DECIMAL`/`NUMERIC` must stay text regardless — so
  converting `BIGINT` alone would make the module's rule *less* uniform, not
  more.
- **`mod` on a degraded operand** — the result is already wrong before `mod`
  sees it. Probably nothing to do beyond the warning at the degrade site.

## Increments

1. The representation plus `+ - *` and the overflow warning. No rendering
   change, so no goldens move; exactness is observable only through arithmetic.
2. Comparison, with the exact-versus-inexact tier.
3. Rendering and `encode`, which is the increment that moves goldens.
4. `serialize` / actor wire, with the old-payload tier.
5. Exact `pow`, `mod`, and the counting builtins.

Each is separately gateable, and increment 1 is the one that decides whether the
rest is worth it.

### SHIPPED 2026-10-07 — 1, 2 AND 3, because splitting 3 off would have been worse

`tests/run_exact.sh`, 50 self-checking assertions plus a python3 oracle over 183
values, the diagnostic channels, a `sizeof` tier and valgrind. Nine perturbations
proven red.

**INCREMENT 3 WAS FOLDED IN RATHER THAN DEFERRED, and the reason is a defect the
split would have created.** With arithmetic and comparison exact but rendering
still showing the double view, `print x` shows `9007199254740992` while
`x = 9007199254740993` answers `true` — so a program can **prove** a value its own
display contradicts. That is a new silent-wrong-answer shape and worse than the
loss it replaces, which at least had one story. The design asked for the affected
goldens to be measured before changing rendering; measured across every `.bas`,
`.gb` and `.out` in `examples/` and `tests/`, the integer tokens at or above 2^53
are hex key material inside string literals, base32 secrets, money text, and
`9007199254740994` (exactly representable, so unmoved). **Zero goldens moved.**

**THE DESIGN WAS WRONG ABOUT ONE COST, and it is the one that looked free.** It
said the 219 `as.number` reads "keep working unchanged", by overlaying a struct on
the bare `double` in the union. C11 6.5.2.3 permits inspecting the common initial
sequence of two **structures** in a union, and a bare scalar member is not a
structure — so that read would have been outside what the standard promises. The
219 sites were renamed to `as.num.value` instead, across six files, which the
compiler verifies completely: a `double` has no `.value`, so a missed site cannot
build. Memory cost is still zero and a tier now asserts it.

**A SITE EDITED BY MISTAKE, found by probing rather than by reading.** There are
two `case AST_EXPR_NUMBER:` in `src/eval.c` and the first version of this change
edited the wrong one — `server_literal_value` rather than `eval_expr`. **Nothing
failed.** Every exactness check simply stayed false, so the feature was inert and
the fixture read as a design error rather than a wrong edit; three separate
hypotheses were tried before instrumenting `value_is_exact` and seeing
`is_exact=0` arrive at the evaluator.

**AN EXISTING ORACLE TIER'S PREMISE WAS INVALIDATED, and it caught that itself.**
`run_numfmt.sh` recomputes 4070 values in awk and its stated premise is that both
languages perform the same correctly-rounded IEEE primitives and so land on the
identical double. Exact integer multiplication broke it: the `up7` recipe
(`y = 1`, then `y = y * 7`) became exact, so gBASIC produced the TRUE 7^19 =
11398895185373143 where awk produced the nearest double, and the tier reported 17
disagreements **in which gBASIC was right**. The fixture's seed is `1.0` now, with
the reason written beside it, because that tier's subject is the double FORMATTER
and a fraction in the seed says so. This is the gate doing its job rather than a
regression.

**The open decision on `1e20` is settled: an exponent literal is INEXACT.** The
notation says "approximately this magnitude", and `number("...")` is how to ask
for the integer. A perturbation making it exact is proven red.

**Still open:** `pow`/`mod`/counting builtins (increment 5 — so this release does
*not* fix the design's own headline example), `serialize`/the actor wire
(increment 4, needing a `SER_VERSION` bump), and whether `BIGINT` columns should
become exact numbers rather than strings, which is a visible behaviour change
deserving its own ruling.

**And `mod` is sharper than "still open" suggests.** Measured 2026-10-12:
`mod(9007199254740993, 2)` answers **0**. That is not lost precision, it is a
*wrong answer* — the exact odd literal degrades to a double before `mod` sees
it, and the double is even. The design table above says `mod` preserves
exactness; it does not yet, and the failure is the kind a reader would print in
a table without blinking. It is the strongest argument for increment 5 going
next, ahead of increments 4's serialization.
