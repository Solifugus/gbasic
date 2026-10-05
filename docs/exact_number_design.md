# Exact numbers — design

**Status: Proposal.** None of this exists. Decided 2026-10-05 after costing the
alternatives; the costs below are measured, not estimated.

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

- An integer **literal** whose digits round-trip: the lexer parses to `int64`
  and confirms the text matches. `10000000000000001` becomes exact; `0.1` does
  not; `1e20` is a judgement recorded as open below.
- Builtins that **count**: `len`, `count`, `byte_count`, a found index, `epoch`.
  These are integers by nature and are exact for free.
- `number("…")` on integer text that fits.
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
- **Should `BIGINT` columns become exact numbers** rather than strings? It would
  make the two halves of the tree agree, and it is a visible behaviour change
  that belongs to its own increment with its own ruling.
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
