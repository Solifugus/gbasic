# gBASIC bitwise operations — design proposal

**Status:** IMPLEMENTED (2026-06-30). Approved as recommended — 32-bit unsigned,
raise-on-bad-input, builtins-first. All eight builtins shipped (`band bor bxor
bnot shl shr rotl rotr`), tested in `examples/bitwise_test.bas` plus five negative
domain cases. Operators (`& | ^ ~ << >>`) remain deferred sugar. This came out of
the crypto work: crypto itself does *not* need language bitwise ops (it binds
libcrypto), but bitwise ops are independently useful and gBASIC had none.

## 0. Motivation (why, given crypto is covered)

Bitwise ops are a general language feature, not a crypto enabler. gBASIC already
has the pieces that make them pay off: binary-safe strings with `byte_at` /
`from_bytes`, so the natural next step is bit-level work on the bytes:

- packing/unpacking binary file and network formats (headers, flags fields)
- permission / option bit masks
- color packing (RGBA ↔ int), fixed-point tricks
- non-cryptographic hashing and checksums (FNV, CRC)

It is explicitly **not** meant for rolling cryptographic primitives — that stays
in libcrypto (see `docs/crypto_design.md`).

## 1. Decision A — the integer model (the crux)

gBASIC numbers are IEEE-754 doubles. Integers are exact only up to 2^53, and
doubles have no bit representation to "operate on" directly — so any bitwise op
must define an integer domain and a width.

Three candidates:

| Model | Exact in double? | Width for `~`/rotate/shift | Familiarity |
|---|---|---|---|
| **32-bit unsigned** | yes (32 ≤ 53) | 32, unambiguous | high (C `uint32`, colors, CRC32, protocols) |
| 53-bit unsigned | yes | 53 is odd/unusual | low |
| 64-bit | **no** (>2^53 loses bits) | 64 | high but **unrepresentable** |

**Recommendation: 32-bit unsigned.** It is the only choice that is both exact in
a double and has a familiar, unambiguous width. `and`/`or`/`xor` of two uint32s
stay in range; `not`, shifts, and rotations are defined mod 2^32; everything
round-trips through a double losslessly. It matches the overwhelming majority of
real bit-twiddling (bytes, 16/32-bit words, colors, checksums).

Cost: no native 64-bit masks. If a concrete need appears later, add explicit
`band64`/… over the [0, 2^53) range — but don't pay for it up front.

## 2. Decision B — domain / error behavior

Consistent with the rest of gBASIC (builtins raise a runtime error on bad input
rather than silently coercing):

- Operands must be **integers in [0, 2^32)**. Non-integer, negative, or
  out-of-range → runtime error (no JS-style silent truncation, which is a
  notorious footgun).
- Shift/rotate count `n` must be an integer in **[0, 31]**; otherwise error.
  (No "shift by 32 = 0 or = undefined" ambiguity.)
- Results are always in [0, 2^32); left shift **wraps mod 2^32** (documented),
  since that is the point of a fixed-width model. `shr` is logical (zero-fill).

Rationale: errors surface bugs early; the alternative (silent wrap on input,
platform-dependent results) is exactly what makes bit code fragile.

## 3. Decision C — surface: builtins vs. operators

Two ways to expose them:

- **Builtins:** `band(a,b) bor(a,b) bxor(a,b) bnot(a) shl(a,n) shr(a,n)
  rotl(a,n) rotr(a,n)`. Zero grammar risk — added exactly like the crypto/math
  builtins (name in `builtins.c`, dispatch in `eval.c`). Self-documenting.
  Verbose when nested: `band(bor(a,b),c)`.
- **Operators:** `a & b`, `a | b`, `a ^ b`, `~a`, `a << n`, `a >> n`. Ergonomic,
  but needs bison grammar + precedence work. Lexer status (checked): `&` `|`
  `^` `~` are **free**; `<<`/`>>` are addable via `match()` (bare `<`/`>` are
  taken by comparisons). Precedence would slot below comparison, above nothing
  obvious — and gBASIC uses word operators (`and`/`or`) for logic, so symbolic
  bitwise operators wouldn't collide semantically but would be a new visual
  style for the language.

**Recommendation: ship builtins first.** It matches how every other primitive in
gBASIC is done, carries no parser risk, and lets the 32-bit semantics prove out
in real use. Revisit operators as pure sugar afterward *if* they earn it — the
symbols are available, so nothing is foreclosed.

## 4. Proposed builtin set (if approved as above)

```
band(a, b)   bitwise AND            (uint32)
bor(a, b)    bitwise OR             (uint32)
bxor(a, b)   bitwise XOR            (uint32)
bnot(a)      bitwise NOT            (~a mod 2^32)
shl(a, n)    logical left shift     (a << n, mod 2^32; n in 0..31)
shr(a, n)    logical right shift    (a >> n, zero-fill; n in 0..31)
rotl(a, n)   rotate left            (32-bit; n in 0..31)
rotr(a, n)   rotate right           (32-bit; n in 0..31)
```

Each raises a runtime error on a non-integer / negative / out-of-range operand or
shift count. Testing mirrors the existing pattern: a golden `examples/bitwise_test.bas`
(known vectors — masks, packing a color, a small FNV-1a hash to exercise xor+
wrapping-multiply-free composition) plus negative cases for the domain errors.

## 5. Open question for the user

Approve **32-bit unsigned + raise-on-bad-domain + builtins-first**? If yes, this
is a small, self-contained change (no grammar work). If you'd rather have the
operators too, or a wider integer model, say so and this note gets revised before
any code is written.
