# The money type (PLAT-MONEY)

Status: **ruled 2026-08-29, not yet implemented.** The governing requirement,
stated by the project owner: money must be **highly accurate and highly safe**.
Every ruling below resolves toward refusing an operation rather than returning
a number that might be wrong. This document records the
defects, the design ruling, and the phasing. Evidence is in `DOGFOOD.md`
(2026-08-29) and was raised by the gdash session, the type's first real
consumer; every claim below was re-verified against `src/eval.c` rather than
taken on trust.

---

## 1. What is already right

`money` is an exact `int64` of scaled integer units — `value_money(long long
cents)`. That is the correct representation and it is not what is being
changed. Demonstrated rather than asserted: `0.01` accumulated 1000 times is
exactly `10.00`, which a double cannot do.

**Everything below is about the boundaries of that core, not the core.**

## 2. The four defects

**1. There is no exact way to construct one.** `{USD}` takes a number, which is
already a double by the time the modifier sees it, and refuses text:

```
big {USD}= 92233720368547.75      ' -> 92233720368547.76, silently
b   {USD}= "92233720368547.75"    ' -> raises: USD modifier expects a number
```

The type's own int64 range is unreachable through its own constructor.

**2. `*` and `/` by a number leave integer arithmetic.** Both route through
`(double)left.as.cents * number` and then `round_to_cents(amount / 100.0)` — a
divide by 100 and a multiply by 100 in floating point, for an operation that
needs neither. This is the worst of the four: it corrupts a value the caller
already got right, with no error, and multiplying by an *integral* scalar is
not exact.

**3. Scale is hardcoded to cents.** `round_to_cents`, `format_money` and
`odbc_money_text` all assume `/ 100` and `% 100`. JPY (minor unit exponent 0)
and KWD/BHD/TND (exponent 3) have no correct representation, and neither does
anything below the cent.

**4. The rounding rule at `.5` is not well-defined.** `0.125 -> 0.13` but
`0.145 -> 0.14`. That looks like banker's rounding and is not: `round_to_cents`
is half-away-from-zero, and `0.145` as a double is `0.14499999999999999001`.
**The rule depends on the binary representation of the literal, not on the text
the author wrote.** This is the same wound as defect 1 — a double at the
entrance to an exact type — and an exact decimal-text constructor closes both.

### The interlock, which decides the order

**Defect 1 blocks the test for defect 2.** The double path only diverges above
2^53 units ($90,071,992,547,409.92 at cents scale), and no exact value that
large can be constructed from source while defect 1 stands. Attempted and
confirmed: the fixture has to assert on a value that is already wrong. So
exact construction ships *first*, or step 2 ships with a test that cannot
reach the range where it fails.

## 3. The ruling

**A money value carries its currency.** ISO 4217, as a numeric code, with the
minor-unit exponent derived from a table. Storage scale is
`exponent + 4 guard digits`.

Two objections were measured and both are gone:

- **Storage cost: none.** The `Value` union already holds a 32-byte `DateTime`;
  `{ int64 units; uint16 currency; }` is 16 bytes and fits inside the existing
  footprint. `Value` does not grow.
- **Namespace collision: none.** `modifier_resolve` (user-defined) is consulted
  *before* the built-in `USD` comparison, so a program that defines its own
  `CHF` modifier keeps it. Currency codes can be added as built-in modifiers
  safely.

`USD` today is a single hardcoded `strcmp` in the modifier dispatch. It becomes
one table lookup, and `{EUR}`, `{JPY}`, `{KWD}` arrive with it.

### Why guard digits

Interest, allocation, unit costs and FX all produce intermediate values below
the minor unit. Rounding each to the minor unit as it is produced loses money
across a multi-step calculation — measurably: compounding $100 monthly for a
year at cents scale gives `105.12` where the exact answer is `105.116…`.
Four guard digits below the minor unit keep the intermediates and round once,
at presentation.

### Ranges, to be stated in the reference rather than discovered

| Currency | Exponent | Storage scale | int64 range |
|---|---|---|---|
| USD, EUR, GBP | 2 | 6 | ±9.22 × 10^12 (±$9.22 trillion) |
| JPY | 0 | 4 | ±9.22 × 10^14 |
| KWD, BHD, TND | 3 | 7 | ±9.22 × 10^11 |

**Overflow must raise — ruled 2026-08-29, and this is new work the proposal
did not cost in.** The standing requirement for this type is *highly accurate
and highly safe*: where the two are in tension, safety wins, and a raise is
always preferred to a plausible wrong number.
Guard digits cut headroom by 10^4, so overflow becomes *reachable* where it was
not: $9.22 trillion is beyond most use but under some real aggregates (US
federal debt is roughly $35 trillion). C `int64` overflow wraps silently, which
would replace a rounding bug with a sign-flip bug — strictly worse. Every
arithmetic path needs a checked operation.

## 4. Arithmetic

| Operation | Rule |
|---|---|
| `money + money`, `money - money` | exact integer. **Currency mismatch raises.** |
| `money * integer` | exact integer, overflow-checked |
| `money * fraction` | rounds at the **guard scale**, not the minor unit |
| `money / number` | as above — the defined rounding rule at guard scale |
| `money * money` | stays refused (dollars² is meaningless) |
| `money = money` | different currencies are **not equal** — `false`, not a raise |
| `money < money` | different currencies **raise** |

The equality/ordering split follows the idiom PLAT-EQ already established for
compound values: equality answers a question about the values, ordering refuses
where no order exists. `USD 10 = EUR 10` is a legitimate question with the
answer "no"; `USD 10 < EUR 10` is not a question at all without a rate.

## 5. Construction and display

**Construction is by integer parse from decimal text. No double on the path.**
Digits beyond the storage scale are **rejected, not rounded** — confirmed as
the platform ruling, matching gdash's design §4 and the house idiom already
used by `odbc` (refuse what cannot be represented faithfully) and `encode`
(refuse a value whose text will not read back).

**Display is at currency precision**, and the rounding rule is **half-even by
default** — the usual financial choice, and unbiased. It is *not* the only rule
available: several tax jurisdictions mandate half-up, and hardcoding half-even
would make those uncomputable. Where rounding is explicit, the mode is
selectable.

## 6. Compatibility

Two facts, both verified:

- **`serialize`/`deserialize` are public builtins**, not internal actor
  plumbing, so serialized payloads persist on disk across binary versions.
- **The reader requires an exact `SER_VERSION` match.** A version bump would
  make v1 payloads fail with "not a valid serialized value" rather than
  migrate. So the reader must **accept v1 and rescale** (×10^4, currency = USD,
  the only one that existed), and write v2.

`odbc_money_text` (added 2026-08-29 in the ODBC module, `3828dbb`) divides by
100 and takes `% 100`; it becomes scale-driven. **The coordination risk gdash
flagged between this work and the ODBC module is resolved** — ODBC is committed
and pushed, so this is an edit to landed code rather than a merge conflict.

Roughly ten sites assume cents: `value_money`, `round_to_cents`, `format_money`,
`odbc_money_text`, the serializer and deserializer, truthiness, equality, the
arithmetic paths, and the number→money modifier.

## 7. Phasing

Each phase is shippable alone and each has a test that fails without it.

| Phase | Work | Why here |
|---|---|---|
| **0** | Exact construction from decimal text; excess digits rejected | Unblocks the test for phase 1. Small, and alone it makes the existing range reachable. |
| **1** | `*` and `/` stay in integer arithmetic; overflow raises | The silent-corruption fix. Write it **parameterized on scale** even though scale is still 2, so phase 2 does not rewrite it. |
| **2** | Currency tag, per-currency exponent, guard digits, `SER_VERSION` 2 | The representation change. Needed this ruling first. |
| **3** | FX: dated rates, `convert` | Depends on the tag. A rate is a *dated* fact — converting without an as-of date gives an unreproducible number, which is an audit problem, not an arithmetic one. |
| **4** | `finance` library: NPV, IRR, XIRR, PMT, PV, FV, RATE, NPER, amortization, depreciation | The actual business-operations gap. See §8. |

## 8. What this unlocks

`stats.bas` has 225 functions and its finance side is **securities analytics** —
returns, Sharpe, Sortino, drawdown, VaR, CVaR, CAPM, event studies, GARCH,
ARIMA. **The time value of money is entirely absent**: no NPV, IRR, XIRR, PMT,
PV, FV, RATE, NPER, no amortization schedule, no depreciation — not in
`stdlib/` and not in the xlsx formula engine either.

That is the gap for business operations. A dashboard or a line-of-business
application computes loan schedules, lease liabilities and capital budgets far
more often than it computes a Sharpe ratio. And an amortization schedule is
exactly where guard digits and allocation earn their place: a schedule whose
payments do not sum to the principal is wrong in a way a customer notices.

Allocation belongs here too — `100.00 / 3` currently gives `33.33`, and `× 3`
gives `99.99`. A splitting primitive whose parts provably sum back to the whole
is a phase-4 requirement, not a nicety.

## 9. Tests to pin

- exact construction at the top of the int64 range, per currency;
- excess decimals **refused**, with the message pinned;
- no precision loss through `*` and `/`, including above 2^53 units — the case
  phase 0 exists to make testable;
- guard digits **retained across a multi-step calculation**, rounded to
  presentation only at the end, compared against the exact expected result;
- the display rounding rule at the `.5` boundary, from decimal *text* so the
  test cannot pass by accident of binary representation;
- currency mismatch: `+` raises, `=` answers `false`, `<` raises;
- overflow raises rather than wraps;
- `SER_VERSION` 1 payloads still deserialize, rescaled, with a committed v1
  fixture — a migration nobody tested is a migration that does not work.

## 10. The currency table (ruled 2026-08-29)

**Built in, and extensible at run time.** ISO 4217 ships in the binary — all of
it, about 180 entries of `{code, numeric, exponent}`, a couple of KB. A short
list plus "a documented way to add one" is *more* design work, not less, and it
fails anyone outside the majors for no benefit.

On top of that, a program may **register** a currency: an internal scrip,
loyalty points, a historical currency, a redenomination that outpaces the
release cycle.

**Removal is not offered. Currencies become HISTORICAL instead.** Removing one
does not unmake the values that already exist, and archived data is precisely
what money is for — DEM and ITL must still deserialize in 2040. A historical
currency refuses *new* values and reads old ones.

### The decision that makes registration safe

**A serialized money value carries its exponent, not only its currency code.**

Without this, a value is only meaningful to a program holding the same table,
and gBASIC has two places where that bites:

- `serialize`/`deserialize` are **public builtins**, so values persist on disk.
  An archived value whose currency was registered by a program that no longer
  exists would be a numeric code at an unknown scale.
- **Actors are fork+exec — separate processes.** A currency registered in the
  parent is *not* registered in the child, so money sent to an actor would
  arrive uninterpretable.

Carrying the exponent makes the value self-describing: arithmetic and display
work anywhere, and only the display *name* needs the table. A value whose code
is unknown locally still computes correctly and renders as its code.

## 11. Construction is reflective (ruled 2026-08-29)

`{USD}` takes whatever it is given and does the most accurate conversion
available for that type, rather than accepting one type and refusing the rest.
Every path converges on the same operation — **exact integer parse of decimal
text**:

| Given | Path |
|---|---|
| string | parsed directly |
| number **literal** | the token's source text, which the lexer already retains |
| computed number | shortest-round-trip render (PLAT-NUMFMT), then parsed |
| integer | exact |
| money | a re-tag needs no rate; a *conversion* does — see §7 phase 3 |

**This dissolves defect 4.** The `0.145 -> 0.14` ambiguity existed because a
*binary* value was being rounded. `string(0.145)` is already `"0.145"` — the
shortest decimal that reads back identically — and parsing that text has no
binary representation left to be ambiguous about. Half-even then applies at
display, explicitly, where it can be seen and tested.

It also means **no breaking change**: `price {USD}= 19.95` keeps working and
becomes exact, so the phase-0 question of whether to deprecate the number form
does not arise.

### Excess decimals: the rule splits by origin

gdash's §4 asks that excess decimals be rejected. Confirmed, with a
distinction that keeps the type usable:

- **Authored text** — reject. `"1.23456789"` as USD is a value the author
  wrote that money cannot hold, and that is a bug in their input.
- **Computed** — round at the guard scale. `price * 1.08` produces seventeen
  digits and always will; refusing it would make money unusable for
  arithmetic. The guard digits exist so this rounding sits four places below
  the minor unit, where it cannot move a reported figure.

## 12. Open

- Registration syntax, and whether a registered currency needs a numeric code
  at all or can be identified by its alphabetic code alone.
- What `{EUR}` applied to an existing USD *money* value means: re-tagging
  (asserting the units were always euros) versus converting (needs a dated
  rate, §7 phase 3). These are different operations and should probably not
  share a spelling.
