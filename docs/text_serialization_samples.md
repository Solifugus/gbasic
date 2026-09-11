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
