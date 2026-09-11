# A textual form that keeps every gBASIC type — worked samples

**Status:** Samples for discussion (2026-09-10). Nothing decided, nothing built.

The gap: `encode` is readable and **refuses** dates, money, durations and files;
`serialize` keeps every type and is **opaque binary**. There is no form a person
can open in an editor, read, and hand-edit that survives a typed value.

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

### Variant A1 — modifier prefix, unquoted keys (most gBASIC-like)

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
| key-side + nameless `{date}:` in arrays | **1 shift/reduce** |
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
