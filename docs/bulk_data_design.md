# Working with lists and records in bulk — 0.6.0 considerations

**Status: §2 SHIPPED (0.6.0 increment 1, 2026-10-06); §3 SHIPPED (increment 2,
same day, items 1–2 of four); §2a, §2b, §4 proposed; §5 deliberately
unpriced.** The rest is a list of things to consider for 0.6.0,
with the measurements that say which of them would actually remove loops *in
this tree* rather than in Python.

Written 2026-10-03 from a design conversation with Matthew. The prompt was list
comprehensions; the survey below moved the answer somewhere smaller and
cheaper. Each section is annotated as it ships, including where building it
contradicted what was written here.

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

### SHIPPED 2026-10-06 — and the one thing this section did not anticipate

`tests/run_projection.sh`, 58 + 18 self-checking assertions plus shell-level
refusals, thirteen perturbations proven red. One site in `src/eval.c`, the single
place that raised `field access expects a record`. Every question above was
settled as this section guessed, except assignment, which is refused by name
(`append cannot change a projection: … change the array itself, or assign the
projection to a name first`) rather than merely refused.

**WHAT THE PLAN MISSED, AND IT WAS NOT SMALL: none of the five aggregates could
reach money.** §1 counted loops and §2 named `sum`, `mean`, `count`, `min` and
`max` as unlocked for nothing — and measured after projection worked, four of the
five still refused a money array:

```
sum(invoices.total)    sum expects a numeric array
mean(invoices.total)   mean expects a numeric array
max(invoices.total)    max supports only scalar array values
sort(invoices.total)   sort supports only scalar array values
```

`money` is this tree's exact business number — a loan balance, an invoice line,
a ledger posting — so the release whose subject is business data in bulk would
have shipped `sum(invoices.total)` refusing while the `+` operator added the same
two values happily. That is the `web.configure` shape: a capability announced
that does not reach the obvious case.

**Fixed by taking the answer from the operators rather than inventing one**, which
is what keeps it small. Ordering already knew what to do: `<` orders money within
a currency and refuses across one (PLAT-EQ's rule — equality answers, ordering
would invent a rate), and it orders exact durations and refuses month-bearing
ones (a month has no fixed length). The sorter simply did not know the same
things, so `value_sort_comparable` gained both kinds and
`array_all_sort_comparable` gained the two conditional refusals **in the
operator's own words** — in a pre-pass, because a `qsort` comparator returns an
`int` and cannot raise. The fold is the same argument one operator along: money
addition already refuses mixed currencies and is already overflow-checked, and
`mean` divides through the existing `money_scale_by`, so `sum`/`mean` over money
decide nothing new.

**That produced the suite's strongest tier, which was not in the plan either:
the `<` operator is the ORACLE for the sorter.** They are different code — a
raising branch chain against a comparator that cannot raise — asking one
question, so agreement is evidence rather than a second call into one place. The
tier walks eleven pairs and requires the same verdict *and*, where both answer,
the same direction; reverting money to its pre-0.6.0 state reddens it
immediately. It records the one deliberate divergence as a divergence: `<`
refuses an absence because there is no answer, while `sort` must place one and
ranks it lowest.

**`median`, `stdev`, `variance`, `percentile`, `quantile` and `correlation` stay
numeric**, stated rather than discovered: two of them are meaningless on money
(a variance of money is money squared) and none is among the five §1 measured.

**The diagnostics were part of the work, not a polish pass.** `sum expects a
numeric array` is true and says nothing about *which* row is wrong, and over a
projection that is the whole question — an array of absences means a row lacks
the field. It names the element now (`element 1 is unknown`), which also removed
an asymmetry this increment had introduced: `[{USD}"1.00", 7]` named element 1
while `[7, {USD}"1.00"]` answered the terse sentence. Naming a kind in a sentence
is one helper rather than an article at each site, because two rules were being
got wrong across three messages: "a array" and "a unknown" read as mistakes, and
`money`, `nothing` and `unknown` take **no** article — `money` being a mass noun
and the other two the words for having no value.

**Two perturbations worth recording** because each is caught by exactly one
check. A projection that **drops** the elements with no value leaves the sum, the
mean, the min and the max all *correct* — it only shortens the array — so what
catches it is the count and the absence checks, not any figure; it also flips
`all(rows.paid)` from `false` to `true`, reporting an unpaid row as paid. And a
`mean` over money that **rounds at the minor unit** prints `19.82`, identical to
the right answer, so only `avg * 3` separates them: `59.46` against `59.45`.

---

## 2a. What an aggregate does with an absence — SQL's answer, and its warning

§2's projection is only useful if `sum(rows.amount)` survives a ragged row, and
real business data *is* ragged. Matthew's question was whether `unknown` is meant
to behave like SQL's `NULL` here, and whether a **warning** makes more sense
than an error.

**SQL's answer is more designed than it first looks**, and it is a standard
rather than a convention:

```sql
-- values {10, NULL, 7}
SUM(col)    → 17      -- NULLs ignored
AVG(col)    → 8.5     -- 17 / 2, divided by the NON-NULL count
MIN / MAX   → ignore NULLs
COUNT(col)  → 2       -- non-NULLs
COUNT(*)    → 3       -- rows
SUM over all-NULLs → NULL, not 0
```

**And the SQL standard raises a warning for exactly this**: SQLSTATE `01003`,
*"null value eliminated in set function"* — class `01` being the warning class.
Some engines emit it; PostgreSQL does not. So "skip them, and say you did" is
the designed answer, and Matthew's instinct matches the standard.

### Why a warning works here, when the same shape failed this morning

The absence-coercion warning was **recommended and then withdrawn** on
2026-10-03 (see `DOGFOOD.md`) because it fired on correct code at **8 sites of
10**: showing that something is absent is an *idiom*, and
`print("find(zz)=" + find(f, "zz"))` is indistinguishable from the bug.

**There is no counterpart here.** Nobody writes `sum(rows.amount)` *in order to*
skip nulls — you write it because you want the total, and if a row has no amount
you want to know. So the expected false-positive rate is near zero, which is
row 9's bar in `warning_model_design.md` (shipped at 0) rather than row 7's
(reverted at 2).

That also answers §2's own steward worry: **silent** skipping is the dangerous
thing. Skipping *with a warning* is not silent, and `on warning stop` turns it
into a failure for a test run while production keeps the ergonomics.

### Today's behaviour is the strictest possible

```
sum([10, unknown, 7])   → RAISES "sum expects a numeric array"
sum([10, nothing, 7])   → RAISES
mean / max / median     → RAISE
count([10, unknown, 7]) → 3           (SQL's COUNT(*))
sum([])                 → RAISES "non-empty array"
```

So SQL semantics would be a **loosening**, which is exactly why it must not be
a quiet one.

### Four details where the wrong choice would be silent

1. **`mean`'s denominator.** SQL divides by the *non-null* count. Skip the nulls
   and divide by the full count and every average is silently too low. Assert
   it; do not assume it.
2. **All-absent is `unknown`, not 0.** "There was nothing to add" is not "the
   total is zero." This collides with an existing decision — `sum([])` *raises*
   today — so gBASIC would have `sum([])` raise while `sum([unknown, unknown])`
   answers `unknown`. Defensible (an empty list is a programming mistake;
   all-absent is a data fact) but decide it rather than inherit it.
3. **`count` has one meaning where SQL has two.** `count([10, unknown, 7])` is 3
   today, which is `COUNT(*)`; SQL's `COUNT(amount)` is 2. Keep `count` as
   length and add a separate way to ask for present values, because silently
   changing `count` moves existing answers.
4. **gBASIC has two absences and SQL has one.** `unknown` is the close match for
   `NULL` — *nobody knows*. `nothing` is the program's own data saying there is
   no value. Treating them the same in an aggregate is probably right, because
   the distinction does not help here and two rules are a trap people hit once a
   year — but it is a real question.

**Recommendation: skip, and warn.** SQL's semantics with SQL's warning. It is a
documented standard, the false-positive argument that killed the other warning
does not apply, and it is what makes the projection useful rather than refusing
on the first ragged row.

---

## 2b. `median` and `mode` — and two defects `mode` has today

Both are **builtins already**, along with `percentile`, `quantile`, `stdev` and
`variance`. The absence rule above applies to them unchanged: skip, warn, and
all-absent answers `unknown`.

`median` needs nothing else. Measured, it already takes the statistical
convention on an even count — `median([4,1,2,3])` is **2.5**, the mean of the two
middle values — which is the right default and worth pinning rather than leaving
to be rediscovered.

**`mode` has two defects, both present today and neither about absences.**
Measured:

| | answers | should be |
|---|---|---|
| `mode([1,1,2,2])` | **1** | a tie: two values are equally the mode |
| `mode([2,2,1,1])` | **2** | — and it is **source order**, not the lowest |
| `mode([1,2,3])` | **1** | there is **no mode**; nothing repeats |
| `mode([19.95, 32.50, 7.00, 4.25, 88.00])` | **19.95** | no mode |

That last row is the sharp one. **On continuous data — money, measurements, any
real price list — every value is unique, so `mode` always returns the first
element and always looks like an answer.** Nobody downstream can tell it from a
real mode, and the answer depends on how the data happened to be sorted.

This is the silent-wrong-answer class in a shipped builtin, and it matches a
known wart in SQL:2003, whose `MODE()` is *implementation-defined* on ties. This
project's bar is higher: it refuses rather than guesses.

Three things to decide:

- **A tie should be reported, not resolved.** Either return every tied value (an
  array, which makes `mode` the only aggregate that answers a list) or refuse
  by name. Returning one of them because it came first is the thing to stop.
- **No repeats means no mode** — `unknown`, not the first element.
- **`mode` refuses text today** (`mode expects a numeric array`), which is the
  one aggregate where text is the *common* case: the most frequent category,
  city, or status code. Numeric-only makes it nearly useless for the data people
  actually have.

### "What is most typical?" — and why the midpoint of a tie is not it

The ordinary intuition about `mode` is better than the textbook definition: it
is *the most typical value*, which is a question businesses ask constantly and
almost never ask of `mode`. Two proposals for resolving its ambiguities were
measured rather than reasoned about, and one of them is unsound.

**A tie may not be resolved to the value between the modes.** Measured on
`[10, 10, 10, 100, 100, 100]` — a cheap line and a premium line, which is an
ordinary shape and not a contrived one:

| | |
|---|---|
| the two modes | 10 and 100 |
| the value between them | 55 |
| times 55 occurs in the data | **0** |

So "the most typical sale is 55" is false in the strongest way available: 55 is
the **rarest** price in that list. The midpoint of two modes is a fact about the
modes, not about the data, and nothing bounds how far it sits from any value
that occurred. The rule is sound only while the two modes are *adjacent*
(`[1,1,2,2]` → 1.5 at least lies between two values that both occurred), and a
rule that holds only when the answer barely matters is not a rule. The type is
not the discriminator either — the separation of the modes is — so "integers but
not reals" would not rescue it.

**The "drop down on ambiguity" instinct is binning, and binning is correct.**
Measured on `[19.95, 19.99, 20.05, 21.00, 19.95]`:

| | mode |
|---|---|
| raw | 19.95 — a one-vote margin, i.e. noise |
| rounded to the dollar | **20** |

"About $20" is what a person means by the typical price. That is the statistical
**modal class**: the ambiguity is not resolved by dropping to a lesser answer,
it is resolved by *widening the bucket until the question has one*. It is also
the whole reason `mode` is unused in business — raw business data is continuous,
mode on continuous data is meaningless, and every tool answers anyway.

So the three honest answers, none of which invents a value:

- **Report every tied mode** — an array. `mode([10,10,100,100])` → `[10, 100]`.
  Bimodal *is* the answer; collapsing it hides that there are two products.
- **Report the mode with its frequency** — `{value:, count:, of:}`. Two of five
  is not typicality, and a bare `19.95` cannot say so. This is the field that
  makes the continuous case report its own weakness instead of concealing it.
- **Bin with the granularity declared, and carried in the answer** —
  `mode(prices, {to: 1.00})` → 20, following the rule `reasoning.finding`
  already follows: the choice that shapes an answer travels with it. A guessed
  bin width is the same error as a guessed midpoint.

`mode([1,2,3])` is `unknown`. Nothing repeats, so there is no most typical
value, and that one needs no argument.

`mode` is used **nowhere** in `stdlib/`, `examples/` or `tests/`, and is
documented in a single line, so this is latent rather than burning. Recorded in
`DOGFOOD.md` as well, because a wrong answer in a shipped builtin belongs in the
ledger and not only in a proposal.

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

### SHIPPED 2026-10-06 — items 1 and 2; 3 and 4 refused with the measurement

`tests/run_sort_records.sh`, 40 self-checking assertions, eight perturbations
proven red. `sort` takes an options record; the one-argument form is untouched.

**THE MEASUREMENT CHANGED THE SHAPE, and this section had the wrong count.** It
said `_row_key` was the case for several fields, which is true, and listed four
ways to extend `sort` without measuring which ones the tree needs. Measured, the
hand-rolled sorts over records are:

| where | shape |
|---|---|
| `stdlib/frame.bas:340` | one field ascending, O(n²) insertion sort |
| `stdlib/fundamentals.bas:183` | **two** fields, faked as `r["end"] + "|" + r["start"]` |
| `stdlib/nlq.bas:1507` | score **descending** then id **ascending** |
| `stdlib/stats.bas:3438, 8313` | an **index array** against a parallel column |

**The third is why `descending` takes two shapes.** A plain boolean turns every
key around, which covers frame and fundamentals and *not* nlq — whose own comment
says why its total order matters ("or the answer depends on a driver's row
order"). So `descending` is a boolean (every key) **or a list of field names**
(those keys), which keeps the common case one word and makes the mixed case
declarative and checkable: naming a field `by` does not sort on is refused.

**The fourth is not a field sort at all**, and a comparator function would not
have helped it either — what it orders is an index array against a separate
column of eigenvalues. So the one case this section offered a comparator for
turns out not to be a comparator case.

**Item 3, a declared collation, is deferred with a measurement rather than a
preference.** Of 73 `sort` call sites in this tree, **none** orders a lowered,
trimmed or naturally-collated key. Its spelling is also not free, which this
section assumed it was: a comparison lens is a **grammar** construct driven by a
parser-triggered lexer mode, not a value, so `using: {trimmed; caseless}` cannot
be written as an argument today, and inventing a second vocabulary on a guess
would commit the language to a syntax nothing has asked for.

**The sort is STABLE, and that is a portability requirement.** `qsort` is not
stable and its tie order differs between implementations, so a golden pinning
rows sorted by one field would read differently on glibc and on a BSD libc.
Stability also pays for the one thing `descending` cannot express: sorting by the
minor key and then by the major key gives mixed directions in two declared
passes, and the suite asserts the two routes give the **identical** answer —
which is also what reddens an unstable merge twice over.

**MIGRATED, with goldens byte-identical:** `fundamentals._sort_rows` (its
concatenated key was equivalent only because `end` is a fixed-width ISO date, and
its absent `start` is defaulted to `""` one function up — the residual of the
`fp` defect) and `nlq._by_score`. Both insertion sorts are gone, and the suite
reads the source as well as the goldens, because a library that quietly kept its
own sort would pass the goldens perfectly.

**NOT MIGRATED, AND THIS IS A QUESTION FOR THE USER: `frame.sort_by` ranks an
absence LAST and core `sort` ranks it FIRST.** Demonstrated, not inferred:

```
rows = [ { x: 3 }, { x: unknown }, { x: 1 } ]
frame.sort_by(...)  ->  [{x:1}, {x:3}, {x:unknown}]
sort(rows, {by:"x"}) -> [{x:unknown}, {x:1}, {x:3}]
```

There is no universal convention to appeal to: `ORDER BY x ASC` puts NULLs **last**
in PostgreSQL and Oracle and **first** in MySQL and SQLite. So this is a
convention gBASIC has to choose, which makes it a language-visible decision
rather than this increment's to take. Both answers are **pinned** in the suite, so
neither side can move alone and whoever rules on it has to come to that tier.

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
