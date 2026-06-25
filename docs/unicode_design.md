# Unicode and Binary-Safe Strings — Design

Status: **proposal** (2026-06-24). One of the three pre-freeze language threads
(with PBI — now complete — and multiprocessing). See `docs/pbi_design.md §9` for
how these threads share machinery.

## 1. Motivation

gBASIC strings are today a bare NUL-terminated `char *` with **no length**
(`src/eval.c`, `struct Value` → `as.string`; `value_string` copies via an
implicit `strlen`). Two distinct problems follow, and "good Unicode support"
means fixing both:

1. **Not binary-safe.** A NUL byte terminates the string, so `chr(0)` is a hard
   error (`eval.c` rejects code 0 explicitly) and raw/binary data cannot live in
   a string at all. `gbasic-design.md §526` lists this as a known gap.
2. **Not character-aware.** Every length/index operation is **byte-based**:
   `len`, `left`, `right`, `mid` all use `strlen` and raw byte offsets
   (`eval.c:11020–11132`). So `len("café")` is `5`, and `mid` can cut a
   multibyte codepoint in half. Case operations (`caseless`, `upper`, `lower`)
   and comparison are ASCII/byte-based.

These are **separable**: (1) is a representation change; (2) is a layer of
codepoint-aware operations over UTF-8. The plan below does (1) first (it is the
shared foundation) and (2) on top.

## 2. Settled decisions

Decided with Matthew, 2026-06-24:

- **One binary-safe string type — not a `bytes`/`text` split.** The string value
  becomes *length + bytes* (binary-safe; `chr(0)` works). Character-oriented
  operations are codepoint-aware over UTF-8; raw byte access is available through
  explicit builtins. Rationale: a single string type keeps BASIC approachable and
  avoids forcing every builtin and every module (sqlite, pg, webclient, GUI,
  encode/decode) to choose which of two types it speaks. The cost — that one type
  carries both "sequence of bytes" and "sequence of characters" meanings — is
  managed by a clear **operation taxonomy** (§4), not by the type system.

- **Codepoint is the character unit for v1.** `len`/`mid`/`left`/`right`/`reverse`
  count Unicode codepoints, not bytes and not grapheme clusters. No Unicode tables
  required; handles `café` and most emoji. Grapheme-cluster segmentation (flag
  emoji, combining marks, ZWJ sequences as one unit) is documented as **future
  work** (§9).

```basic
s = "café"
len(s)          ' 4   codepoints  (was 5)
byte_count(s)   ' 5   UTF-8 bytes
mid(s, 4, 1)    ' "é" never splits a codepoint
chr(0)          ' valid — a one-byte binary-safe string
```

## 3. Representation

The string value carries an explicit byte length:

```c
struct {
    char  *bytes;    /* UTF-8 by convention; may contain interior NUL */
    size_t length;   /* authoritative byte count; NOT strlen-derived */
} string;
```

- **`length` is authoritative.** No code may infer a string's size with `strlen`
  once Phase 1 lands; embedded NUL bytes are legal content.
- **NUL-termination is retained as a convenience.** `bytes[length] == '\0'`
  always, so C-string interop (snprintf targets, `strcmp` on known-NUL-free
  strings, path handling, module glue) keeps working for the common case of
  strings without interior NULs. The terminator is *not* counted in `length`.
- **Constructors.** `value_string(const char *)` keeps its meaning — "from a C
  string", `length = strlen`. Add `value_string_n(const char *bytes, size_t n)`
  for binary-safe construction. `copy_string`/`value_copy`/`value_free` become
  length-aware (allocate `length + 1`, copy `length` bytes, terminate).

This mirrors PBI Phase 0: a representation change that is **behaviorally
invisible** at first, because every existing construction site still produces a
valid NUL-terminated UTF-8 string where `strlen == length`.

## 4. Operation taxonomy

The single string type exposes two families of operation. The split is by
*builtin*, made explicit in naming, not by type.

**Character-oriented (codepoint, UTF-8).** The default for text work:

- `len(s)` — codepoint count.
- `left(s, n)`, `right(s, n)`, `mid(s, start, count[, repl])` — slice on codepoint
  boundaries; never split a codepoint.
- `reverse(s)` — reverse by codepoint.
- `find` / `contains` / `split` — operate on codepoint boundaries (a match index
  is a codepoint index).
- `chr(n)` — codepoint → its UTF-8 encoding (§5).
- `code(s)` — first codepoint of `s` (§5).

**Byte-oriented (raw).** For binary and protocol work:

- `byte_count(s)` — UTF-8/raw byte length.
- `byte_at(s, i)` — the `i`-th byte as a number `0..255`.
- `from_bytes(array_of_numbers)` — build a string from byte values `0..255`
  (binary-safe; values may be 0).

Indexing is **0-based or 1-based per the language's existing convention** —
match whatever `mid`/array indexing already do; do not introduce a second
convention here. (Confirm against current `mid` start semantics during Phase 2.)

## 5. `chr` / `code` migration

`chr`/`code` were added recently as **byte** builtins (`chr`: `0..255`, errors on
`0`; `code`: first byte). Under the new model they become **codepoint** builtins —
the canonical example of the design:

| | Before (byte) | After (codepoint) |
| --- | --- | --- |
| `chr(0)` | error (NUL not representable) | one-byte string `"\0"` (binary-safe) |
| `chr(233)` | `"é"`-lead byte only (invalid) | `"é"` (2-byte UTF-8) |
| `chr(0x1F600)` | error (> 255) | `"😀"` (4-byte UTF-8) |
| `code("é")` | `195` (first byte) | `233` (first codepoint) |

`chr` accepts `0 .. 0x10FFFF` (excluding surrogates `0xD800..0xDFFF`, which are
not valid scalar values). Raw single-byte access moves to `byte_at`/`from_bytes`.
This is a behavior change to two builtins added in this same dev cycle, so the
blast radius is small; call it out in the changelog.

## 6. Comparison, case, normalization

- **Ordering / equality** is by **byte sequence** (`memcmp` over `length`). This
  is well-defined, binary-safe, and stable; codepoint order equals byte order for
  UTF-8, so it is also correct codepoint ordering for valid text.
- **Case folding** (`caseless`, `upper`, `lower`) is **ASCII-only in v1** — `A–Z`
  ↔ `a–z`, every other byte unchanged. Full Unicode case folding needs tables and
  is **future work** (§9). Document the limit; do not silently mis-fold non-ASCII.
- **Normalization (NFC/NFD) is out of scope for v1.** Strings are stored exactly
  as authored/received. A decomposed `e`+◌́ therefore counts as 2 codepoints; an
  NFC `é` counts as 1. This is a documented consequence of "codepoint, not
  grapheme", not a bug.

## 7. Invalid UTF-8 policy

Character-oriented operations must never crash on non-UTF-8 bytes (the type is
binary-safe, so they will encounter them). Rule: **a malformed/continuation byte
that is not part of a valid sequence counts as one unit** and is sliced as a
single byte. This makes `len`/`mid` total functions over arbitrary bytes,
degrading gracefully to byte semantics for non-text data instead of erroring.

## 8. Source encoding and literals

- `.bas` source files are **UTF-8**. String literals carry their bytes verbatim,
  so `"café"` in a UTF-8 file is already the correct 5 bytes.
- Add a `\u{...}` escape for explicit codepoints, e.g. `"\u{1F600}"`. Existing
  escapes are unchanged. (Lexer escape handling is byte-based today; `\u{...}`
  emits the UTF-8 encoding of the codepoint.)

## 9. Convergence with multiprocessing (the third thread)

Doing strings before actors de-risks multiprocessing:

- **Serialization becomes trivial and total.** Once a string is *length + bytes*,
  sending one across an actor/process boundary is a length-prefixed byte copy —
  no encoding questions, binary-safe by construction. A `char *`-with-`strlen`
  model could not serialize embedded NULs at all.
- **Watchers / value model unaffected.** Strings are leaf values; the §3 change
  does not touch the refcounted-cell / COW machinery PBI added. Concatenation and
  slicing allocate fresh strings as today.

## 10. Phased implementation plan

Mirrors the PBI discipline: a behaviorally invisible representation phase first,
each phase merged green before the next.

- **Phase 0 — length-carrying representation.** Add `length` to the string value;
  update `value_string`, `value_string_n`, `copy_string`, `value_copy`,
  `value_free`. All existing sites still yield valid NUL-terminated UTF-8 with
  `strlen == length`. Behaviorally invisible; full suite green unchanged.
- **Phase 1 — binary safety. DONE (2026-06-24).** Core paths are now
  length-authoritative rather than `strlen`: `print`/string output and file
  `write`/`append`/`overwrite` (`fwrite` over `string_length`), string equality
  and ordering (`string_value_equal`/`string_value_compare` via `memcmp`+length,
  applied at every both-operands-runtime comparison: watcher equality, `unique`,
  sort, the central `eval_comparison`, `consider`), `len` (byte-authoritative;
  codepoint count comes in Phase 2), concatenation (`value_string_n`), and
  `encode`/`decode` (`encode_string_literal` takes an explicit length and escapes
  NUL/control bytes as `\u00XX`; `decode_parse_string` rebuilds via the builder
  length). `chr`/`code` migrated to codepoints (§5) using `utf8_encode_codepoint`/
  `utf8_decode_first` — `chr` accepts `0..0x10FFFF` excluding surrogates, `chr(0)`
  works, `code` returns the first codepoint. Added `byte_count`/`byte_at`
  (1-based)/`from_bytes`. Tests: `examples/unicode_bytes_test` (interior-NUL
  round-trip through concat/encode/decode/equality, `chr(128512)`=😀, byte
  builtins); negatives retargeted (`chr_range`→`0x10FFFF+`, new `chr_surrogate`,
  removed now-valid `chr_null`); `chr_code_test` updated to codepoint semantics.
  Suite 103/162/sqlite/webserver/site/webclient green, Valgrind-clean incl.
  adventure. NOTE: `.bas` lexer has **no `0x` hex literal**; use decimal codepoints
  (the `\u{…}` escape arrives in Phase 2).
- **Phase 2 — codepoint-aware character ops.** Make `len`/`mid`/`left`/`right`/
  `reverse`/`find`/`split`/`contains` count and slice by codepoint (§4) with the
  §7 invalid-byte rule. Add the `\u{...}` literal escape. Tests: `len("café")`,
  `mid` on emoji, slicing across multibyte boundaries.
- **Phase 3 — case and comparison polish.** Pin down ASCII case folding for
  `caseless`/`upper`/`lower`; document full-Unicode folding, normalization, and
  grapheme clusters as future work. Tests: caseless ASCII; non-ASCII left intact.

## 11. Open questions

1. **Indexing base for `mid`/`byte_at`.** Confirm 0- vs 1-based against the
   existing `mid` convention before Phase 2; keep it consistent, do not introduce
   a second base.
2. **Should `len` of a non-string keep its current container meaning?** `len`
   today also reports array/record sizes. Codepoint counting applies only to the
   string case; array/record behavior is unchanged. Confirm no overload conflict.
3. **`reverse` on a record/array** already exists as a collection mutator;
   string `reverse` must not collide with it (it is a separate value kind, so it
   should not — verify dispatch).
4. **Grapheme clusters** (future): which algorithm/table (UAX #29) and when.
5. **Full Unicode case folding / normalization** (future): table source and size
   budget, given the project ships a single self-contained binary.

## 12. Test matrix (golden-file, per project conventions)

- representation: a string built with an interior NUL round-trips through assign,
  `print`, `encode`/`decode`, and length reports correctly.
- `chr`: `chr(0)`, `chr(233)`, `chr(0x1F600)`; surrogate and out-of-range errors.
- `code`: first codepoint of an ASCII and a multibyte string.
- bytes: `byte_count`/`byte_at`/`from_bytes` round-trip; `from_bytes([0,…])`.
- codepoint ops: `len("café")` = 4; `mid`/`left`/`right` on multibyte and emoji;
  slicing never splits a codepoint; reverse by codepoint.
- invalid UTF-8: `len`/`mid` over arbitrary bytes degrade to one-unit-per-bad-byte
  and never error.
- literals: `"\u{1F600}"` equals the 4-byte UTF-8 sequence.
- case: ASCII `caseless`/`upper`/`lower`; non-ASCII bytes unchanged.
- negative: `chr` surrogate / out-of-range; `byte_at` out of range; `from_bytes`
  with a value outside `0..255`.
```
