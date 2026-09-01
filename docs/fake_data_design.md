# Fake data design

**Status:** Partial — Layer 1 (values) and the first Layer 2 builders
(`customers`, `invoices`) are shipped in `stdlib/fake.bas` with
`tests/run_fake.sh`. Planted anomalies (§6), the `fake.table` spec form (§4)
and the year-long worked business (§11) are **not** built. Sections marked
deferred remain proposals.

**Scope:** `stdlib/fake.bas` — fabricated but realistic data for tests,
demonstrations and development. Names and addresses are the easy half; the
half that finds bugs is **distributions**.

Motivated by the finance platform work: `stats` needs samples, `finance` needs
cash flows, `accounting` needs a month of transactions, `forensics` needs a
population with a known planted anomaly, and Phase 3 lending needs a loan
portfolio with realistic terms and payment behaviour. Every one of those is
currently tested against fixtures typed by hand.

---

## 1. Why this is worth building

**It is the missing input for the libraries that already exist.** Phase 2's
validation item was literally "a worked business", and it was satisfied by nine
transactions written out by hand. That is enough to prove the kernel adds up;
it is not enough to exercise `grid`'s messy-sheet detection, `forensics`'
Benford test, or any loan analytic whose whole subject is a *distribution* —
vintage curves, roll rates, delinquency aging.

**And hand-written fixtures are systematically unrepresentative.** They are
small, tidy, uniform, and written by someone who knows the answer. The money
cookbook found a silent `money * scalar` defect only because a realistic
amortization produced a scalar with nineteen fractional places; every unit
fixture had used 2, 3, 1.08 and 0.5. A generator produces the shapes nobody
thinks to type.

---

## 2. What the language decides for us

Three facts were measured before this design, and each one settles a question
that would otherwise be argued.

**gBASIC's RNG is already the right one.** `seed`/`random`/`random_int` are
backed by SplitMix64 seeding xoshiro256 in `src/eval.c` — not libc. So the
sequence is **identical on every platform and every build**, and a fixture
generated here regenerates byte-for-byte in CI. No PRNG needs implementing in
gBASIC, which the bitwise builtins would otherwise have made tempting.

**A stream object cannot advance itself.** Records are values, so

```basic
function draw(s)
    s.n = s.n + 1        ' mutates a LOCAL copy
    return random()
end function
```

leaves the caller's counter untouched — measured, and every draw returned the
same number. A stateful `fake.stream(seed)` is therefore not expressible
without making every call site rebind the stream, which is noise on every line.

**So the API is pure functions of `(seed, index)`.** Each value is derived by
reseeding from the pair:

```basic
function at(base, i)
    seed(base * 1000003 + i)
    return random()
end function
```

Verified: six indices give six distinct values, asking for index 4 first gives
the same value as reaching it in order, a different seed gives a different
series, and two runs agree exactly. **Order independence is the property that
matters** — adding a field to the customer generator must not shift every
invoice generated afterwards, or committed fixtures silently rot.

---

## 3. Two layers, because "realistic" depends on what for

**"Fake" is the simple half. "Realistic" is relative to a purpose**, and
conflating the two is what limits a library to one domain.

So the design splits:

**Layer 1 — values.** Domain-neutral draws: a name, a company, an address, an
email, a date, an identifier. This is roughly what Python's Faker provides, and
it is deliberately opinion-free about what the data is *for*. It should stay
general even though this library's first domain is business and finance.

**Layer 2 — datasets.** A *population* with constraints, consistency and
distributions: customers, the invoices that reference them, the payments that
settle those invoices, and a ledger that balances. This is where "realistic for
what" lives, and it is the half a value generator cannot reach.

**The rule that keeps it from becoming a business-only library: Layer 1 must
not know Layer 2 exists.** Adding healthcare, logistics or telemetry later is a
Layer-2 addition and touches nothing underneath.

### What a value generator cannot do

Measured against Faker 39.0.0 rather than recalled. It generates independent
values, so `name()` and `email()` are unrelated draws — seeded at 0 it produces
`Norma Fisher` / `tammy76@example.com` and `Heather Snow` /
`williamcampbell@example.org`. It has no invoice, ledger, customer or
relational concept at all (searched: none). And its numeric providers are
**uniform**: 20,000 draws of `pyint(1, 100000)` put 11.0% of values in each
leading digit where Benford's law expects 30.1% for 1 — so Faker data
**cannot** exercise a Benford detector, which is the single clearest example of
why "realistic" has to mean distributions and not just plausible strings.

Its seeding is solid and reproducible, and it already uses RFC 2606 reserved
domains for email, which §8 adopts.

Four kinds of consistency follow from the value-at-a-time gap, and all four are
why business data needs Layer 2:

| Consistency | Example | Without it |
|---|---|---|
| **Intra-record** | the email derives from the name; the postcode belongs to the city | data that looks wrong to anyone who reads a row |
| **Referential** | every invoice names a customer that exists | cannot drive `accounting` or a foreign key at all |
| **Temporal** | a payment is dated after its invoice; nothing predates the company | any aging, cohort or vintage analysis is meaningless |
| **Arithmetic** | line items sum to the invoice total; payments never exceed it | `accounting` refuses it, correctly, and the fixture is unusable |

The fourth is the one that decides whether generated data is usable **as
input** rather than only as filler. A ledger whose entries do not balance is
not fake business data; it is noise shaped like business data.

## 4. Shape

```basic
' Every generator takes the seed and the row index. Nothing is stateful.
person = fake.person(seed, i)      ' { name, given, family, email, phone }
co     = fake.company(seed, i)     ' { name, domain }
addr   = fake.address(seed, i)     ' { line1, city, region, postcode }
```

Composite generators take a **spec** and produce an array of records — the
shape `frame`, `dbframe` and `accounting` already consume:

```basic
customers = fake.table(seed, 500, {
    id:      { kind: "sequence", prefix: "C" },
    name:    { kind: "company" },
    country: { kind: "pick", from: ["US", "CA", "GB"], weights: [70, 20, 10] },
    since:   { kind: "date", from: "2019-01-01", to: "2026-01-01" }
})
```

---

## 5. Distributions are the point

A uniform amount column is useless for testing anything that cares about
shape. The generator must offer, and default to, distributions that look like
business data:

| Need | Why a uniform draw fails |
|---|---|
| **Lognormal amounts** | Real invoice values are lognormal-ish, which is *why* they satisfy Benford. Uniform values do not, so `forensics.benford` cannot be tested against them at all. |
| **Pareto concentration** | One customer being 40% of revenue is the case concentration analysis exists to find. Uniform data has no tail. |
| **Weekday clustering** | Business dates avoid weekends. A uniform date range puts 2/7 of activity on days the business was shut, which breaks any business-day calculation downstream. |
| **Seasonality and trend** | Forecast backtesting needs a series with something to forecast. |
| **Round-number bias** | Humans enter 500 and 1000 far more than 497. Several fraud tests key on its *absence*. |

**Refusing to guess the scale.** Amounts need a currency and a magnitude, and
neither is inferable — the same rule `money` and `finance` already follow.

---

## 6. Planted anomalies

The feature that a hand-written fixture cannot provide: a clean population with
a **known** defect inserted at a **known** place.

```basic
ledger = fake.transactions(seed, 5000, spec)
rigged = fake.plant(ledger, { anomaly: "round_dollar", count: 12, at: seed })
```

**Correction, 2026-08-31:** an earlier version of this section said this would
improve "`forensics`' Benford and accrual tests". `stdlib/forensics.bas` has no
Benford test — it is *financial-statement* forensics (accruals, M-score,
Beneish, Piotroski, Altman, dilution), not transaction-level anomaly detection,
and the only occurrence of the word "benford" in the tree was in this document.
The claim was about a detector that does not exist.

What planting is actually for is therefore **future** rather than current, and
worth stating as such. When a transaction-level detector exists, the assertion
it enables is *found the twelve rows we planted and did not flag the other
4,988* — a statement about the **detector** rather than about a fixture chosen
to produce the expected answer. The nearer consumer is Phase 3's credit
analytics (`docs/lending_design.md` §8), where vintage curves and roll rates
need a portfolio with known-bad accounts rather than six hand-written loans.

Planting is independently testable in the meantime, and that is the bar it has
to meet now: plant *n* anomalies into a clean population and assert that
exactly those *n* rows carry it, that they are the rows named, and that every
other row is untouched.

**Shipped 2026-08-31**, with exactly those five kinds: `round_dollar`,
`just_under`, `weekend`, `duplicate`, `sequence_gap`.

`plant` returns `{ rows, planted }` — the population, and a report record per
anomaly carrying `anomaly`, `id`, `index`, `was`, `now` and `source`. The spec
takes `anomaly`, `count` and `at`, and optionally `amount_field` /
`date_field` / `id_field`, a `threshold` for `just_under`, and `avoid` so a
second plant can be composed onto a first without overwriting it.

**The report is separate from the rows, and that is the design.** No planted
row carries a field its neighbours do not — a marker would be a back door a
detector could read, and a detector tested against data that labels its own
anomalies has not been tested at all. The rule has one visible consequence:
because a `duplicate` needs an id that continues the population's own sequence
rather than announcing itself, a population whose ids do not end in digits is
**refused** rather than given an id that stands out. Refusing is the right
answer here; inventing a distinguishable id would quietly make the fixture
worthless.

The suite is `tests/run_fake_plant.sh`, self-checking rather than golden for
the usual reason — every planting defect leaves a population that still looks
like a population. Its tiers were proven red one at a time against deliberately
broken copies of the library: a report naming the wrong row, a clean row
altered in passing, a marker field, a duplicate id that stands out, an unsorted
sample, and a weekend anomaly that misses the weekend. Each was caught by the
tier written for it and by no other.

---

## 7. Consistency, concretely

§3 names four kinds; this is how they are met.

**Referential** — a child row picks its parent **by index**, and the
`(seed, index)` design makes the parent regenerable from the same seed without
being stored. So an invoice can reference customer 317 and both agree, in any
order, without either being kept in memory.

**Temporal** — a generated entity carries its own window. An invoice cannot
predate its customer's `since` date, and a payment is drawn from the window
*after* its invoice. Dates are business-day clustered by default (§5).

**Arithmetic** — line items are generated first and the total is their sum,
never the reverse; a payment is drawn as a fraction of the outstanding balance,
so it cannot exceed it. Money is exact, so these hold to the cent rather than
approximately.

**Intra-record** — an email is derived from the name and company that were
already drawn for that row, not drawn independently.

**The test that all four are real** is that the output drives `accounting`
without a single refusal: an unbalanced entry, a phantom account or a
cross-currency line would each be rejected at the point it was posted. A
generator whose ledger posts cleanly has demonstrated its consistency rather
than claimed it.

## 8. Uniqueness belongs to Layer 2

**A value may repeat; a population may not.** Two real people are called Ada
Novak, so `fake.person` returning the same name twice is honest. A customer
list with two `Basalt Partners` is not realistic — it is broken, and anything
keyed on email silently merges rows.

Measured before this was fixed: **2,000 customers gave 555 distinct emails and
359 distinct company names**, because 24 heads × 15 tails saturates at 360.
Widening the pools only moves the number; the birthday problem beats any pool
at population scale.

So the **dataset builder** guarantees uniqueness, which it can do because it
owns the whole list — a `(seed, index)` value generator cannot see its own
siblings. It appends the smallest numeral that makes a value new, which is what
real directories do (`j.smith2@` exists because `j.smith@` was taken), so the
result stays plausible rather than becoming a serial number. Below saturation
no suffix appears at all.

This is also where Faker's `unique` proxy sits, and the comparison is worth
being straight about: Faker's is general and applies to any provider; ours is a
property of the dataset builders only. That is a deliberate consequence of the
layering, not an oversight — but it does mean a caller assembling their own
population out of Layer 1 values must handle uniqueness themselves.

## 9. It must be obviously fake

**Names, addresses, companies and contact details must not collide with real
people or organisations**, and the library should make that structural rather
than hoped-for:

- email domains from `example.com` / `example.org` (RFC 2606, reserved forever);
- phone numbers in the `555-01xx` range (reserved for fiction);
- street addresses assembled from invented street names, never a real gazetteer;
- company names assembled from parts, with a check that the result is not a
  well-known mark.

This is not merely tidy. Fake data ends up in bug reports, screenshots, demos
and test databases that get shared, and a plausible-looking record naming a
real person is a privacy problem the moment it leaves the machine.

---

## 10. Validation

- **Reproducibility**: the same seed produces byte-identical output, asserted
  across two runs in the same suite — the property everything else rests on.
- **Order independence**: generating rows 0..99 and generating row 47 alone
  must give the same row 47.
- **Distribution shape asserted statistically, not by eye**: a lognormal
  amount column must pass `forensics.benford`; a weekday-clustered date column
  must put under 2% of rows on a weekend. These are `stats` calls, so the
  library is checked with the library it exists to feed.
- **Planted anomalies are found**: the detector flags what was planted, and the
  count of false positives is asserted — a detector that flags everything would
  otherwise pass.
- **No real-world collisions**: generated emails resolve to reserved domains,
  phone numbers to the reserved range.
- **Scale**: 100,000 rows in a bounded time, since the point is populations
  bigger than anyone types.

---

## 11. Deferred, with reasons

- **Locales beyond en-US** — real localisation needs native review per locale,
  and a machine-translated name list is worse than none. This is the one place
  Faker is clearly ahead (~70 locales) and the gap is honest: those lists were
  contributed by speakers, and there is no shortcut.
- **Domains beyond business and finance** — healthcare, logistics, telemetry.
  Not excluded, just not first: they are Layer-2 additions by construction
  (§3), so adding one later touches nothing underneath. The value layer is
  built general precisely so this stays true.
- **A schema-inference mode** ("generate data like this table") — attractive
  and a different problem: it needs profiling, and profiling real data is how
  real values leak into fake ones.
- **Streaming generation** — everything here builds arrays; a generator for
  data larger than memory can come when something needs it.
- **Statistical fidelity to a supplied dataset** — same objection as schema
  inference, more so.

---

## 12. First consumer

Phase 2's worked consultancy, scaled up: the same chart of accounts, a year of
generated transactions instead of nine typed ones, and the accounting equation
asserted over the result. That exercises distributions, referential integrity
and volume against a library whose correct answers are already known — which
is the cheapest place to find out the generator is wrong.
