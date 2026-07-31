# gBASIC text & pattern library — design proposal

Status: **Layer 0 (regex builtins) ACCEPTED and in build; Layer 1 (ARI) still a
proposal.** Decisions recorded in §13 (2026-07-31).

A two-layer library for finding and extracting structured data from text:

- **Layer 0 — regex**: a small set of always-available C builtins (`re_*`) for
  linear pattern matching, backed by the POSIX regex engine already in libc.
- **Layer 1 — ARI**: a pure-gBASIC `stdlib` library that parses *messy,
  semi-structured reports* declaratively and returns a **frame**. ARI is built
  on Layer 0.

The regex layer is the linear primitive everyone reaches for. The ARI layer is
the structural one: it locates a field by *what surrounds it in the document*
rather than by a rigid column position, which is what real-world legacy reports
demand.

> ARI (Anchor Relative Identification) originates in the **AmorphDB** project
> (`docs/archived/amorphdb_computer_library.md`, "Fixed-Width Import via ARI"),
> where it was specified but never implemented. This design brings it across
> faithfully and adapts it to gBASIC's frame and value model, where it fits
> better than it did at home (see §4).

## 0. Motivation (the genuine gap)

gBASIC has `find`, `replace`, `split`, `contains`, `starts_with`, `ends_with`,
`trim` — all **literal** string operations. There is **no regular expression
support at any level**, and the EDGAR suite already feels it: `mdna.bas` and the
filing readers do ad-hoc substring surgery to pull prose and numbers out of SEC
documents. Two distinct needs go unmet:

1. **Linear pattern matching** — "find every `\$[\d,]+\.\d{2}` in this string."
   That is regex, and it is missing.
2. **Structural report extraction** — "from this whole quarterly report, pull the
   transactions table into rows." That is a document-level problem regex handles
   poorly on its own. That is ARI.

They are not competitors; they are two layers of one library, because **ARI's
field patterns are themselves regexes**.

## 1. Architecture — two layers, one library

```
  ARI spec (text)  ─►  ari.bas  ─────────────►  frame  ─►  frame.bas / stats / chart
   (stdlib, pure gBASIC; parses the spec, walks the report as a line grid)
                          │
                          ▼  uses
  re_test / re_find / re_find_all / re_replace / re_split      (C builtins, libc)
```

- **Layer 0** is C, always compiled in (§2), and usable on its own.
- **Layer 1** is ordinary gBASIC that consumes Layer 0. It emits a frame, so its
  output flows straight into `frame.bas`, `stats.bas`, and the proposed charting
  library — ARI becomes an *import front-end* alongside `frame.read_csv`.

## 2. Decision A — regex engine: libc POSIX ERE, as an always-on builtin

This is the crux decision for Layer 0.

- **It is a C builtin, not a `stdlib` library.** A correct regex engine is real
  machinery (NFA compilation, matching) with a performance floor a tree-walked
  gBASIC reimplementation could never meet. This clears the "earn a C
  implementation" bar the way XML and crypto did.
- **The engine is `regcomp`/`regexec` from libc** (POSIX Extended Regular
  Expressions). Crucially, this is **not an optional dependency** — POSIX regex
  is part of libc on every platform gBASIC targets. So unlike `sqlite`/`pg`/`xml`
  behind `HAVE_*`, the regex builtins are **always available**, with no guard and
  no "feature compiled out" runtime error. They are core builtins, registered in
  `src/builtins.c` next to the bitwise verbs and implemented in `src/eval.c`.

**The tradeoff, stated plainly.** POSIX ERE lacks some PCRE conveniences:
no lookaround, no backreferences *in the pattern*, greedy-only quantifiers, and
the shorthand classes are spelled `[[:digit:]]`/`[[:alpha:]]`/`[[:space:]]`
rather than `\d`/`\w`/`\s`. To keep patterns readable (and to let ARI specs use
the familiar `\d{4}` form), the builtin performs a **small, documented
pre-translation** of a whitelisted shorthand set before compiling:

| written | compiled to |
|---|---|
| `\d` | `[[:digit:]]` |
| `\D` | `[^[:digit:]]` |
| `\w` | `[[:alnum:]_]` |
| `\W` | `[^[:alnum:]_]` |
| `\s` | `[[:space:]]` |
| `\S` | `[^[:space:]]` |

The translation is bounded and deterministic (it does not attempt to emulate
`\b`, lookaround, or non-greedy — those simply aren't supported in v1). If real
demand for PCRE power features appears, the clean upgrade path is an **optional**
`HAVE_LIBPCRE2` engine selected at build time behind the same builtin surface
(§14 Phase 4) — the always-on libc engine remains the floor.

## 3. Decision B — the regex surface (`re_*` builtins)

Six builtins. Names take an `re_` prefix because `find`/`replace`/`split`
already exist as literal-string builtins and must not change meaning.

| builtin | returns | notes |
|---|---|---|
| `re_test(s, pat)` | boolean | does the pattern match anywhere in `s`? |
| `re_find(s, pat)` | match record or `unknown` | first match; `unknown` if none |
| `re_find_all(s, pat)` | list of match records | non-overlapping, left to right |
| `re_replace(s, pat, repl)` | string | `$1`..`$9` group refs in `repl` |
| `re_split(s, pat)` | list of strings | split on matches |
| `re_groups(m)` | list | convenience: capture groups of a match record |

A **match record** is plain data (inspectable, no new value kind):

```
{ text: "$1,500.00", start: 21, end: 30, groups: ["1,500.00"] }
```

- **No match** is `unknown`, not an error — consistent with gBASIC's NA policy,
  so `is_unknown(re_find(...))` is the miss test and a missing match propagates.
- **Flags** ride as a trailing optional argument, a short flag string:
  `re_find(s, pat, "i")` (ignore case), `"m"` (multiline `^`/`$`), `"s"`
  (dot matches newline). Absent ⇒ defaults (case-sensitive, `.` excludes
  newline). Kept as a string so the common call stays two arguments.
- **Determinism**: same inputs ⇒ same output, always. Group ordering is by
  opening paren.
- **`start`/`end` are CODEPOINT indices, not byte offsets** (decided 2026-07-31,
  overriding this document's original byte-offset proposal). The existing `find`
  builtin already returns a codepoint index precisely so that
  `mid(s, find(s, x), n)` composes; a regex match that reported bytes would
  disagree with it and could not be fed to `mid`/`left`/`right` for any string
  containing non-ASCII. The POSIX engine works in bytes internally, so the
  implementation converts on the way out — O(1) for one-byte-per-unit strings
  after PLAT-STRIDX, and bounded by the cached cursor otherwise.
- **Compile caching**: `re_*` compiles the pattern on each call, but the
  implementation caches the last-compiled pattern string ⇒ compiled program, so a
  pattern reused across a loop compiles once. (An explicit `re_compile` handle is
  deliberately deferred — see Non-goals — to keep the surface stateless.)

## 4. Decision C — ARI as pure gBASIC, emitting a frame

- **ARI stays in `stdlib` (pure gBASIC).** It wraps no native library — it is
  spec-parsing plus column arithmetic over lines, all on top of the `re_*`
  builtins. The earn-a-C-module bar is *not* met, so it is `stdlib/ari.bas`.
- **ARI emits a frame.** This is the key adaptation, and the reason ARI fits
  gBASIC better than it fit AmorphDB. A repeating `section ... break` is exactly
  a frame: named columns, equal-length, one record per repeat. A flat (non-break)
  section returns a single record. So ARI output drops directly into
  `frame.bas`/`stats`/`chart` with no glue. In AmorphDB this was "planned"; here
  the target type already exists and is load-bearing.
- **Type keywords map to native gBASIC values.** ARI's `date`, `money`,
  `integer`, `decimal` become real gBASIC date/money/number values on extraction
  — automatic type conversion is nearly free because the value kinds exist.
  (`ssn` is dropped from the built-in set as too niche; it is expressible as a
  user `type`.)
- **The report is a line grid.** Because gBASIC strings are not index-addressable
  character arrays, the ARI engine holds the document as a **list of lines** and
  does `up`/`down` by line index and `left`/`right`/`same` by column math over a
  line (using the codepoint/substring surface). This is the one structural
  adaptation to call out.

## 5. The ARI spec language (adapted from AmorphDB)

An ARI spec is a text value. Faithful to the original, with the gBASIC
adaptations noted above.

**Overall structure:**

```
section section_name [starts("pattern")] [ends("pattern")]:
    field field_name: anchor [anchor ...]: pattern [, pattern ...]
    [break [on "pattern" | on /regex/]]
    [section ...]
```

**Anchors** describe where a field sits relative to a known marker —
`direction distance: pattern`:

- **Directions**: `left`, `right`, `up`, `down`, `same` (same line as the
  previous anchor).
- **Distances**: `5` (exactly), `2-10` (range), `3-` (at least), `-8` (at most),
  `flush` (to the edge of the current section).
- Multiple patterns on one anchor are **OR**; multiple anchors on one field are
  **AND**.

**Pattern types:**

| Type | Syntax | Example |
|---|---|---|
| Text literal | `"text"` | `"ID"`, `"TOTAL"` |
| Regex | `/regex/` | `/\d{4}-\d{2}-\d{2}/` (compiled via the `re_*` engine) |
| Regex with transform | `/regex/replacement/` | `/(\d{2})\/(\d{2})\/(\d{4})/$3-$1-$2/` |
| Built-in type | `date`, `money`, `integer`, `decimal` | native gBASIC values |

**Sections** are named regions; without `starts`/`ends` they inherit the
enclosing bounds. Nesting is supported. **Break** marks a repeating record:
`break` (re-match of the section's first field), `break on "---"` (text
separator), or `break on /^\s*$/` (regex/blank-line separator). **Custom types**
are definable in-spec for reuse:

```
type CURRENCY:
    /\$([\d,]+\.\d{2})/  -> strip(",") as decimal
    /([\d,]+\.\d{2})CR/  -> strip(",") negate as decimal
    output: decimal
```

## 6. ARI gBASIC API

```
' Parse an in-memory report string against a spec → record or frame
result = ari.parse(report_text, spec_text)

' Convenience: read a file and parse it
result = ari.import(path, spec_text)          ' p(file)= path under the hood
result = ari.import(path, spec_text, options)
```

- A flat spec returns a **record**; a spec whose top section (or a nested one)
  has `break` returns a **frame** for that section, nested under its record key —
  exactly the shape `frame.bas` consumes.
- **Field not found** ⇒ that cell is `unknown` (the original's `#not_found`),
  never guessed — consistent with the EDGAR suite's missing-data rule.
- **A malformed spec** raises with the offending line (a programmer error, caught
  early). **Malformed input under a valid spec** degrades per-field to `unknown`,
  so one bad row never sinks the whole import.
- `options` mirrors `frame.read_csv` where sensible (e.g. type-coercion toggles).

## 7. Worked example (brought across, gBASIC output)

Input report:

```
BANK REPORT 2024-03-15
ID: 12345
==== transactions ====
TYPE: DEBIT  AMOUNT: $1,500.00  TAX: $45.00
TYPE: CREDIT AMOUNT: $2,000.00  TAX: $0.00
==== end of transactions ====
TOTAL: $500.00
```

Spec:

```
section bank_report:
    field report_date: right 10 up 1: date
    field bank_id: right 5 down 1: "ID"

    section transactions starts("==== transactions ====") ends("==== end of transactions ===="):
        field transaction_type: right 6 same: "TYPE"
        field amount: right 12 same: "AMOUNT"
        field tax: right flush same: "TAX"
        break

    field total: right 10 down 1-5: "TOTAL"
```

`ari.parse(report, spec)` returns:

```
{
  report_date: @2024-03-15,          ' native date value
  bank_id: 12345,
  transactions: {                    ' a FRAME (columns, break-delimited rows)
    transaction_type: ["DEBIT", "CREDIT"],
    amount: [1500.00, 2000.00],      ' native money/decimal
    tax:    [45.00, 0.00]
  },
  total: 500.00
}
```

`result.transactions` is now directly usable: `stats.mean(result.transactions.amount)`,
`chart.bar(result.transactions, "transaction_type", "amount")`,
`frame.summarize(...)`.

## 8. Missing data and errors

- **Regex miss** ⇒ `unknown` (not an error).
- **ARI field not found** ⇒ that cell `unknown`.
- **Type-conversion failure** on an otherwise-matched field ⇒ that cell
  `unknown` (never a silent zero).
- **Spec syntax error** ⇒ raise, with line number — this is a bug in the caller's
  spec, surfaced immediately.
- **File not found** in `ari.import` ⇒ raise (matches file-builtin behavior).

## 9. Determinism and testing (the golden-file win)

Both layers are deterministic, so both test the standard gBASIC way:

- **Regex**: a `.bas` that prints `re_find_all(...)` results, a sibling `.out`.
- **ARI**: because a spec *is a text value*, a test is `input.txt` + `spec.ari`
  (or an inline spec) + a `.bas` that prints the parsed frame + a `.out`. No
  network, fully reproducible.

This is exactly what makes the library handable to Adrian: he validates report
parsing by writing specs and diffing frames, never by reading C.

## 10. Performance notes

**CORRECTED 2026-07-31.** This section was written when gBASIC had two O(n²)
traps, and it shaped the design around avoiding them. Both are gone:

- Indexed array access in a `while` loop, and `append`, became linear on
  2026-07-23 (copy-on-write arrays). `for each` is still the more readable loop;
  it is no longer the faster one.
- Per-character string scanning became linear on 2026-07-29 (PLAT-STRIDX): a
  256 000-character scan went from 249 s to 0.30 s, in either direction.

That second fix is what makes Layer 1 viable at all. A pure-gBASIC ARI parser
walks a report character by character; at the sizes real filings reach, the old
behaviour would have made the layer unusable, and the design would have had to
push ARI into C. It does not.

What still holds:

- **Regex**: reuse the compiled-pattern cache (§3); do not rebuild a pattern
  inside a loop. That is a real cost and the cache is why it is bounded.
- **String building**: `s = s + x` in a loop is still O(n²) — the one trap that
  was not fixed. Accumulate into an array and `join` once.
- **Memory bound**: ARI holds the whole report in memory as lines. That is the
  right call for filings and reports and the wrong one for multi-gigabyte
  streams; it is stated as a bound, not hidden.

## 11. Non-goals (v1)

- **PCRE power features** — lookaround, backreferences in the pattern,
  non-greedy quantifiers, named groups. POSIX ERE doesn't offer them; the
  optional `HAVE_LIBPCRE2` upgrade (§14) is the path if they're needed.
- **A compiled-pattern handle type** (`re_compile` → object). The cache gives the
  performance benefit without adding a stateful value kind or a dispose story.
- **CSV/TSV parsing** — `frame.read_csv` already owns that; ARI is for
  *irregular* fixed-width/positional reports, not delimited data.
- **Full grammar / BNF parsing** — ARI extracts fields by spatial anchoring; it
  is not a general parser generator. Language parsing is a different tool.
- **Streaming huge files** — the report is loaded as lines (see §10).

## 12. Integration

- **EDGAR** — replace `mdna.bas`'s ad-hoc extraction with regex, and parse
  positional filing exhibits / 13F tables / regulatory dumps with ARI specs
  stored as `.ari` files, versioned in the repo.
- **frame / stats / chart** — ARI's frame output flows straight into the existing
  toolkits; a parsed report can be summarized and charted with no glue.
- **Reusable specs** — an ARI spec is a text value: read it from a file, pass it
  around, generate it programmatically, and golden-test it.

## 13. Decisions (answered 2026-07-31)

Answered before Layer 0 went into build. The original questions are kept below
each answer so the reasoning stays legible.

**A. Match offsets are CODEPOINT indices.** Not in the original list — raised
because §3 specified byte offsets while the existing `find` builtin returns
codepoints so it composes with `mid`. Two conventions in one language for the
same idea is the kind of thing nobody remembers correctly. See §3.

**B. This phase ships LAYER 0 ONLY** — the six `re_*` builtins, tested,
documented, with a cookbook entry. ARI is a later phase, designed against
builtins that exist rather than against a specification of them.

**C. ARI type keywords: the four generic ones** (`date`, `money`, `integer`,
`decimal`). Domain identifiers such as `cik`/`accession`/`cusip` stay
user-defined via `type` with a regex, rather than putting EDGAR vocabulary in a
general text library. Revisit once ARI has run against real filings.

**Adopted as proposed** (the document's own leans, taken unchanged): the
whitelisted `\d \D \w \W \s \S` shorthand set with `\b` left out (POSIX's
`[[:<:]]`/`[[:>:]]` is not portable); libc-only for v1 with `HAVE_LIBPCRE2`
deferred until a power feature is actually asked for; and `stdlib/ari.bas` alone,
with no `text.bas` wrapper layer over the builtins.

### The original open questions

1. **Regex shorthand set** — is the whitelisted `\d \D \w \W \s \S` translation
   (§2) the right scope for v1, or do you also want `\b` word boundaries (POSIX
   supports `[[:<:]]`/`[[:>:]]` on some libc builds — non-portable, so I lean
   leaving it out)?
2. **Optional PCRE2 now or later** — ship libc-only in v1 and add
   `HAVE_LIBPCRE2` only if power features are requested (my lean), or wire the
   optional engine from the start?
3. **File layout** — regex builtins + a single `stdlib/ari.bas` (my lean), or
   also a thin `stdlib/text.bas` of ergonomic wrappers (`text.matches`,
   `text.extract_all`) over the `re_*` builtins for a friendlier surface?
4. **ARI scope for Phase 2** — is the worked bank-report example (sections,
   fields, five directions, literal + `/regex/`, `break`) the right first cut,
   with distance ranges/`flush`, nested `starts/ends`, type keywords, and
   transforms deferred to Phase 3? (My lean: yes.)
5. **Type keyword set** — `date`, `money`, `integer`, `decimal` for v1, dropping
   `ssn` to user-defined `type`? Any domain types worth building in for EDGAR
   (e.g. `cik`, `accession`, `cusip` as validated string types)?

## 14. Roadmap (phases)

Each phase is independently shippable and golden-file testable.

### Phase 1 — regex builtins
`re_test`, `re_find`, `re_find_all`, `re_replace`, `re_split`, `re_groups` on the
libc engine, with the shorthand translation, flag string, match-record shape,
`unknown`-on-miss, and the compile cache. Golden + negative tests. Unblocks all
linear text work immediately, including a first pass at cleaning up `mdna.bas`.

### Phase 2 — ARI core
`stdlib/ari.bas`: `ari.parse` / `ari.import`; `section`/`field`; the five
directions with exact distances; literal and `/regex/` patterns; `break`
(default + `on`); single-record and frame output; `unknown` on not-found. Enough
to parse the worked example end to end. Golden tests with inline specs.

### Phase 3 — ARI advanced
Distance ranges and `flush`; nested sections; `starts`/`ends` boundaries; the
built-in type keywords (native value conversion); regex-with-transform
(`/re/repl/`); custom `type` blocks. Golden tests per feature.

### Phase 4 — optional power engine + ergonomics
Optional `HAVE_LIBPCRE2` engine behind the same `re_*` surface (lookaround,
non-greedy, named groups) selected at build time; optional `text.bas` ergonomic
wrappers if §13.3 favors them. Purely additive — the libc floor never regresses.
