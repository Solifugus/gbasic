# Fake data design

**Status:** Proposal
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

## 3. Shape

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

## 4. Distributions are the point

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

## 5. Planted anomalies

The feature that a hand-written fixture cannot provide: a clean population with
a **known** defect inserted at a **known** place.

```basic
ledger = fake.transactions(seed, 5000, spec)
rigged = fake.plant(ledger, { anomaly: "round_dollar", count: 12, at: seed })
```

This is what makes `forensics` testable. Today its Benford and accrual tests
assert against data chosen to produce the expected answer; with a planted
anomaly the assertion becomes *the detector found the twelve rows we planted
and did not flag the other 4,988*, which is a statement about the detector
rather than about the fixture.

Anomaly kinds worth having first: round-dollar clustering, duplicate payments,
just-under-threshold approvals, sequence gaps, and a weekend-dated entry.

---

## 6. Referential integrity

Generated data must be internally consistent or it cannot drive `accounting`
or `dbframe`: an invoice must reference a customer that exists, and a payment
must reference an invoice whose amount it does not exceed.

The `(seed, index)` design makes this cheap — a child row picks its parent by
index, and the parent is regenerable from the same seed without being stored.

---

## 7. It must be obviously fake

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

## 8. Validation

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

## 9. Deferred, with reasons

- **Locales beyond en-US** — real localisation needs native review per locale,
  and a machine-translated name list is worse than none.
- **A schema-inference mode** ("generate data like this table") — attractive
  and a different problem: it needs profiling, and profiling real data is how
  real values leak into fake ones.
- **Streaming generation** — everything here builds arrays; a generator for
  data larger than memory can come when something needs it.
- **Statistical fidelity to a supplied dataset** — same objection as schema
  inference, more so.

---

## 10. First consumer

Phase 2's worked consultancy, scaled up: the same chart of accounts, a year of
generated transactions instead of nine typed ones, and the accounting equation
asserted over the result. That exercises distributions, referential integrity
and volume against a library whose correct answers are already known — which
is the cheapest place to find out the generator is wrong.
