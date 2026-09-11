# A textual form that keeps every gBASIC type

**Status:** Shipped (2026-09-11) as `stdlib/notation.bas` —
`notation.to_text` / `from_text` / `try_from_text`, `tests/run_notation.sh`.
Version 1 discards comments, by the decision recorded below.

The gap: `encode` is readable and **refuses** dates, money, durations and files;
`serialize` keeps every type and is **opaque binary**. There is no form a person
can open in an editor, read, and hand-edit that survives a typed value.

---

## Decided

**1. The type tag goes on the KEY side: `issued {date}: "2026-03-15"`.**

It does not collide with PBI (which uses parens), it costs **0 grammar
conflicts** where the value-side form costs 1, and — the reason that matters
more than the count — **it is the language's own typed-assignment syntax with a
colon where the equals goes**:

```basic
issued {date}= "2026-03-15"     ' gBASIC, today
issued {date}: "2026-03-15"     ' the format
```

The two brace positions already mean different things in gBASIC, and only one
of them means what a type tag means:

| position | what gBASIC already means by it |
| --- | --- |
| **before the separator** — `x {date}= "…"` | **this value is of this type** |
| **after the value** — `name {caseless} = "joe"` | compare *through this lens* |

**2. Array elements use the same tag with the name dropped:
`[ {date}: "2026-01-01", … ]`.** One spelling everywhere, no exception to the
round-trip promise.

**2a. A tag on an array is a DEFAULT for its elements, and an element may
override it.** So the array-level form and the element-level form are not
alternatives — they compose:

```
due_dates {date}: [ "2026-04-14", "2026-05-14" ]        ' both are dates
prices {USD}: [ "19.99", "3.459", {JPY}: "500" ]        ' the third is not
mixed: [ {date}: "2026-03-15", {USD}: "19.99", "plain" ] ' no default at all
```

This is the common case written short and the awkward case still writable,
which is what makes one spelling affordable.

Four sub-rules, because each of them is a place a format quietly does the
wrong thing:

- **A tag on an array means "default for elements". A tag on anything else
  means "this value is of this type".** There is no ambiguity to resolve:
  gBASIC arrays are untyped, so "this array is a date" has no meaning.
- **The default applies to every element that does not carry its own**, whatever
  its shape — including a bare number. `prices {USD}: [ 7 ]` is money, not a
  number. An author who wrote the default said what they meant; silently
  exempting some elements would make the rule depend on the data.
- **A scalar the tag cannot parse is REFUSED, never skipped.**
  `{date}: [ "not-a-date" ]` is an error. Skipping would produce a plausible
  half-tagged array, which is the failure this whole format is arranged
  against. (A nested *record* is not a refusal — the default stops there by the
  rule below, rather than failing to apply.)
- **A default CASCADES through nested arrays and STOPS at a record.**
  `rows {date}: [ [ "2026-01-01" ], [ "2026-02-01" ] ]` is a matrix of dates,
  written once. It stops at a record because a record's fields have *names* and
  each can tag itself — cascading in would make `{USD}: [ { price: "1.00",
  name: "Ada" } ]` try to turn a name into money. Arrays are positional and
  their elements are alike; records are named and theirs are not.
  An element may override the default at any depth, and its override becomes
  the default below it.

**2b. The document root is a record or an array.** A bare typed value at the
root has no field name to hang a tag on, and inventing a spelling for a case
nobody needs is worse than refusing. `encode(5)` works today, so this is a
deliberate asymmetry with a stated reason rather than an oversight. A root
array may carry a default, so a document that is simply a list of dates is
`{date}: [ "2026-04-14", … ]`.

**3. The format has its own parser, and its output is NOT gBASIC source.**

This is what makes decision 2 affordable. Every conflict count below matters
only if *gBASIC itself* must parse the format; a hand-written parser resolves
`{date}` versus `{date:` with one peek, and bison's LALR(1) limit is the
obstacle rather than any real ambiguity. Built and measured, adding the
nameless element form to gBASIC's grammar **breaks `{ x: 1 }` everywhere** —
so that door is shut for the language and open for the format.

**And pasteability was never reachable anyway.** gBASIC refuses `\u{0}` in a
string literal and directs the author to `chr(0)` — a *call*, not a literal —
so a record whose string holds an interior NUL, which field names may now
contain, cannot be written as gBASIC source at all. "Pasteable except for
values the language can hold but not spell" is a promise with an asterisk.

**4. Money is written with trailing zeros trimmed.** `string()` is lossy and
drops the currency (`3.459` prints as `3.46`), so the value comes from
`money.text()` — but `1234.560000` is not what anyone wants to read, and
trailing zeros in decimal do not change the value.

**5. Strings and comments follow gBASIC's own rules, with exactly one
divergence — and the divergence has a reason rather than a preference.**

Escapes are gBASIC's: `\n`, `\t`, `\"`, `\\`, `\u{...}`. Comments are `'` to
end of line, which is free because strings are double-quoted; a `'` inside a
string is content, as it is in gBASIC.

**The divergence is `\u{0}`, which gBASIC refuses and the format must allow.**
Checking *why* it refuses turned out to matter: gBASIC's AST stores a string
literal as a plain `char *` (`ast_string(char *value)`, read back with
`value_string(expr->as.string)`), so a literal NUL would be **silently
truncated at evaluation**. The refusal is **load-bearing, not vestigial** —
PLAT-NUL made string *values* counted and did not change how a *literal*
reaches them.

The format is not affected by that, because its parser builds Values directly
(`value_string_n`) with no AST hop. So it allows `\u{0}`, and this is the only
place its string rules part from the language's.

Two consequences worth stating rather than discovering:

- The format can spell **every** string gBASIC can hold, including the interior
  NUL that field names may now carry. That is what keeps the round-trip
  promise total.
- Making gBASIC itself accept `\u{0}` would mean giving AST string literals a
  length. That is a real change and a separate one; it is what would make the
  format's string rules *identical* rather than nearly so.

## A separate question this raised, worth deciding on its own merits

**Should `{ issued {date}: "…" }` be legal gBASIC?** Today a typed value cannot
be put in a record literal at all — you must build it on its own line and then
place it. The key-side production is **0 conflicts**, reads as the language's
own typed assignment, and closes a real ergonomic gap. It is independent of the
serializer: worth doing whether or not the format's output happens to match it.

## What building it found

Three things, none visible by reading, and each caught by a different tier.

**A nested array's default was computed and then dropped.** `_common_tag` only
looked one level, so a matrix of dates found no common tag at the top — every
element being an array — while the tag was computed one level down and never
written. The values came back as **plain strings**. The round-trip tier caught
it; the encoder now recurses as deep as the decoder cascades.

**A re-raise issued while `on error goto next` was still armed vanished.** The
wrapper that attaches a line number to a modifier's message caught the raise,
cleared it, and raised a better one — *inside the frame that was still
catching*. PLAT-ERR's frame scoping means the frame caught its own raise, so
**every refusal in the library silently stopped refusing**: five checks went
red in one edit. `on error stop` before the re-raise is the fix.

**A raw NUL round-trips perfectly, so nothing forced the escape.** The
perturbation that deleted `\u{0}` from the encoder passed every test — the byte
survives as itself. But a text file carrying raw control bytes is hostile to
the editor this format exists to be opened in, so correctness was not the whole
requirement: every control byte is escaped now, and the tier asserts the file
contains **no raw NUL** rather than merely that the value survives.

And a fourth, which is only funny: **the library that writes `\u{0}` cannot
write it as a literal.** Its escapes are built from `chr(92)`, for exactly the
reason the format exists.

## Still open

- **Comment PRESERVATION.** Decided for v1: **discard**, because the
  destructive case is narrow — a program *rewriting* a hand-edited file — where
  the ordinary loop is generate, review, read back. Still the open question for
  a later increment, and it is an API fork rather than a detail: a comment
  surviving a read-modify-write means `from_text` cannot answer with a plain
  record. The syntax is decided (`'` to end of line); whether
  a read-modify-write cycle keeps a reviewer's comment is not. Dropping one
  silently is a defect, and preserving them is a much larger commitment than it
  looks — it means the decoder cannot simply discard them.
- **Diagnostics.** Hand-edited means the parser's error messages are part of
  the feature: a misspelled currency, a bad date, an unclosed brace.
**6. The tag namespace resolves by case, checked against types first.**

`{date}` is a type and `{USD}` is a currency — gBASIC's own modifiers already
mix the two, so the format inherits it. Measured, the two sets do not actually
collide: type names are **lowercase** (`date`, `datetime`, `time`, `duration`,
`file`, `dir`) and `money.of` refuses a lowercase code (`money.of: usd is not a
known currency`). Worth pinning anyway, because `dir` is three letters like a
currency code and a length heuristic would misread it — the rule is **the known
type names first, then the currency table**, never a guess about shape.

**7. It can be written in gBASIC — measured, not assumed.** A decoder has to
build typed values from a tag known only at run time, and every kind is
reachable:

| tag | how a gBASIC decoder builds it |
| --- | --- |
| `date` `datetime` `time` `file` `dir` | a literal modifier inside a per-tag branch |
| any of 178 currencies | **`money.of(code, text)`** — takes the code as a *string* |
| `duration` | composed arithmetically: `(n * (1 days)) + (m * (1 hours))` |

`duration(text)` does **not** exist, so a duration is parsed into components and
composed. That is the only kind needing more than a direct constructor, and it
is the reason to check rather than assume: had one kind been unreachable, this
would have had to be a C builtin like the other three serializers.

- **Pretty-printing**: indent width, when a line breaks, and whether a key is
  quoted only when it is not a valid identifier.

---

## The evidence, kept because the decisions rest on it

What follows is the same record written several ways, plus the cases that make
the choice bite. **The values are real** — every rendering below was produced by
the interpreter, not written from memory.

---

## Four measurements that constrain this before any taste enters

**1. A typed value cannot be written inside a record literal today.**

```basic
r = { when: {date}"2026-03-15" }
' parse error: syntax error, unexpected RBRACE, expecting OP_EQ or COLON
```

You must build it first and then place it. So "the output is just gBASIC source
you could paste back" is **not reachable without a grammar change**.

**2. `string()` on money is LOSSY, and drops the currency.**

| value | `string()` | `money.text()` | `money.currency()` |
| --- | --- | --- | --- |
| USD 1234.56 | `1234.56` | `1234.560000` | `USD` |
| USD 3.459 (fuel, sub-cent) | **`3.46`** | `3.459000` | `USD` |
| JPY 500 | `500` | `500.0000` | `JPY` |
| KWD 12.345 | `12.345` | `12.3450000` | `KWD` |

`string()` rounds to the minor unit, so a sub-cent price **prints wrong**. The
format must use `money.text()` and trim trailing zeros — trailing zeros in
decimal do not change the value, and `1234.560000` is not what anyone wants to
read.

**3. The other types already have a faithful text form**, and a duration's is
already its literal syntax:

```
datetime  2026-03-15 14:30:05      date  2026-03-15       time  14:30:05
duration  2 days 3 hours 15 minutes
file      /etc/hostname            dir   /tmp
number    0.30000000000000004      inf   nan
```

**4. gBASIC record literals need commas and reject a trailing one.**

```basic
r = { a: 1,
      b: 2 }        ' fine — multi-line is already legal
r = { a: 1, b: 2, } ' parse error: unexpected RBRACE
r = { a: 1
      b: 2 }        ' NOT newline-separated: reads as a duration
```

Quoted keys and keyword keys both work: `{ "a b": 1, end: 2 }` is a two-field
record. So the only question about keys is *when to quote*, not *whether*.

---

## The sample record

A small invoice — money at three precisions, two date kinds, a duration, a
file, a nested record, an array of records, and both empty values.

### Variant A1 — modifier prefix, unquoted keys (SUPERSEDED by the key-side form; kept because the comparison below refers to it)

```
{
  id: 4471,
  issued: {date}"2026-03-15",
  posted: {datetime}"2026-03-15 14:30:05",
  customer: {
    name: "Ada Lovelace & Co.",
    account: "ACME-0042",
    contact: unknown
  },
  lines: [
    { sku: "GB-100", description: "Widget, large",  qty: 2, unit: {USD}"19.99", total: {USD}"39.98" },
    { sku: "GB-205", description: "Fuel surcharge", qty: 1, unit: {USD}"3.459", total: {USD}"3.459" }
  ],
  subtotal: {USD}"43.439",
  tax: {USD}"3.62",
  total: {USD}"47.06",
  terms: {duration}"30 days",
  attachment: {file}"/var/spool/invoices/4471.pdf",
  paid_at: nothing,
  notes: ""
}
```

### Variant A2 — modifier prefix, quoted keys (what `encode` would grow into)

```
{
  "id": 4471,
  "issued": {date}"2026-03-15",
  "customer": {
    "name": "Ada Lovelace & Co.",
    "contact": unknown
  },
  "total": {USD}"47.06",
  "terms": {duration}"30 days",
  "paid_at": nothing
}
```

### Variant B — sigil inside a string

```
{
  "id": 4471,
  "issued": "@date 2026-03-15",
  "customer": {
    "name": "Ada Lovelace & Co.",
    "contact": "@unknown"
  },
  "total": "@USD 47.06",
  "terms": "@duration 30 days",
  "paid_at": null
}
```

**B's selling point is that a JSON editor still highlights and folds it — and
that only holds if `nothing` becomes `null` and `unknown` becomes a tagged
string.** Two distinct gBASIC values then both look like absence to a JSON
reader, and one of them silently stops being distinguishable by eye. `inf` and
`nan` have the same problem and need tagging too (`"@inf"`).

That is the trade in one line: **B buys tooling by spending the very
distinction the format exists to preserve.**

---

## The cases that decide it

### The `{` collision, which is the honest worry about A

```
  customer: {                      ' a record opens
    name: "Ada"
  },
  issued: {date}"2026-03-15",      ' a TYPE TAG, not a record
```

Both start `{`. They are unambiguous one token later — a type tag has no colon
and is immediately followed by a string — but a reader skimming a column of
braces is the person this format is for. **Worth eyeballing above before
deciding it is fine.**

### Keys that are not identifiers

A record used as a map has keys that cannot go unquoted:

```
{
  "total volume": 4471,            ' a space
  end: 2,                          ' a keyword -- legal unquoted in gBASIC
  "rate (%)": 0.0425,              ' punctuation
  "": 9                            ' the empty name is a legal key
}
```

Under A1 this is a **mixture** of quoted and unquoted keys in one record. Under
A2/B every key is quoted and the file is uniform. Uniformity is easier to read
in bulk; unquoted is easier to read one line at a time and is what gBASIC
source looks like.

### Empty, absent, and not-a-number

```
{
  none: {},                 empty record
  nolines: [],              empty array
  missing: nothing,         a value that is deliberately absent
  unasked: unknown,         a value nobody has supplied
  overflow: inf,
  bad: nan
}
```

`nothing` and `unknown` are **different values** in gBASIC and both round-trip
through `encode` today. Any format that cannot tell them apart is a step back.

### Strings that fight the format

```
{
  quote: "she said \"go\"",
  path: "C:\\logs\\2026",
  newline: "line one\nline two",
  nul: "a\u{0}b",                  ' PROBLEM: gBASIC REFUSES \u{0} in a literal
                                  ' ("use chr(0)"), so this is the one value
                                  ' the format CANNOT spell the way the
                                  ' language does
  unicode: "café 日本語 😀"
}
```

The NUL case is not hypothetical: field **names** may contain one as of rc9, so
a key can too. And it is the **one place where "the output is gBASIC source"
provably cannot hold**: gBASIC refuses `\u{0}` in a string literal by design and
directs you to `chr(0)`, which is a *call*, not a literal — so a serializer
must either invent an escape the language does not have, or refuse a value the
language can hold. Measured, not assumed:

```
invalid unicode escape: \u{0} is not allowed in a literal; use chr(0)
```

This one case may be enough to settle question 3 on its own.

### Money at every precision, which is the type most likely to be reviewed

```
{
  ordinary: {USD}"1234.56",
  sub_cent: {USD}"3.459",          ' string() would print 3.46 -- WRONG
  no_minor_unit: {JPY}"500",
  three_places: {KWD}"12.345",
  negative: {USD}"-0.01",
  zero: {USD}"0"
}
```

### A big flat record, where indentation earns its place

`encode` writes one line. This is the same data with newlines:

```
{"id":4471,"issued":...,"customer":{"name":"Ada Lovelace & Co.","account":"ACME-0042"},"lines":[{"sku":"GB-100",...
```

**Pretty-printing may be the larger half of the readability win**, independent
of the type tags — and it is the half `encode` could gain on its own.

---

---

## Matthew's proposal: put the tag on the KEY side (2026-09-10)

```
{
  id: 4471,
  issued {date}: "2026-03-15",
  customer: {
    name: "Ada Lovelace & Co."
  },
  total {USD}: "47.06",
  terms {duration}: "30 days",
  paid_at: nothing
}
```

**It does not conflict with PBI**, and that is a fact rather than a hope: PBI's
per-field policy uses **parens** — `name (copy): "unnamed"` — so the brace form
is free. The reference's warning about `cost(USD): 9.99` not being a money field
is about the paren spelling, which PLAT-BRACE retired for modifiers.

**And it is measurably better than the value-side form this document proposed
first.** Each row below is the real bison conflict count, from adding the
production to a copy of `src/parser.y`:

| form | conflicts |
| --- | --- |
| baseline (today) | 0 |
| **key-side** `issued {date}: "..."` | **0** |
| bare-word `issued: date"..."` | 0 |
| key-side + bare-word in arrays | 0 |
| value-side `issued: {date}"..."` | **1 shift/reduce** |
| key-side + nameless `{date}:` in arrays | **1 shift/reduce — and it breaks `{ x: 1 }`** |
| suffix on a string literal `"..."{date}` | **1 shift/reduce** |
| suffix on any expression `expr{date}` | **2 shift/reduce** |

That matters in this project: `IDENT expression` as a statement form was
rejected over **4 measured conflicts**, so 1 is not nothing.

**The conflict is exactly the ambiguity a reader would also feel.** Bison's
counterexample is `{` followed by `IDENT`: a record literal's field name
(`{ date: ... }`) and a type tag (`{date}"..."`) are indistinguishable at that
point, and the PBI production sitting in the same place is what makes the
lookahead insufficient. The key-side form dissolves it because a `{` in *value*
position then unambiguously opens a record — which is the whole reason the page
above flagged the collision as the one thing option A could not argue away.

### The one hole, and it is structural

**An array element and a top-level value have no key to hang a tag on.**

```
dates: [ ???"2026-01-01", ???"2026-02-01" ]
text_encode(aDate)          ' no record, no key
```

This is not hypothetical: a heterogeneous array holding a date, money, a string
and a number is an ordinary gBASIC value, and `serialize` round-trips one today.
A textual format that cannot is not a replacement for it.

Three ways to close it, and the choice is a real trade:

**(a) Tag the array, not the element.** `dates {date}: [ "2026-01-01", ... ]` —
needs **no new grammar at all**, since it is the key-side production with an
array on the right. Reads well. **Fails on a mixed array**, so the round-trip
promise would have to carry an exception, and an exception in the one promise
this format exists to keep is expensive.

**(b) A nameless tag in element position.** `[ {date}: "2026-01-01" ]` — one
spelling everywhere, and it costs the **1 shift/reduce conflict** back.

**MEASURED BY BUILDING IT, AND IT IS NOT VIABLE.** The conflict is not benign:
bison resolves shift/reduce by shifting, so on `{` then IDENT the parser commits
to the type tag and expects `}`. An ordinary record literal whose first key is a
bare identifier then **stops parsing at all**:

```
a = { x: 1 }            PARSE ERROR: unexpected COLON, expecting RBRACE
a = [ { date: 1 } ]     PARSE ERROR: unexpected COLON, expecting RBRACE
a = f({ x: 1 })         PARSE ERROR: unexpected COLON, expecting RBRACE
a = [ { "x": 1 } ]      ok   -- a QUOTED key survives, since STRING is not IDENT
a = [ {date}: "..." ]   ok   -- the new form works, at that price
```

So the cost is not "one conflict on the new syntax" but **the most common
record literal in the language**. Left-factoring to recover it was tried and is
worse: sharing the `LBRACE IDENT` prefix with the record production takes the
grammar from 1 conflict to **32**.

This is the concrete form of the worry that prompted the experiment — *"the
parser just sees it as a record value"*. The truth turned out to be the mirror
image and worse: **the parser sees a record value as a type tag.**

**(c) A bare-word tag in element position.** `[ date"2026-01-01" ]` — **0
conflicts**, works at top level too, and is the only option with no exception.
Its cost is a **second spelling** for a typed literal, which is the objection
this page already raised against variant B.

### The suffix form, measured (2026-09-10)

```
dates: [ "2026-01-01"{date}, "2026-02-01"{date} ]
```

**It costs 2 shift/reduce conflicts, and the reason is a shipped feature rather
than an accident of the grammar: the slot after a value is already taken by the
COMPARISON LENS.**

```basic
if name {caseless} = "joe" then     ' this works today
```

**And a comparison lens is legal inside brackets** — measured, not assumed:

```basic
n = "JOE"
r = [ n {caseless} = "joe", 1 < 2 ]   ' [true,true]
q = { flag: n {caseless} = "joe" }    ' true
```

An array element is a full expression, so it can hold a comparison, and a lens
is an expression-level construct rather than a statement one. There is no
bracket-shaped exemption to carve out.

**The position is claimed by the LEXER, not just by the grammar**, which is a
harder wall than a shift/reduce conflict:

```
comparison_lens
    : LBRACE { lexer_begin_lens_content(ctx->active_lexer); } LENS_CONTENT RBRACE
```

The parser switches the lexer into `lens_content_mode` the instant it sees `{`
after a value, so the bytes inside are consumed as raw lens content before the
grammar ever sees an identifier. A suffix type tag there is not a preference
the parser could be taught — `date` would not arrive as a token at all.

Restricting the suffix to a string literal does not help — still 1 conflict,
with bison naming the lens as the alternative:

```
First example:  PRINT STRING • LBRACE IDENT RBRACE
Second example: PRINT STRING • LBRACE $@1 LENS_CONTENT RBRACE comparison_operator ...
```

### Which is what makes the key-side form the right one

The two positions already have meanings in this language, and they are not the
same meaning:

| position | what gBASIC already means by it |
| --- | --- |
| **before the separator** — `x {date}= "…"` | **this value is of this type** |
| **after the value** — `name {caseless} = "joe"` | compare *through this lens* |

So `issued {date}: "2026-03-15"` is not merely conflict-free — it is the
language's own typed-assignment syntax with `:` where `=` goes. A reader who
knows `x {date}= "…"` needs to learn nothing. A suffix tag would be a **second
meaning for a slot that already has one**, which is what the conflict count is
reporting.

### Where that leaves it

Key-side wins on every count the page asked about — no PBI collision, no reader
ambiguity, fewer grammar conflicts than the alternative — and inherits one
question the value-side form did not have: **what a bare typed value looks like
when there is no field name.** Question 3 below is now narrower and sharper:
not "should the output be gBASIC source" but "**is a mixed array of typed values
worth a second spelling, or worth an exception?**"

## Questions the samples raise

1. **Does the `{` collision bother you on the page?** It is the one thing A
   cannot argue away, and it is a matter of eye rather than logic.
2. **Quoted keys always, or unquoted where legal?** Uniform bulk reading versus
   gBASIC-shaped lines and pasteability.
3. **Does the file have to be pasteable gBASIC**, or only *readable* by someone
   who knows gBASIC? Pasteable requires the grammar change; readable does not.
   The grammar change reintroduces one token of lookahead after `{IDENT}`, which
   is the class of guess PLAT-BRACE was built to remove — small here, since
   `{x}` with no colon cannot be a record, but not free.
4. **Hand-edited means the parser's diagnostics are part of the feature.** A
   misspelled currency, a bad date, an unclosed brace — each needs a message
   that names the line. Does it also need to preserve **comments** across a
   read-modify-write cycle? That is a much larger commitment than it looks.
5. **One more serializer, or grow `encode`?** `encode` already refuses exactly
   these types, and accepting them is strictly more permissive — no correct
   program changes behaviour. Against: `encode`'s output would then sometimes be
   nearly-JSON and sometimes not, depending on the data, which is worse than a
   clean split.

---

## The decided form, end to end

```
{
  id: 4471,
  issued {date}: "2026-03-15",
  posted {datetime}: "2026-03-15 14:30:05",
  customer: {
    name: "Ada Lovelace & Co.",
    account: "ACME-0042",
    contact: unknown
  },
  lines: [
    { sku: "GB-100", description: "Widget, large",  qty: 2, unit {USD}: "19.99", total {USD}: "39.98" },
    { sku: "GB-205", description: "Fuel surcharge", qty: 1, unit {USD}: "3.459", total {USD}: "3.459" }
  ],
  due_dates {date}: [ "2026-04-14", "2026-05-14" ],
  prices {USD}: [ "19.99", "3.459", {JPY}: "500" ],
  mixed: [ {date}: "2026-03-15", {USD}: "19.99", "plain", 7 ],
  subtotal {USD}: "43.439",
  tax {USD}: "3.62",
  total {USD}: "47.06",
  terms {duration}: "30 days",
  attachment {file}: "/var/spool/invoices/4471.pdf",
  paid_at: nothing,
  notes: ""
}
```

Those three arrays are what decisions 2 and 2a buy together: the homogeneous
case written **once** at the array, the same-but-one case written as a default
with an override, and the genuinely mixed case still writable — **one spelling,
and no exception to the round-trip promise.**
