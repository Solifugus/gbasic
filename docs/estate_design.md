# A fabricated business estate: requirements

**Status:** Proposal (2026-09-08). **Nothing here is built.** This is a
requirements document, deliberately circulated before code so that the
difficulties it plants are not only the ones its author thought of.

**If you have met real business databases, skip to §6 — that is the ask.**

## 1. What it is for

Two jobs, and they must not be confused.

**Ground truth for discovery.** "Did it find the right relationship?" has no
answer unless the right answer is known, and the sharper question — "did it
invent one where there is none?" — has no answer at all without a region whose
truth is *nothing*. A real customer database cannot supply this: nobody knows
its true lineage, and recovering that is the thing being built.

**A tutorial and cookbook dataset.** The recipes need a business to talk about.

These pull the same way, which is the useful surprise. A real business database
has undeclared foreign keys, a column that changed name crossing a system
boundary, four unrelated `status` columns, and a "temporary" table from 2019
that became load-bearing. **The clean textbook schema is the unrealistic one.**
Being honest about a business estate and being adversarial toward a discovery
tool turn out to be the same act.

**It does not retire the need to meet a real schema.** Only that can say
whether a reader survives what actually exists. Stated here so a passing suite
is never mistaken for the second kind of evidence.

## 2. R1 — one declaration produces both the database and the truth

The load-bearing requirement. A relationship is declared once, saying whether
the database *enforces* it; the DDL emits a constraint only when it does, and
the truth record reports it either way.

A hand-written answer key drifts the first time either side changes, and **a
fixture whose answer key is wrong is worse than no fixture** — it teaches the
tool to be wrong and then certifies it.

## 3. R2 — the truth is a separate value, never a marker in the data

No `is_fk` column, no naming convention only the answer key relies on. This is
`fake.plant`'s rule and it exists for the same reason: a detector tested
against data that labels its own anomalies has not been tested at all.

## 4. R3 — the truth carries a KIND, not a boolean

*Corrected 2026-09-08 on gdash's contribution; the original R3 said only that
the record must name the decoys, and could not score its own hardest case.*

"This pair is a relationship" and "this pair is not" do not cover the case that
matters most in a real estate:

> `order_lines.customer_id` is denormalised from `orders.customer_id`. Every
> value is a real `customers.id`. The relationship is **real**, and the lineage
> **does not run that way** — it runs through `orders`.

A tool reporting `order_lines.customer_id → customers.id` is right about the
values and wrong about the lineage, and an ETL built on that answer computes
different totals the day the copy drifts. Scored `true` it is **rewarded for a
mistake**; scored `decoy` it is **punished for noticing something real**. A
boolean cannot express the difference, so the fixture cannot pose the question.

| kind | meaning |
|---|---|
| `enforced` | declared, and the database checks it |
| `real_undeclared` | true, convention only (R4) |
| `real_indirect` | true, but the lineage runs through another table (R13) |
| `real_non_equality` | true, but not `a = b` — concatenation, substring, range (R19) |
| `real_conditional` | true only for rows where a sibling column has a given value (R15) |
| `coincidence` | the decoy assertion, which is what R3 originally had alone |

Naming the decoys is still required — "this pair is **not** a relationship" is
the assertion a false-positive test makes, and a record listing only what is
true cannot express it.

### 4a. R3b — and an orthogonal flag: is it discoverable at all?

Follows from R21 and R28 below, and is not a kind. Some planted facts are
**deliberately not recoverable from the catalog**: that `tmp_rebate_2019` is
load-bearing lives in a nightly job, and that `customer2` is the live table
lives in the application. The correct behaviour is to **report the question as
unanswerable from the catalog**, not to guess — so the truth record needs
`discoverable: false` beside the kind, or a tool is scored wrong for being
honest.

## 5. R4–R29 — what is deliberately planted

Comprehensiveness is not the goal; **planted difficulty is**. A large clean
schema makes a discovery tool look like it works, which is Recipe 1's finding
in a new costume. Each of these defeats a specific plausible implementation:

| | Planted | What it defeats |
| --- | --- | --- |
| R4 | **Undeclared FK** — `order_lines.product_id`, convention only | reading `SQLForeignKeys` and calling it lineage. The most common real case |
| R5 | **A declared FK beside it** — `orders.customer_id` | the control: "declared beats inferred" is only measurable if both exist |
| R6 | **A column renamed across a boundary** — `ar_invoices.cust_id` → `customers.id` | name-similarity matching; and it is what happens when two vendors' systems meet |
| R7 | **`audit_log.row_id`** — integers overlapping every table's ids, referencing none | **value-overlap inference.** The best decoy available: it looks like a foreign key to everything |
| R8 | **Four unrelated `status` columns**; `id`, `created_at`, `amount` everywhere | name matching. A search over 10,000 columns finds these first |
| R9 | **A null region** — `staging.tmp_import_2019`, no keys, no relationships | **everything.** Lineage reported here is invented, and no other tier can reveal it |
| R10 | **A lookup table reached two ways** — one declared, one by convention | a lookup detector that only reads constraints |
| R11 | **Views, and a view on a view** | the claim that declared column lineage was exhausted before inference began |
| R12 | **The same entity in two databases** | a catalog that assumes one connection is the world. One organisation often runs several at once |

**R13–R29 are gdash's**, contributed 2026-09-08 from having met real business
databases. They are ordered as gdash ordered them: what to plant first if the
estate can only hold a few.

| | Planted | What it defeats |
| --- | --- | --- |
| R13 | **A denormalised copy** — `order_lines.customer_id`, written at order time, always a valid `customers.id` | **the assumption that a true inclusion is a true edge.** Every value-based method scores this a strong FK; the lineage runs through `orders`. Needs `real_indirect` to be scorable at all |
| R14 | **Sentinels in a real FK** — `-1` unknown, `0` not applicable, `999999` system | **inclusion thresholds.** Demanding 100% containment rejects a real relationship; tolerating 5% orphans accepts R7. The point is that **no single threshold gets both right** — plant a known sentinel rate and let the tool declare its threshold |
| R15 | **A polymorphic association** — `attachments.owner_type` + `owner_id` over four tables | **one column, one referent.** The mirror of R7: R7 references nothing while looking universal, this references several *conditionally*. A model keyed on a single column must either miss it or emit four false edges |
| R16 | **A composite key with a promiscuous member** — true key `(company_id, order_no)`; `order_no` alone overlaps every company's | **single-column candidate generation**, which everyone builds first for the O(n²) reason. Yields a confident, wrong, single-column edge |
| R17 | **A type mismatch across a real relationship** — zero-padded `varchar(10)` `'00042'` against `integer` 42 | **the type-compatibility prefilter.** Every implementation prunes by type before comparing values because the cross-product is unaffordable — so it is load-bearing for performance and rarely revisited once written |
| R18 | **Soft deletes** — parents carry `deleted_at`, children still reference them, the `active` view omits the parents | **profiling a view instead of a table**, and **orphan-rate-as-disproof.** Inclusion is 100% against the base table and broken against the interface everyone actually uses |
| R19 | **A relationship through a concatenation** — `gl_entry.reference = 'INV-' \|\| invoice.id`; a fixed-width `cost_centre` whose first three characters are the branch code | **equality itself.** No value-overlap method finds either, and both are the normal shape of a general ledger — where "where did this number come from" is asked most |
| R20 | **A silent truncation** — source key `varchar(50)` into a `varchar(20)` column; two distinct customers collapse into one | **high overlap as strong evidence.** The truncation makes overlap *better* than reality. Include the two source rows, so the tool can be asked the question that matters: not "is this an FK" but **"is this key still unique"** |
| R21 | **A "temporary" table that became load-bearing** — `staging.tmp_rebate_2019`, no constraints, read by a nightly job, breaks month-end if dropped | **degree-in-the-graph as importance.** R9 tests inventing lineage where there is none; this tests the opposite error — correctly finding nothing and therefore rating it removable. Needs R3b: the fact is not in the schema at all |
| R22 | **A column that changed meaning at a date** — numeric `status`, one vocabulary before a 2021 migration and another after, disambiguated only by `created_at` | **treating a column as homogeneous.** Every profiler summarises a column as one distribution; this one is two. The purest "convention nobody wrote down" |
| R23 | **Two independent sequences that align** — `invoice.id` and `shipment.id` both from 1, overlapping almost perfectly | **value-overlap inference at high cardinality.** R7 is universal and therefore suspicious; this looks like a clean, high-cardinality, near-perfect FK — the profile every heuristic trusts most |
| R24 | **Case, padding and collation** — `'ACME '` and `'ACME'` are one company; `'ACME'` and `'acme'` are two | **both directions at once.** Exact matching misses the first, normalised matching invents the second, and no setting gets both right — so the tool must **report the ambiguity rather than resolve it** |
| R25 | **An SCD2 dimension** — `dim_customer` with several rows per `customer_id` and `valid_from`/`valid_to`; facts join on `customer_sk` and also carry a degenerate `customer_id` | **one row per key.** Overlap on `customer_id` reports a many-to-many that is arithmetically true and analytically wrong; the real edge is on the surrogate |
| R26 | **The same entity split by date** — `orders` and `orders_archive`, ids **disjoint by construction** | **overlap-based unification.** One entity with zero value overlap: the evidence points exactly the wrong way, and the only signal is structural (identical column sets) |
| R27 | **A column in eighty tables, null in seventy-nine** — added by a framework migration | **"common column" reports.** Its presence means nothing and it dominates every one of them |
| R28 | **Table names encoding a history nobody remembers** — `customer`, `customer2`, `customer_new`, `customer_v2_final`; the live one is `customer2` | **the schema as the source of truth about what is live.** The only evidence is which one the application writes to, which is not in the database at all. Needs R3b |
| R29 | **A schema whose only accurate documentation is a view** — the tables are a mess, the view is the contract, and a column's business meaning exists only in the expression producing it | extends R11 **with a purpose**: not "can the tool follow a view" but **"does it know the view is the interface"** |

## 5a. The coverage argument — read the table the other way

gdash's, and it is worth more than another example. Several traps defeat the
**same** mitigation, and that is structure rather than redundancy:

| mitigation an implementation will reach for | defeated by |
|---|---|
| read declared constraints | R4, R10 |
| name similarity | R6, R8 |
| type prefilter | **R17 only** |
| value inclusion / overlap | R7, R13, R23, R26 |
| inclusion threshold tuning | R14, R18 |
| single-column candidates | R15, R16 |
| equality itself | R19, R24 |
| graph degree as importance | R9, R21 |
| column-level profiling | R22, R27 |
| one row per key | R25 |
| the catalog can answer it at all | R21, R28 |

§5 says **a trap that defeats no mitigation is decoration.** The inverse is the
sharper test: **a mitigation defeated by no trap is an untested assumption**,
and the list should be read that way *before* anything is built.

On this list the thinnest column is the **type prefilter, defeated only by
R17** — the one everybody writes for performance reasons and nobody revisits.
That is a finding about the design, not about the fixture: it says where the
next trap is worth more than a better implementation.

## 6. What was asked for — answered once, still open

**gdash answered this on 2026-09-08** (their `docs/estate_contribution.md`),
contributing R13–R29, the R3 correction that this document's load-bearing rule
could not previously express, and §5a's coverage argument. That exchange is the
evidence for circulating requirements before code: the correction was to the
part I was most confident about.

The list stays open. What is still worth more than another invented example:

- a mitigation from §5a's table that **no trap defeats** — an untested
  assumption, which is now the sharper thing to look for than another trap;
- the **type prefilter** specifically, defeated only by R17;
- anything from a real estate that does not fit any row above, which is the
  most interesting case of all.

### The original ask, kept because it is what produced the above

**The traps above are the ones its author thought of.** A tool tested against
difficulties chosen by the same mind that built it has the blind spot this
document opens with, one level up — so the list is explicitly incomplete and
contributions are worth more than anything invented here.

Useful shapes, if you have met them:

- **A convention nobody wrote down** that a newcomer could not guess — a
  prefix, a sentinel value, a column that means something different per row.
- **A "temporary" table that became load-bearing**, and how you found out.
- **Something an ETL did that surprised you** — a silent truncation, a join
  that dropped rows, a column that changed meaning between two hops.
- **A relationship that is real but unrepresentable as an FK** — across
  databases, through a code lookup, keyed on a concatenation.
- **A thing that looks like a relationship and is not**, which is the highest
  value item on this list because false positives are the failure mode.
- **What made a schema hard to understand** the first time you met it.

Add them to §5 with the "what it defeats" column filled in, or raise them
however is convenient — the column matters more than the formatting, because a
planted difficulty nobody can say the purpose of is decoration.

## 7. Proposed shape — not settled

```basic
spec  = estate.spec()                       ' databases, tables, relationships
ddl   = estate.ddl(spec, "sqlite")          ' create statements, per dialect
rows  = estate.rows(spec, 20260908)         ' data from `fake`, pure in (seed, index)
truth = estate.truth(spec)                  ' real relationships, and the decoys BY NAME
n     = estate.materialise(conn, spec, seed, "sqlite")
```

**DDL is dialect-specific and ODBC does not normalise it.** That is not a
retreat from reading the catalog through the driver manager: reading a schema
portably and *writing* one are different problems, and only the first is what
discovery does. The dialect map should stay small — `integer`, `text(n)`,
`money`, `date`, `timestamp`, `boolean`.

**Data should come from `fake`**, staying a pure function of `(seed, index)` so
a committed fixture cannot silently rot. `estate` would load `fake`; `fake`
must not load `estate` and must stay dependency-free.

## 8. Known limits, stated before anything is built

- **It is generated, so it is regular.** Real schemas have inconsistent casing,
  dead columns nobody can explain, permissions hiding half the catalog, and
  encodings that disagree. None of that would be here.
- **It says nothing about scale.** Twenty tables is not five hundred, and
  search width is exactly what makes inference hard (Recipe 4 measured the
  smallest detectable change rising from 18% to 147% of a typical cell as a
  search widened fiftyfold).
- **Realism and adversarial content agree here, but not forever.** If the
  tutorial ever wants a tidier story than the fixture wants, that is the point
  to split them rather than to compromise both.
