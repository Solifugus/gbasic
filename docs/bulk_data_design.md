# Working with lists and records in bulk — 0.6.0 considerations

**Status: Proposal.** None of this exists. It is a list of things to consider
for 0.6.0, with the measurements that say which of them would actually remove
loops *in this tree* rather than in Python.

Written 2026-10-03 from a design conversation with Matthew. The prompt was list
comprehensions; the survey below moved the answer somewhere smaller and
cheaper.

---

## 1. What the loops in this tree are actually doing

Classified by body shape across `stdlib/`, `examples/` and `tests/` — 813 `for
each` loops and 1,539 `append` calls in stdlib alone:

| shape | count | is there a verb for it today? |
|---|---|---|
| **map** — the body is one `append` | **130** | no `map` exists |
| **filter** — `if` + one `append` | **45** | no `filter` exists |
| **min/max** | 23 | `min`/`max` exist, **numbers only** |
| **sum** over the loop variable | 22 | `sum` exists, **numbers only** |
| **`excluding`** — append if NOT in another list | **20** | nothing |
| **any/all** | 19 | nothing |
| group-by (key → list) | 6 | nothing |
| intersect | 4 | nothing |

Two things to read off that table.

**The `excluding` shape is real.** Matthew built an `excluding` keyword into a
hospital managed-care contract system years ago for exactly this, and the
instinct is measured rather than remembered: 20 places here write that loop.

**And a correction to my own first number.** I reported 499 "accumulate" loops;
that was a pattern over-matching every `x = y + z`. `sum`, `min`, `max`, `mean`
and `count` all already exist as builtins, so the genuine figure is 22 — which
is what pointed at §2.

---

## 2. The blocking primitive is smaller than map, filter or sort

The aggregates exist. They cannot reach data:

```basic
rows = [ { item: "hammer", amount: 19.95 },
         { item: "saw",    amount: 32.50 },
         { item: "nails",  amount:  7.00 } ]

sum([10, 32, 7])      ' 49
sum(rows.amount)      ' field access expects a record
sum(rows)             ' sum expects a numeric array
```

Every one of those 22 sum loops is `total = total + r.amount` — summing a
**field**. So the thing standing between this language and no-loops is not
`map`, `filter` or `sort`; it is that **there is no way to name a field across a
list**. Give it one and five existing aggregates start working on business data
for nothing.

**PROPOSAL: `rows.amount` is a projection** — the array of that field from every
element.

```basic
sum(rows.amount)               ' 59.45
max(rows.amount)               ' 32.50
count(rows.amount)             ' 3
sort(rows.item)                ' ["hammer", "nails", "saw"]
```

**The syntax is unclaimed, and that is a proof rather than a survey.**
Measured: `rows.length`, `rows.count`, `rows.amount` — every dotted access on an
array answers `field access expects a record` today. No working program contains
the shape, so claiming it cannot change one. (The same argument that made
`{IDENT}` safe for inline modifiers, and a stronger one than the enumeration
that the record/modifier classifier rests on.)

### What it does not solve

Projection **plucks**; it does not transform. Of the 130 map loops, the ones
that compute (`append(out, r.price * r.qty)`) are untouched by it, and so are
all 45 filter loops. That is why the comprehension question in §5 is priced
*after* this one rather than instead of it.

### Questions to settle before building it

- **A missing field.** A record without `amount` reads back as `unknown` today,
  so the projection would contain `unknown` and `sum` would refuse. That is
  probably the right chain — the refusal names the problem and `default` exists
  for the other intent — but *silently skipping* is the alternative and it is
  the shape that cost `examples/steward` an authentication bypass. Decide
  deliberately.
- **Assignment through a projection.** `rows.amount = 0` as a bulk update is
  genuinely useful and is also warning 2107's trap one level up — a write whose
  target is a copy. **Refuse it in v1** and say why, rather than leave it
  ambiguous.
- **Nesting.** `rows.customer.name` falls out for free if a projection returns
  an array and `.name` on an array projects again. Consistent, and worth
  asserting rather than discovering.
- **A non-record element.** `[1, 2, 3].amount` should refuse naming the element
  kind, not answer `[]`.
- **An empty list.** `[].amount` is `[]`; whether `sum([])` is `0` or a refusal
  is a separate existing question this inherits.

---

## 3. Sorting

**`sort` refuses records outright** — `sort supports only scalar array values`.
So a list of records, which is what business data *is*, cannot be sorted at all.

The consequence is in shipped stdlib: `stdlib/fundamentals.bas` hand-writes an
**insertion sort** (O(n²)) over a composite string key built by concatenation,
and that key is the same `_row_key` whose unguarded `fp` was fixed on
2026-10-03 — the key was `2023-12-31|nothing` for 59 of 4,096 rows and grouped
correctly only by accident.

Four ways to extend it, deliberately in this order:

1. **By a named field.** `sort(rows, by: "amount")`, with `descending:`.
   Covers nearly every real case, needs no function, and composes with §2.
2. **By several fields.** `sort(rows, by: ["last", "first"])` — the thing
   `_row_key` is faking with string concatenation, and the reason that bug
   existed.
3. **With a declared collation.** The default is alphanumeric standard, but
   business data wants case-insensitive, or numeric-within-text (`item2` before
   `item10`), or money- and date-aware. This is the **declare it, don't guess
   it** rule that `lending`'s accrual basis and `credit`'s delinquency method
   already follow, and the comparison-lens chain shipped in 0.5.0 is already
   the vocabulary for it: `sort(rows, by: "name", using: {trimmed; caseless})`.
4. **A comparator function**, JavaScript-style — `sort(xs, bylen)`. **Last, not
   first.** It works in principle today (function values exist and no capture is
   needed), but it needs a *named* function per sort, and a comparator is the
   one piece of code everyone gets wrong: `a - b` against `a < b`, and returning
   a boolean silently half-sorts. It is the escape hatch for what 1–3 cannot
   express, and if 1–3 are good it is rarely reached.

---

## 4. Set operations

```basic
kept = all_claims excluding paid_claims
both = a intersecting b
```

20 measured loops for the first, 4 for the second. Matthew's instinct that this
reads better as an **operator** than as `difference(a, b)` is worth keeping:
`excluding` says what it means at the call site and needs no argument order
convention to remember.

Questions: what decides membership (`=`, which PLAT-EQ made correct for
compounds, so records compare by value — probably right); whether duplicates are
preserved on the left (probably yes, since `unique` exists separately); and
whether these are operators or functions, which is a grammar cost to measure
rather than guess.

---

## 5. List comprehensions — priced after §2–§4, not before

```basic
totals = [p * rate for each p in prices]
```

175 map+filter loops is the headline figure, and it overstates the case: if
projection and set operations land, the residual need is much smaller than 175,
and this document would rather find that out than assume it.

Two things in its favour that are specific to gBASIC rather than borrowed:

- **It gives the ergonomics people want from lambdas without touching the
  closure invariant**, because there is no function object to capture into — the
  expression is evaluated inline, where the enclosing variables are simply in
  scope. `[p * rate for each p in prices]` just sees `rate`.
- It reuses `for each`, the language's own vocabulary, rather than Python's
  `for`.

**Lambdas are not on this list and should not be.**
`docs/first_class_functions_design.md` §2 makes the invariant explicit — a
function value is always a reference to a registered function, never an
anonymous capturing closure — for two reasons that are still true (no reference
cycles in a refcounted runtime; actor-sendable by name, since `spawn` is
fork+exec and both processes run the same program) and a third that document does
not name: **`encode` totality**, which is what lets an `agent` run sit in a
store between HTTP requests. §13 defers closures "only if real code demands
capture, and only with a cycle story", and nobody has measured the first half.

**The number that decides §5 is the conflict count**, and it is not measured
yet. This project rejected `IDENT expression` as a statement form over **four**
shift/reduce conflicts and priced the general lens form in expression position
at **19**. My hypothesis is zero — `[` opens an array literal, and after an
expression, seeing `for` instead of `,` or `]` is unambiguous because `for` is
already reserved — but a hypothesis is not a measurement and this section should
not be acted on until it is one.

---

## 6. Sequencing

| | why here |
|---|---|
| 1. **Projection** (§2) | smallest change, largest unlock, syntax provably free |
| 2. **`sort` on records** (§3) | fixes a shipped O(n²) hand-roll; needs §2's field naming |
| 3. **Set operations** (§4) | 24 measured loops, self-contained |
| 4. **`any` / `all`** | 19 loops, and possibly `any(rows.paid)` once §2 exists rather than needing a predicate at all |
| 5. **Comprehensions** (§5) | re-measure the residual after 1–4, then measure the conflict count |
| 6. `map`/`filter`/`reduce` | the fallback if §5 does not land, and clumsy without inline functions |

None of this belongs in 0.5.0, which is prepared bar the tag.
