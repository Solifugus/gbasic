# gBASIC text & pattern library — design proposal

Status: **Layer 0 (regex in the core) IMPLEMENTED 2026-08-01 — Phase 1 of §14 is
done. Layer 1 (ARI) remains a proposal.** Decisions recorded in §13.

The Layer 0 surface was redesigned on 2026-08-01, before any C was written — see
§13 decision D — from six `re_*`-prefixed builtins to a `regex` value kind plus
overloads of the existing `contains`/`replace`/`split` verbs. Sections 2, 3, 9
and 11 reflect the shipped shape; the superseded surface is recorded in §13.D
rather than deleted.

What shipped: `VALUE_REGEX` (copy/free/print/compare/`SER_REGEX`), `regex()`,
`match()`, `match_all()`, the three overloads, the shorthand translation, the
flag handling, codepoint offsets, and binary-safe subjects via `REG_STARTEND`.
Tested by `tests/run_regex.sh` (golden + flag matrix + 8 negative cases + actor
round-trip + valgrind); documented in `docs/reference.md`, `docs/ai/UNLEARN.md`
and `docs/ai/COOKBOOK.md`.

A two-layer library for finding and extracting structured data from text:

- **Layer 0 — regex**: an always-available core value kind (`regex`) plus a small
  set of C builtins for linear pattern matching, backed by the POSIX regex engine
  already in libc.
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

gBASIC has `find`, `replace`, `split`, `starts_with`, `ends_with`, `trim` — all
**literal** string operations. (This list originally included `contains`;
**corrected 2026-08-01** — `contains` was array-only and *raised* `contains
expects an array` on a string first argument, verified against the built
interpreter. §13.G resolves this by giving `contains` the string case, literal
and regex, so the list above becomes true rather than merely asserted.) There is
**no regular expression
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
  regex() / match() / match_all() / contains() / replace() / split()   (core, libc)
```

- **Layer 0** is C, always compiled in (§2), and usable on its own.
- **Layer 1** is ordinary gBASIC that consumes Layer 0. It emits a frame, so its
  output flows straight into `frame.bas`, `stats.bas`, and the proposed charting
  library — ARI becomes an *import front-end* alongside `frame.read_csv`.

## 2. Decision A — regex engine: libc POSIX ERE, as an always-on builtin

This is the crux decision for Layer 0.

- **It is in the core, not a `stdlib` library.** A correct regex engine is real
  machinery (NFA compilation, matching) with a performance floor a tree-walked
  gBASIC reimplementation could never meet. This clears the "earn a C
  implementation" bar the way XML and crypto did.
- **The engine is `regcomp`/`regexec` from libc** (POSIX Extended Regular
  Expressions). Crucially, this is **not an optional dependency** — POSIX regex
  is part of libc on every platform gBASIC targets. So unlike `sqlite`/`pg`/`xml`
  behind `HAVE_*`, regex is **always available**, with no guard and no "feature
  compiled out" runtime error. The new names are registered in `src/builtins.c`
  next to the bitwise verbs and implemented in `src/eval.c`; the `regex` value
  kind lives alongside the existing date/money/file kinds (§3).

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

## 3. Decision B — the regex surface (a value kind + overloads)

**Revised 2026-08-01 (§13.D).** The original design added six `re_*`-prefixed
builtins, the prefix existing solely because `find`/`replace`/`split` already
mean *literal* string operations and must not change meaning. A namespace solves
that problem properly instead of working around it: inside a regex value, the
verbs go back to being the verbs they already are.

**A `regex` value is a compiled pattern.** `regex(pat)` (optionally
`regex(pat, flags)`) compiles once and returns an immutable value holding the
compiled program and its flags.

Three of the four operations then **overload existing verbs**, dispatching on the
kind of the pattern argument exactly the way `find` already dispatches on
`VALUE_STRING` vs `VALUE_ARRAY` (`src/eval.c:19577`). A string argument stays
literal, so every existing program keeps its meaning by construction:

| call | returns | notes |
|---|---|---|
| `regex(pat [, flags])` | regex value | compiles; reuse it to compile once |
| `contains(s, regex(p))` | boolean | does it match anywhere in `s`? (§13.G) |
| `contains(s, "sub")` | boolean | literal substring; new, previously raised |
| `replace(s, regex(p), repl)` | string | `$1`..`$9` group refs in `repl` |
| `split(s, regex(p))` | list of strings | split on matches |
| `match(s, p)` | match record or `unknown` | first match; `unknown` if none |
| `match_all(s, p)` | list of match records | non-overlapping, left to right |

**`find` deliberately does not overload.** It returns a single number, because a
literal match *is* the needle you already hold. A regex match must return the
whole record — you do not know what matched, and the captures are the point.
Overloading `find` would make its return type depend on a runtime property of its
second argument, so every caller would have to know which it got. `match` earns a
separate name for the honest reason that its return shape genuinely differs.
`match`/`match_all` accept a plain string pattern or a `regex` value.

**Naming caution.** `match` scans for a match anywhere in the subject — it is
Python's `re.search`, **not** Python's `re.match`, which anchors at the start.
Reading it as "matches the whole string" gives wrong answers with no error, so
this divergence gets an `UNLEARN.md` bullet. Anchoring is one `^` away.

A **match record** is plain data (inspectable, not a new value kind — only the
compiled pattern is):

For `match("balance: $1,500.00 due", "\$([0-9,]+)\.([0-9]{2})")`:

```
{ text: "$1,500.00", start: 9, length: 9, groups: ["1,500", "00"] }
```

(Verified against the implementation. An earlier draft of this example wrote
`start: 10` and a first group of `"1,500.00"`; both were wrong — the `$` sits at
codepoint 9, and `[0-9,]+` cannot cross the `.`, so the first group stops at
`"1,500"`. `mid(s, 9, 9)` returns `"$1,500.00"`.)

- **`length`, not `end`.** `end` is a reserved word and cannot be a record field,
  and `start` + `length` is exactly what `mid` takes.
- **No match** is `unknown`, not an error — consistent with gBASIC's NA policy,
  so `is_unknown(match(...))` is the miss test and a missing match propagates.
- **`groups` is always a list**, empty when the pattern has no capture groups, so
  `count(m.groups)` is always safe. A group that **did not participate** (the
  unmatched side of `(a)|(b)`) is `unknown`, distinct from a group that matched
  the empty string, which is `""`. POSIX reports the difference for free and
  collapsing it would throw information away.
- **`re_groups` is gone** — capture groups are `m.groups` on an ordinary record,
  so the convenience accessor has nothing to do.
- **Flags** ride as an optional second argument to `regex`: `regex(p, "i")`
  (ignore case), `"m"` (multiline `^`/`$`), `"s"` (dot matches newline). Absent ⇒
  defaults (case-sensitive, `.` excludes newline). `match`/`match_all` accept the
  same flag string as a trailing third argument when given a bare string pattern,
  so the common call stays two arguments.
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
- **Compile caching is now explicit and user-controlled.** The original design
  hid a single-entry cache (last pattern string ⇒ compiled program) inside the
  builtins. `regex(p)` replaces it: hoist the call out of a loop and the pattern
  compiles once, visibly. Passing a bare string still works and compiles per
  call, which is the right default for one-shot use. This change also removes a
  real defect the hidden cache would have had in ARI — see §10.

## 4. Decision C — ARI as pure gBASIC, emitting a frame

- **ARI stays in `stdlib` (pure gBASIC).** It wraps no native library — it is
  spec-parsing plus column arithmetic over lines, all on top of the Layer 0
  surface. The earn-a-C-module bar is *not* met, so it is `stdlib/ari.bas`.
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

> **A tightened, implementable draft of this syntax lives in
> `docs/ari_spec_language.md`** (2026-08-01), written against the two committed
> fixtures. It supersedes this section's sketch where the two differ: the grammar
> below is ambiguous about where a `:` goes, and three of its assumptions did not
> survive measurement — a column heading cannot locate its own column (headings
> drift 4–11 columns from their data), a fixed column span cannot locate a value
> either (a trailing-minus negative is one column wider than the positives above
> it), and word-boundary anchoring is needed but unavailable. See that document's
> §1.

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
| Regex | `/regex/` | `/\d{4}-\d{2}-\d{2}/` (compiled to a `regex` value at spec-parse time) |
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

## 5.1 The variation problem (recorded 2026-08-01, from production experience)

This section exists because it is the reason ARI is worth building, and because
every instinct that says "just write a regex for the amount column" dies here.

Reports of this class are produced by long-lived programs in archaic languages,
edited by different people across decades. **A single report is not internally
consistent**, because different sections were written at different times by
different hands. This is not an edge case to be tolerated; it is the normal
condition of the input, and the design must assume it.

**Money is the worst offender.** All of the following occur, and more than one
can occur *within the same document*:

| form | example | note |
|---|---|---|
| symbol, no padding | `$1,234.56` | |
| symbol, value right-justified in a fixed field | `$    1,234.56` | the gap is padding, not a delimiter |
| no symbol at all | `1,234.56` | |
| no grouping separators | `1234.56` | |
| trailing minus | `$1,234.56-` | the form in `teller_totals.rpt` |
| leading minus, before the symbol | `-$1,234.56` | |
| leading minus, after the symbol | `$-1,234.56` | |
| bracketed negative | `<$1,234.56>` | also seen with other bracket pairs |
| parenthesized negative | `(1,234.56)` | the accounting convention |
| credit/debit suffix | `1,234.56CR` | |

**The design consequence is structural, not cosmetic.** A built-in `money`
keyword cannot be one fixed pattern, and it cannot be one setting for the whole
report either, because the report disagrees with itself. Type recognition must
therefore be **scoped**: a default permissive recognizer covering the union of
common forms, overridable **per section and per field** where the permissive one
would be wrong or ambiguous.

That ambiguity is real, not theoretical. `1,234.56-` is a negative amount in one
report and a positive amount followed by a separator in another; `(1,234.56)` is
negative under the accounting convention and a parenthetical note elsewhere.
Nothing in the token decides it — only the surrounding document does. So the
permissive recognizer must be *overridable*, and the override must be local.

This raises the standing of custom `type` blocks (§5). They were positioned as
an advanced convenience; they are in fact the mechanism by which a spec author
copes with a report that contradicts itself, and the built-in keywords are best
understood as pre-supplied common cases rather than as the primary interface.

**Other archaisms in the same family**, all seen and all to be represented in
fixtures: identifiers glued to labels with no separator (`CHK#4211` inside a
free-text description column); the same field labelled two ways in one document
(`Teller #:` and `Teller#:`); fields whose order differs between otherwise
identical blocks; and column headings that shift position between tables.

**Fixture policy that follows from this.** Inventing *additional* section kinds
purely to widen the variation covered is explicitly worthwhile — a fixture is not
required to be a faithful reproduction of any one real report, and a synthetic
section that exercises a bracketed negative earns its place even if no observed
report pairs it with that layout.

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

- **Regex**: a `.bas` that prints `match_all(...)` results, a sibling `.out`.
- **ARI**: because a spec *is a text value*, a test is `input.txt` + `spec.ari`
  (or an inline spec) + a `.bas` that prints the parsed frame + a `.out`. No
  network, fully reproducible.

This is exactly what makes the library handable to Adrian: he validates report
parsing by writing specs and diffing frames, never by reading C.

**The corpus problem (2026-08-01).** ARI's motivating targets — teller totals,
settlement registers, card-processor reports — are exactly the documents that
cannot be committed as fixtures: they are proprietary, and they carry account
data. So the fixtures have to be *representative rather than real*, and that is
a genuine risk to the design: §7's worked example is invented, and a report
parser validated only against invented reports is a parser that handles invented
reports.

Two mitigations, both worth doing:

- **Public print-image analogues.** What qualifies is narrow — the file must be a
  *human-readable page image*, not a record-coded interchange format. BAI2 and
  NACHA look relevant and are not: every line carries a leading type code, so
  they are parsed by record type, and spatial anchoring buys nothing. The right
  class is pre-XBRL **SEC EDGAR ASCII filings** (fixed-width financial data
  schedules and 13F tables in plain-text submissions), archived statistical
  releases that predate their PDF/XML era, and sample report *output* printed
  inside vendor documentation for mainframe report writers.
- **Structure without data.** A spec is a description of layout, not of content.
  A fixture can reproduce a proprietary report's *shape* — page height, header
  block, column positions, subtotal placement, rollup depth — under wholly
  invented numbers and names, and still exercise every hard part of the engine.
  That is the practical route when the real file cannot leave its building.

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

- **Regex**: compile once with `regex(p)` and hoist it out of the loop. That is a
  real cost, and making it explicit is why it is now controllable.
- **The hidden cache would have broken ARI** (found 2026-08-01, while revising
  §3). The original single-entry cache held only the *last* pattern. An ARI spec
  with N field patterns walking M report lines alternates between all N on every
  line, so every lookup evicts the previous one and **every pattern recompiles M
  times** — the cache would have had a 0% hit rate on precisely the workload
  Layer 1 exists to serve. With a `regex` value, ARI compiles each field's
  pattern **once when it parses the spec** and reuses it across the whole report.
  This was not the reason for the redesign; it was found by it.
- **String building**: `s = s + x` in a loop is still O(n²) — the one trap that
  was not fixed. Accumulate into an array and `join` once.
- **Memory bound**: ARI holds the whole report in memory as lines. That is the
  right call for filings and reports and the wrong one for multi-gigabyte
  streams; it is stated as a bound, not hidden.

## 11. Non-goals (v1)

- **PCRE power features** — lookaround, backreferences in the pattern,
  non-greedy quantifiers, named groups. POSIX ERE doesn't offer them; the
  optional `HAVE_LIBPCRE2` upgrade (§14) is the path if they're needed.
- ~~**A compiled-pattern handle type**~~ — **reversed 2026-08-01.** This was a
  non-goal on the grounds that a hidden cache gave the benefit "without adding a
  stateful value kind or a dispose story." The premise was wrong twice over: the
  cache did not give the benefit (§10), and a compiled pattern is **not stateful**
  — it is immutable, so it needs no dispose story any more than a `date` does.
  `regex(p)` is now the design (§3).
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

**B. This phase ships LAYER 0 ONLY** — tested, documented, with a cookbook entry.
ARI is a later phase, designed against a surface that exists rather than against
a specification of one. (Decision D is what this decision was for: the surface
changed shape before a line of C was written, which is the cheapest possible
moment for it to happen.)

**C. ARI type keywords: the four generic ones** (`date`, `money`, `integer`,
`decimal`). Domain identifiers such as `cik`/`accession`/`cusip` stay
user-defined via `type` with a regex, rather than putting EDGAR vocabulary in a
general text library. Revisit once ARI has run against real filings.

**D. The Layer 0 surface is a `regex` VALUE KIND plus OVERLOADS of the existing
verbs** (decided 2026-08-01, superseding the six `re_*` builtins of decision B's
original phrasing). Nothing had been implemented yet, so this cost only the
documentation.

*Superseded surface, for the record:* `re_test`, `re_find`, `re_find_all`,
`re_replace`, `re_split`, `re_groups` — six new names, prefixed because
`find`/`replace`/`split` already mean literal operations.

*Why it changed.* The prefix existed only to dodge a name collision, and a
namespace dissolves the collision instead. Overloading by argument kind is
already the house idiom, not a new mechanism: `find` dispatches on
`VALUE_STRING` vs `VALUE_ARRAY` (`src/eval.c:19577`) to do two entirely different
jobs under one name. Passing a string keeps the literal meaning, so existing
programs are unaffected by construction.

*What it costs, stated plainly:*

- A new value kind is not free — `VALUE_REGEX` needs copy, free, print, compare,
  and a `SER_` tag for actor serialization (the existing set runs `SER_NULL`
  through `SER_FUNCTION`, `src/eval.c:7535`). The project has paid this price
  before for `date`, `money`, and `file`; a half-typed tagged record dispatched
  by magic field name was the alternative and was rejected as the odd one out.
- Three shipped core verbs get touched. The only negative test pinning their
  messages is `tests/negative_replace_type.err`, which pins the *first*
  argument's message, so overloading the second does not disturb it.
- `find` cannot join (return shape, §3), so the symmetry is imperfect and that
  asymmetry is permanent.

*Rejected alternative: a `/pat/` regex literal.* `TOKEN_SLASH` is division and
nothing else (`src/lexer.c:458`); telling division from a pattern opener requires
knowing whether the previous token ended an expression — the JavaScript lexer
hack — inside a lexer already carrying three context-sensitive modes. `PLAT-CLAUSE`
and `PLAT-CLAUSE-B` were both recent repairs to ambiguous `(`; a second ambiguous
punctuation mark is not worth syntax sugar. **ARI keeps `/regex/` in its spec
text**, where the grammar has no division and therefore no ambiguity.

**E. `match`, not `search`.** It pairs with "match record." It scans rather than
anchors, diverging from Python's `re.match`, and that divergence is a silent
trap — so it is documented in §3 and gets an `UNLEARN.md` bullet.

**F. A non-participating capture group is `unknown`**, distinct from a group that
matched the empty string (`""`). POSIX distinguishes them at no cost and
collapsing them would discard information.

**G. `contains` is EXTENDED to strings — substring *and* regex** (decided
2026-08-01). Decision D assumed `contains(s, regex(p))` on the strength of §0's
claim that `contains` was a literal *string* operation. **That claim was false**:
`contains` is array-only (`src/eval.c:19646`) and raises on a string first
argument. Rather than route around it, `contains` gains the string case it was
already assumed to have, in both forms:

```
contains(array, element)      ' unchanged — membership
contains(s, "sub")            ' NEW — literal substring
contains(s, regex(p))         ' NEW — pattern occurs anywhere in s
```

All three return a boolean, so the verb keeps one meaning: *does this haystack
contain this needle?* The literal-substring case is a small scope addition beyond
regex, taken deliberately — it was a real gap (`contains("hello","ell")` raises
today), and it is what makes the regex case an overload rather than a special
case bolted onto an array verb.

*Safety of the change:* the string-first-argument path is currently a raise, so
no existing program can depend on it, and no test in `tests/` pins the
`contains expects an array` message (checked). The array path is untouched.

The alternatives — spelling the test `not is_unknown(match(s, p))`, or adding a
fifth name `matches(s, p)` — were rejected as clunky and as surface growth
respectively.

**H. Repeating page furniture is removed by a grid preprocessing pass.** Raised
and resolved 2026-08-01.

The target domain is paginated print-image reports from mainframe-era systems
(teller totals, trial balances, transaction registers). Those files are not a
clean stream of data lines: a logical section is interrupted every 55–66 lines
by a **page break and a repeated header block** — form feed (`\f`, 0x0C), report
title, run date, page number, and a re-printed set of column headings. Footers
and continuation banners appear the same way.

`starts`/`ends` cannot express this. They bound a region **once**; page furniture
*recurs inside* the region they bound. A `section transactions starts(...)
ends(...)` spanning four pages contains three header blocks, and every one of
them will be walked by the field anchors as if it were data — matching a column
heading that reads `AMOUNT` when the anchor is looking for the literal
`"AMOUNT"`, which is exactly the pattern such a spec would use.

This also cuts at the **line grid** (§4), not just the spec syntax: if pages are
excised, the grid the `up`/`down` directions count over is no longer the physical
file, and a distance of `up 2` means something different before and after the
excision. That is a core model decision, so it belongs before implementation
rather than after.

**RESOLVED 2026-08-01**, on production experience of the modeled report:

- **Form feeds are present** in the real document (the hand-made
  `teller_totals.rpt` fixture omits them; that is a fixture defect, not a
  property of the format).
- **A page break can occur ANYWHERE — including mid-section.** Pagination is
  driven by lines-per-page, which is unrelated to the report's logical
  structure. A teller's detail table can be cut in half by a form feed, a page
  header, and a resumption.
- The report contains **many branches**, each with the full teller structure, so
  branch sections begin at arbitrary points and are themselves subject to being
  split across pages.

That settles the mechanism. Because a page break is **independent of logical
structure**, page furniture cannot be handled inside the section machinery at
all — there is no section-relative rule that describes "somewhere in the middle,
possibly between any two lines." It must be removed **before anchoring, as a
preprocessing pass over the line grid**:

```
raw lines ─► strip page furniture ─► CLEAN GRID ─► section/field anchoring
```

**This is also the answer to the grid question.** `up`/`down` count over the
**clean grid**, never the physical file. A section spanning three pages sees its
lines as contiguous, which is what makes an anchor like `down 1` mean the same
thing whether or not a page happened to break at that point. Line numbers
reported in diagnostics should map back to physical lines, so a user can find
the line in the original file.

**The furniture declaration is report-scoped, not section-scoped.** This follows
from the pass ordering rather than being a style choice: furniture is removed
*before* any section is located, so at the moment it runs there is no section to
attribute a declaration to. It is declared once at the top of the spec and
applies to the whole document.

**What constitutes furniture** is spec-declared, in two forms so that both
report styles are expressible:

1. **Form-feed anchored** (this report): furniture begins at `\f` and runs for a
   declared number of lines, or through the last line matching a declared header
   pattern.
2. **Pattern anchored** (no form feeds — the case the fixture accidentally
   modeled): furniture is any line matching a header pattern, plus a declared
   number of following lines.

Both must exist. Form feeds are the reliable signal where present, but a report
that lacks them is not thereby unparseable, and the fixture proved that case is
easy to encounter by accident.

*(Superseded lean, for the record: form-feed pagination as primary with an
`ignore` pattern as an escape hatch, on the assumption that page breaks align
with structure. The "anywhere, including mid-section" fact is what moved
furniture removal out of the section machinery and into a grid preprocessing
pass — a bigger change than choosing between the original three candidates.)*

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

### Phase 1 — regex in the core
The `regex` value kind (compile, copy, free, print, compare, serialize) plus
`match`, `match_all`, and the `contains`/`replace`/`split` overloads on the libc
engine, with the shorthand translation, flag string, match-record shape,
`unknown`-on-miss, and codepoint offsets. Golden + negative tests, and an
`UNLEARN.md` bullet for the `match`-scans-not-anchors divergence (§13.E).
Unblocks all linear text work immediately, including a first pass at cleaning up
`mdna.bas`.

### Phase 2 — ARI core

**Rebalanced 2026-08-01.** The previous split was internally contradictory: it
claimed Phase 2 was "enough to parse the worked example end to end" while
assigning **five** of that example's features to Phase 3 — `starts`/`ends`,
`flush`, the `1-5` distance range, the nested `transactions` section, and the
`date` type keyword. Phase 2's own success criterion was unmeetable by Phase 2.

The split now follows a real boundary: **Phase 2 is everything needed to locate
a field; Phase 3 is everything needed to convert one.** Structure before
coercion. That makes §7 parseable end to end (bar its one `date` keyword, which
Phase 2 returns as a string), and it front-loads the features that decide
whether ARI works on a real report at all rather than on a tidy fixture.

`stdlib/ari.bas`: `ari.parse` / `ari.import`; `section`/`field`; the five
directions; **exact distances, ranges (`2-10`, `3-`, `-8`) and `flush`**;
literal and `/regex/` patterns; **`starts`/`ends` section boundaries**;
**nested sections**; `break` (default + `on`); single-record and frame output;
`unknown` on not-found. Golden tests with inline specs.

The three promotions are not conveniences. **Section boundaries** are what
separate a detail block from the page furniture around it; **nesting** is how a
teller-within-branch-within-report rollup is expressed at all; and **distance
ranges** exist because real reports do not hold a constant label-to-value gap
across every row — an exact-distance-only engine matches the first row of a
column and misses the rest. Without these three, ARI parses examples.

Phase 2 also owns **page-furniture removal** (§13.H) — form-feed-anchored and
pattern-anchored — because it is a property of the line grid every anchor reads,
not a feature layered on top of one.

### Phase 3 — ARI advanced (conversion and reuse) — **COMPLETE 2026-08-01**
The built-in type keywords with native value conversion (`as money` yields a
money value, `as date` a datetime); regex-with-transform (`/re/repl/` with
`$1`..`$9` group references); custom `type` blocks (delivered early in Phase 2,
because the dialect problem forced them).

Construction goes through gBASIC's **assign modifiers** — `m(USD) = 12.34`,
`d(date) = "2021-12-27"` — not functions or literals. That surface is
undocumented in `docs/reference.md`, which is why an earlier note here claimed
no runtime date constructor existed; corrected in /DOGFOOD.md.

The `date` modifier **raises** on a malformed string — since 0.1.0-rc5. This
document and `stdlib/ari.bas` both claimed it did for months while it in fact
printed an unlocated line and assigned `nothing`, leaving the exit code at 0;
the discrepancy was measured on 2026-08-23 and closed by making the runtime
match the claim rather than by weakening the claim.

`_date_in` still range-checks month and day before calling the modifier, and
that is a separate decision: an out-of-range date in an imported report is an
expected condition, so the field degrades to `unknown` with an `invalid-date`
diagnostic while the rest of the row parses.

Tested by `tests/run_ari.sh`, including a cross-check that the `/re/repl/`
transform and the `-> dmy` dialect — two different mechanisms for one
conversion — produce identical dates.

Scoping note (§5.1): the type keywords cannot be single fixed patterns, and
cannot be one setting per report — a real report contradicts itself between
sections, because different sections were written by different people years
apart. Each keyword is a permissive recognizer over the union of common forms,
**overridable per section and per field**. Custom `type` blocks are therefore
not a convenience at the end of the list; they are the mechanism by which a spec
author resolves a document that disagrees with itself, and the built-in keywords
are pre-supplied common cases rather than the primary interface.

### Phase 4 — optional power engine + ergonomics
Optional `HAVE_LIBPCRE2` engine behind the same Layer 0 surface (lookaround,
non-greedy, named groups) selected at build time; optional `text.bas` ergonomic
wrappers if §13.3 favors them. Purely additive — the libc floor never regresses.
