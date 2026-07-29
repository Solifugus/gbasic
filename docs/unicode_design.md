# Unicode and Binary-Safe Strings — Design

Status: **implemented** (Phases 0-3 shipped 2026-06-24). One of the three
pre-freeze language threads (with PBI — complete — and multiprocessing, the last
remaining). See `docs/pbi_design.md §9` for how these threads share machinery, and
§10 below for the per-phase shipping record.

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

Indexing is **0-based**, matching `mid`/array indexing (resolved in Phase 2; see
§11.1). `byte_at(s, 0)` is the first byte.

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
- **Phase 2 — codepoint-aware character ops. DONE (2026-06-24).** `len`/`mid`/
  `left`/`right`/`reverse`/`find` now count and slice by codepoint (§4) via
  `string_codepoint_count`/`string_codepoint_offset` over `utf8_decode_first`,
  with the §7 lenient invalid-byte rule (one unit per malformed byte). `len("café")`
  is 4; `mid`/`left`/`right` never split a codepoint; `reverse` mirrors codepoints
  (new `reverse_string_value`; strings are immutable, so it returns a copy and
  never mutates the binding); `find` returns a **codepoint** index (binary-safe
  search via `string_find_bytes`). `contains` is array-only (membership bool — no
  index, nothing to change) and `split` yields valid substrings byte-wise (no index
  returned), so neither needed codepoint work. Added the `\u{...}` literal escape:
  the lexer (`string_token`) consumes `\u{HHHH}`; the parser (`copy_string_literal`,
  `utf8_encode_literal`) decodes it to UTF-8. `\u{...}` accepts `1..0x10FFFF`
  excluding surrogates; **`\u{0}` is rejected** (AST string literals are
  NUL-terminated `char *` — use `chr(0)` for a literal NUL). `byte_at` is **0-based**
  to match `mid`/array indexing (open question §11.1 resolved: `mid` is 0-based,
  confirmed by `secure_token_test`; the Phase 1 1-based byte_at was corrected before
  release). Tests: `examples/unicode_chars_test`; negatives `negative_uesc_surrogate`/
  `_range`/`_zero`; `negative_reverse_type` retargeted to a number (reverse now
  accepts strings, message → "an array or string"). Suite 104/165/sqlite/webserver/
  site/webclient green, Valgrind-clean incl. adventure.
- **Phase 3 — case and comparison polish. DONE (2026-06-24).** `upper`/`lower`/
  caseless `=`/`!=` now use explicit **locale-independent** ASCII folding
  (`ascii_tolower`/`ascii_toupper`): only `A-Z`<->`a-z` fold; every other byte —
  including all UTF-8 multibyte sequences — passes through untouched, so non-ASCII
  is never mis-folded the way locale-sensitive `tolower`/`toupper` could be. All
  three are now **binary-safe** (length-based, interior NULs preserved): `upper`/
  `lower` rebuilt via `value_string_n`; caseless comparison via
  `string_value_equal_caseless` (length + ASCII fold, non-ASCII compared exactly);
  the shared `string_equal_caseless` (keyword/header matching) switched to
  `ascii_tolower` too (ASCII-only inputs, behavior identical, locale risk removed).
  Full-Unicode case folding, normalization (NFC/NFD), and grapheme-cluster
  segmentation remain **future work** (§6, §11.4-5). Test: `examples/unicode_case_test`
  (ASCII folds; `é`/`É`/`Ω`/`ß` intact; caseless folds ASCII but treats non-ASCII as
  exact bytes). Suite 105/165/sqlite green, Valgrind-clean.

**Unicode v1 is COMPLETE** (Phases 0-3 shipped). The last pre-freeze thread is
multiprocessing (actors); serialization is now trivial since strings are
length+bytes (§9).

- **Phase 4 — the cost of codepoint addressing. DONE (2026-07-29, PLAT-STRIDX).**
  Phase 2 bought codepoint-correct character ops with a walk: `string_codepoint_count`
  and `string_codepoint_offset` each traversed the whole string on **every** call to
  `len`/`mid`/`left`/`right`, so a per-character loop was O(n²). Compounding it,
  `value_copy` duplicated the byte buffer and `env_get` calls `value_copy` on every
  read of a variable, so even `byte_at(s, i)` — O(1) by construction — cost O(n) per
  call inside a loop. Measured before: a 256 000-character forward scan 249 s, a
  1 000 000-byte `byte_at` loop 30 s. Both are removed without any change to what a
  string *means*:
  - `StringHeader` gained `refs`; `value_copy` now shares the buffer. Sound because
    string values are **immutable** — `as.string` is assigned in exactly one place
    (`value_string_n`), and every "modifying" builtin fills a fresh buffer and
    constructs a new value from it.
  - The header also caches `cp_count` (counted once) plus a `cursor_cp`/`cursor_byte`
    pair so a forward walk resumes rather than restarts. When `cp_count == length`
    every unit is one byte — true for ASCII, and equally true for malformed bytes
    under the §7 lenient rule — so the codepoint index *is* the byte offset and
    access is O(1) by arithmetic.
  - A sparse `samples` index (one offset per 64 codepoints) is built lazily, only
    for a multibyte string accessed out of forward order, which is what keeps
    backward and random traversal linear (256 000 units backwards: 152 s → 0.31 s).
  The cache needs no invalidation — the bytes it describes cannot change — and it
  survives across variable reads only because of `refs`; the two are one mechanism.
  Cost: 40 bytes per string value (measured: 200 000 short strings, 29.3 MB →
  37.2 MB; negligible against large ones). Tests: `tests/run_stridx.sh`, whose
  correctness golden was captured *before* the change and must not move.

## 11. Open questions

1. **Indexing base for `mid`/`byte_at`. RESOLVED (Phase 2): 0-based.** `mid` is
   0-based (confirmed by `examples/secure_token_test`: `i = 0` … `mid(token, i, 1)`),
   and `byte_at` matches it. No second base introduced.
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
