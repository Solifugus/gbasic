# gBASIC Financial Adapter Framework

**Status:** Proposal\
**Suggested path:** `docs/financial_adapters_design.md`\
**Working library name:** `finio`\
**Scope:** Import, interpretation, normalization, provenance, validation
primitives, and export for financial-services data formats.

------------------------------------------------------------------------

## 1. Purpose

The financial adapter framework provides a stable gBASIC interface to
financial-services data formats whose representations change across
vendors, standards bodies, jurisdictions, and time.

It is not merely a collection of parsers.

It consists of:

1.  **A runtime framework** for recognizing, parsing, interpreting,
    translating, validating, and emitting financial data.
2.  **A registry** describing known formats, versions, authorities,
    effective dates, documentation, licensing constraints, and
    implementation status.
3.  **A maintenance process** for discovering new formats and revisions,
    acquiring specifications legally, implementing them, testing them,
    and preserving historical compatibility.

The framework must support both current financial data and archives
created under historical versions.

The registry is expected never to be complete. **Completeness is a
direction, not a release criterion.**

------------------------------------------------------------------------

## 2. Fundamental Axioms

### Axiom 1: Preserve the source

No source information is silently discarded.

Anything that cannot presently be normalized remains available in its
original form.

### Axiom 2: Every interpretation has provenance

Every interpreted value must be traceable to the portion of the source
from which it was derived.

Likewise, every adapter rule must be traceable to the specification,
documentation, or other evidence supporting that rule.

### Axiom 3: Meaning and representation are separate

The framework models financial meaning independently from whether the
source is fixed-width, XML, JSON, CSV, spreadsheet, EDI, binary, or
another representation.

### Axiom 4: History is immutable

Once support exists for a historical format revision, later revisions do
not change its interpretation.

Historical adapters remain available indefinitely unless they are
demonstrated to be incorrect.

### Axiom 5: Evolution is data, not API

Changes in standards, layouts, code sets, and vendor formats normally
create new adapter revisions or metadata rather than new public APIs.

The framework API should remain stable.

### Axiom 6: Never silently guess

Adapter and version detection must be supported by evidence.

When multiple interpretations remain plausible, the framework reports
ambiguity rather than choosing invisibly.

### Axiom 7: Unknown is different from invalid

An unrecognized field, extension, code, or revision is preserved as
unknown unless the governing specification establishes that it is
invalid.

### Axiom 8: Loss must be explicit

Any transformation that cannot preserve all source semantics must report
the loss.

Lossy export or translation may be permitted, but never silently.

### Axiom 9: Parsing and policy are separate

Adapters describe what a source says and what its governing format
requires.

Application-specific judgments such as operational severity, workflow
routing, exception handling, or institutional policy belong to
consumers.

### Axiom 10: Recognition is a capability

The framework must be able to examine unknown historical files and
determine candidate formats and revisions where sufficient evidence
exists.

### Axiom 11: Adapters themselves have provenance

Every implemented rule must record what authority or evidence justified
it, what revision it applies to, and when that evidence was obtained.

### Axiom 12: Legal availability matters

Discovery of a format does not imply that its specification may be
redistributed, quoted, bundled, or obtained automatically.

Implementation metadata must distinguish those rights explicitly.

------------------------------------------------------------------------

## 3. Conceptual Model

The import path is:

``` text
Source
   |
   v
Recognition
   |
   v
Source Representation
   |
   v
Interpretation / Mapping
   |
   v
Semantic Representation
   |
   v
Consumer
```

Translation and export reverse the direction through a target mapping:

``` text
Semantic Representation
   |
   v
Target Mapping
   |
   v
Target Representation
   |
   v
Serialized Output
```

The original source remains attached throughout.

Conceptually, provenance is graph-shaped:

``` text
source element ----> interpreted concept
       |                    ^
       +----> rule ----------+
                |
                +----> specification/evidence
```

This does not require a graph database. It means that the relationships
are naturally many-to-many and should not be artificially forced into
one-to-one mappings.

------------------------------------------------------------------------

## 4. Source Representation and Provenance

A universal byte-offset model is too representation-specific. The
framework should instead provide a generic source-location abstraction.

Conceptually:

``` text
SourceValue
    raw
    interpreted
    location
    format
    revision
```

Locations vary by representation:

``` text
FixedWidthLocation
    record
    byte_offset
    byte_length

DelimitedLocation
    row
    column

SpreadsheetLocation
    sheet
    row
    column
    cell

XmlLocation
    path
    occurrence
    optional character/byte range

JsonLocation
    json_pointer
```

This permits a fixed-width format to retain exact byte provenance while
a spreadsheet adapter can identify a source such as `Sheet1!G27`.

Provenance should be retained through normalization and transformation
rather than treated as optional debugging information.

------------------------------------------------------------------------

## 5. Semantic Representation

Normalized financial meaning must retain its ancestry.

Conceptually:

``` text
SemanticValue
    concept
    value
    sources[]
    transformations[]
```

`sources[]` is deliberately plural.

For example, a format revision might change:

``` text
Old revision:
    FullName -> Party.Name

New revision:
    FirstName + MiddleName + LastName -> Party.Name
```

The reverse situation is also possible. One old source value may contain
information that a later semantic model separates into several concepts.

The framework therefore treats provenance and mappings as many-to-many
relationships.

The semantic model should contain stable financial concepts such as
parties, accounts, amounts, balances, transactions, institutions, dates,
identifiers, and other concepts as they become justified by actual
formats.

The semantic model must not attempt to anticipate every future financial
concept. It should be extensible without invalidating historical source
data.

------------------------------------------------------------------------

## 6. Adapter Identity and Versioning

Years should not be embedded in API function names.

An adapter identity should contain information such as:

``` text
family
revision
effective_from
effective_until
authority
```

A revision need not be a year. Standards use dates, releases, message
versions, service packs, vendor revision strings, and other schemes.

A useful internal identity may resemble:

``` text
AdapterId
    namespace
    family
    revision
```

Examples:

``` text
aba.nacha / 2026
iso.20022.camt.053 / 001.10
vendor.example.participations / 4.2
```

A new revision creates a new interpretation. It does not mutate the
historical meaning of an old revision.

Code sets, field semantics, validation rules, and other dependencies may
themselves be versioned even when the physical layout does not change.

------------------------------------------------------------------------

## 7. Public API Philosophy

The ordinary API should remain small and stable.

For example:

``` basic
load finio

doc = finio.read("payments.ach")
```

Resolution should proceed using evidence in roughly this order:

``` text
explicit adapter/revision
        |
        v
as-of-date hint
        |
        v
deterministic recognition
        |
        v
candidate set
        |
        v
ambiguity/error if unresolved
```

Optional information can narrow resolution. gBASIC has **default values for
positional parameters**, not named arguments at a call site, so optional
information travels as an options record — the shape `discovery.scan`,
`odbc.connect` and `webserver.listen` already use:

``` basic
asof {date}= "2023-06-01"

doc = finio.read("payments.ach", { format: "nacha", asof: asof })
```

An options record also gives the framework somewhere to **refuse an
unrecognised option by name**, which a positional list cannot: a misspelled
`reivsion` that is silently ignored leaves the caller believing they pinned an
adapter they did not.

A caller that requires reproducibility can fully pin the adapter:

``` basic
doc = finio.read("payments.ach", { adapter: "aba.nacha", revision: "2023" })
```

Recognition should also be available independently:

``` basic
r = finio.identify("mystery.dat")
```

A recognition result should report candidate adapter, revision,
evidence, and reasons.

Where deterministic evidence exists, avoid implying statistical
certainty. A match classification such as the following may be
preferable:

``` text
exact
strong
possible
ambiguous
unknown
```

No plausible interpretation should be selected silently when the
evidence is ambiguous.

**Ambiguity should be actionable, not merely honest.** Reporting that a file is
either NACHA 2021 or 2022 stops a caller who has work to do. Where the
candidate set is small, the framework should be able to interpret the source
under **each** candidate and report where the interpretations actually differ:

``` basic
a = finio.compare("mystery.dat", ["aba.nacha/2021", "aba.nacha/2022"])
```

``` text
candidates differ in 2 concepts:
    Addenda.PaymentRelatedInformation   present under 2022, absent under 2021
    Batch.ServiceClassCode "225"        "debits only" -> "debits and credits"

they agree on: Party.Name, Amount, EffectiveDate, Account.Number, ...
```

In practice the candidates very often agree on everything the caller needs, and
then the ambiguity is resolved **for that use** without being resolved in
general. That is the honest answer in an imperfect world: a caller rarely needs
to know which revision a file is, they need to know **whether it matters**.

Where the candidates do differ on a concept the caller depends on, that is
precisely the case that must not be guessed, and the report names it.

------------------------------------------------------------------------

## 8. Archive Workflow

Historical archives are a first-class use case.

For example:

``` basic
report = finio.scan("archive/")
```

A report might produce:

``` text
427 files examined

NACHA 2020       82
NACHA 2021      104
NACHA 2022      131
NACHA 2023       96
unknown          11
ambiguous         3
```

Import can then operate across the archive:

``` basic
docs = finio.readall("archive/")
```

Every resulting document records which adapter and revision interpreted
it.

The people possessing an archive should not be required to know when
format revisions occurred. The framework should discover revisions where
the files contain sufficient evidence.

An unrecognized historical variant is a discovery event, not an
invitation to force the data through the nearest known parser.

------------------------------------------------------------------------

## 9. Format Registry

The registry may ultimately be as important as any individual adapter.

Each format entry should include information such as:

``` text
id
name
family
domain
authority
description

representation
transport

known_revisions[]
effective_dates[]

specification_sources[]
source_type
source_url_or_reference
date_retrieved

spec_public
spec_acquisition_method
implementation_allowed
spec_redistribution_allowed
sample_redistribution_allowed

implementation_status
recognition_status
read_status
write_status
validation_status

test_vectors[]
known_variants[]
known_extensions[]

last_reviewed
next_review_due
```

The following states must remain distinct:

``` text
discovered
spec_obtained
researched
implemented
verified
```

Finding that a format exists is not equivalent to possessing enough
legitimate information to implement it.

The registry should include formats even when implementation is
presently impossible. Such entries become a research and acquisition
queue.

### Observations: what has been seen and not explained

The registry above records what is **known**. A maintenance process also needs a
record of what has been **encountered and not understood**, because that is the
evidence a format has moved — and it arrives from production long before it
arrives from a standards body.

``` text
ObservationLog
    adapter / revision under which it was read
    kind        unknown_code | unknown_field | unexpected_length |
                unparsed_region | contradictory_context | ...
    detail      the token, field or region as found
    first_seen  date
    last_seen   date
    occurrences count
    files       a bounded sample of where
```

This closes the loop that §14's research alone cannot: discovery becomes
**bottom-up as well as top-down**. Three hundred occurrences of an unknown
service class code across last quarter's files is a stronger signal that a
revision happened than any watch list, and it names the field to go and read
about.

The log is also what makes Axiom 7 pay for itself. Preserving an unknown value
is only half the benefit; **counting it** is what turns it into work.

Observations must carry the same care as fixtures: they record tokens and
locations, never account numbers, party names, amounts, or any other content of
a customer record.

------------------------------------------------------------------------

## 10. Acquisition Classes

Formats and specifications should be classified according to how their
implementation knowledge can legitimately be obtained.

### OPEN

A public authoritative specification is available.

### CONTROLLED

The specification exists but requires purchase, membership,
registration, agreement, or another controlled acquisition process.

### PUBLIC_VENDOR

A vendor publishes sufficient documentation to implement the format.

### DE_FACTO

No single controlling specification exists, but sufficient lawful public
evidence exists to characterize the format.

### HUMAN_REQUIRED

The format is known, but legitimate acquisition requires human action.

### INSUFFICIENT

The format has been discovered, but insufficient lawful evidence
currently exists for implementation.

This classification gives automated research a clear stopping point.

An automated process should be able to report:

``` text
Format discovered.
Implementation blocked.
Human acquisition required.
```

It must not invent missing details merely to complete an adapter.

------------------------------------------------------------------------

## 11. Human-in-the-Loop Acquisition Process

The maintenance process is:

``` text
DISCOVER
   |
   v
CLASSIFY
   |
   v
ACQUIRE
   |
   v
DOCUMENT PROVENANCE
   |
   v
IMPLEMENT
   |
   v
VERIFY
   |
   v
RELEASE
   |
   v
MONITOR
   |
   +----------> repeat
```

AI and automation should perform as much discovery, classification,
acquisition, comparison, and change detection as legally and technically
possible.

Human acquisition becomes necessary when a source:

-   requires standards-body membership;
-   requires manual purchase;
-   requires an authenticated vendor portal;
-   requires acceptance of a license or agreement;
-   is unavailable electronically;
-   or otherwise cannot appropriately be obtained automatically.

A human may legitimately obtain the necessary material, record its
provenance and applicable rights, and allow the process to resume.

Confidential employer or customer data must not be used merely because
it happens to expose a proprietary format.

------------------------------------------------------------------------

## 12. Adapter Provenance

Adapter provenance is separate from data provenance.

Data provenance answers:

> Where did this interpreted value come from?

Adapter provenance answers:

> Why does the adapter believe this source element has this meaning?

An adapter rule should be traceable through a chain resembling:

``` text
adapter behavior
    -> rule
    -> specification/evidence
    -> specification revision
    -> authority
    -> acquisition/retrieval date
```

This is particularly important when a format changes semantics without
visibly changing its physical shape.

For example, a code value may acquire a different meaning in a later
revision while occupying the same column or XML element.

The adapter must version the semantics, not merely the layout.

------------------------------------------------------------------------

## 13. Maintenance

This is not a "finish it and forget it" library.

Each registry entry should contain maintenance information such as:

``` text
last_checked
current_known_revision
next_expected_revision
watch_sources[]
maintenance_priority
```

Different formats warrant different review cadences.

An actively changing payment standard or regulatory format may require
frequent monitoring. A historical format whose final revision is decades
old may require little or no routine monitoring.

**Staleness should be derivable, not remembered.** Axiom 11 records *when*
evidence was obtained; a maintenance process needs to know which adapters are
now **overdue relative to what has been seen**. Those are different questions,
and the second is the one that produces a next action:

``` text
aba.nacha / 2023
    evidence retrieved   2023-04-11
    observations since   14 unknown codes, 2 unexpected lengths
    last reviewed        2024-02-02
    -> review
```

An adapter with old evidence and no observations may be perfectly healthy — a
stable format simply is not moving. An adapter with recent evidence and a rising
observation count is the interesting case, and only the two together say so.
Without this, "continuous maintenance" is an intention; with it, it has a queue.

When a new revision is discovered:

1.  Record the discovery.
2.  Acquire authoritative documentation where possible.
3.  Record documentation provenance and rights.
4.  Compare the new revision with the prior revision.
5.  Classify changes:
    -   syntax;
    -   physical structure;
    -   semantic meaning;
    -   code sets;
    -   validation rules;
    -   effective dates;
    -   extensions;
    -   transport requirements.
6.  Add a new adapter revision.
7.  Preserve the old revision unchanged.
8.  Add or update fixtures.
9.  Add recognition rules.
10. Test cross-version behavior.
11. Release.

The old adapter is not "upgraded." A new historical branch of meaning is
added.

------------------------------------------------------------------------

## 14. Discovery Process

Format discovery is part of the project itself.

Research should range broadly across the financial-services industry
rather than beginning from a fixed implementation list.

Candidate domains include:

-   ACH and clearing;
-   domestic and international wires;
-   ISO 20022 message families;
-   SWIFT;
-   checks and image exchange;
-   cards, interchange, settlement, ATM, and POS;
-   account statements and cash management;
-   corporate treasury;
-   lending and loan servicing;
-   mortgage;
-   securities and trading;
-   market data;
-   regulatory reporting;
-   tax reporting;
-   credit reporting;
-   identity, KYC, and fraud-related interchange;
-   insurance;
-   accounting interchange;
-   open banking;
-   legacy and archival banking formats;
-   vendor-specific interchange formats for which lawful documentation
    is available.

Discovery should record a format even when no implementation can yet be
produced.

Research automation should prefer authoritative sources and record
exactly what evidence was used.

------------------------------------------------------------------------

## 15. Read and Validate Are Different Operations

Reading and validation should remain distinct.

For example:

``` basic
doc = finio.read(file)
issues = finio.validate(doc)
```

Reading attempts to preserve and explain what is present.

Validation answers:

> Does this source conform to the rules of revision X?

This distinction is important for operational and forensic use. Invalid
input is not necessarily unreadable input.

A malformed source should be preserved as far as safely possible, with
structural problems reported rather than causing unrelated source
information to disappear.

------------------------------------------------------------------------

## 16. Export and Translation

Writing requires stronger guarantees than reading.

For example:

``` basic
result = finio.write(doc, "output.ach", { adapter: "aba.nacha", revision: "2026" })
```

Before serialization, the framework should be able to classify the
requested conversion as:

``` text
representable
lossy
impossible
```

If information cannot be represented in the target format, the framework
must report exactly what cannot be represented.

For example:

``` text
Party.MiddleName cannot be represented by target revision.
Original source remains preserved.
```

The default should favor refusal when semantic information would be
silently lost.

Explicitly permitted lossy conversion may be supported, but the loss
report remains available.

------------------------------------------------------------------------

## 17. Round-Trip Fidelity

Where practical, adapters should distinguish two forms of round-trip
fidelity.

### Byte fidelity

Reading and re-emitting an unchanged source reproduces the same bytes.

This is desirable for formats where insignificant representation details
matter operationally or for audit.

### Semantic fidelity

The serialized output may not reproduce identical bytes, but reparsing
it produces equivalent financial meaning.

Every adapter should document which guarantee it can provide.

**Retaining the original bytes is the floor rather than a per-adapter choice.**
Byte *fidelity on re-emission* is genuinely not always achievable — a format
with optional whitespace or ordering may not survive a round trip — but
**keeping what arrived** always is, and it is the only recovery available when
the semantic model turns out to be wrong. A mis-mapping discovered in 2031 is
repairable if the source survived and unrecoverable if it did not, and in a
domain whose formats are expected to be understood imperfectly at first, that
is not a remote possibility. An adapter documents where it cannot reproduce the
bytes; none is excused from retaining them.

Normalization must not be confused with source preservation. A canonical
representation can coexist with the untouched source representation.

------------------------------------------------------------------------

## 18. Policy Boundary

The adapter framework should not decide matters such as:

-   whether a transaction is suspicious;
-   whether an operational exception requires human work;
-   whether a warning is severe for a particular institution;
-   institution-specific workflow;
-   institution-specific business rules;
-   jurisdiction-dependent policy that is not intrinsic to the format.

Those decisions belong to consuming applications or appropriately
versioned policy libraries.

The adapter framework describes evidence, structure, semantics, and
conformance. Consumers decide what to do about them.

### What the framework must expose so a consumer can decide

Drawing the boundary is not enough. **The same format is used a little
differently from one institution to the next**, so local nuance is inevitable —
and it belongs to the consumer precisely because it is not a property of the
format. What the framework owes such a consumer is everything needed to apply
local interpretation **without re-parsing the source**:

-   the **source as found**, so a local rule can look at bytes the semantic
    model did not claim (Axiom 1);
-   **unknown fields, codes and extensions preserved as unknown**, not dropped
    and not coerced into the nearest known thing (Axiom 7) — a local
    interpretation usually lives exactly there;
-   **the code as written alongside any mapped meaning**, since an institution
    that uses a code slightly differently needs the original token, not only
    the framework's reading of it;
-   **provenance back to the source element** (Axiom 2), so a local rule can be
    expressed against where a value came from rather than only against what it
    became;
-   the **adapter and revision that produced the reading**, so a local rule can
    be scoped to the revisions it was written against and does not silently
    outlive them.

A consumer that has those five things can add nuance in its own code, under its
own versioning, without forking an adapter — and the adapter's provenance chain
(Axiom 11) stays true, because no unattributed rule was ever smuggled into it.

If a local interpretation turns out to be a property of the *format* after all
— a genuine variant the specification allows — that is a discovery event and
belongs in the registry as a known variant, not in the consumer forever.

------------------------------------------------------------------------

## 19. Testing Model

Every adapter should have a fixture corpus covering cases such as:

``` text
fixtures/
    valid/
    invalid/
    boundary/
    historical/
    extensions/
    ambiguous/
```

Tests should cover at least:

-   recognition;
-   parsing;
-   source provenance;
-   adapter provenance;
-   semantic mapping;
-   validation;
-   round-trip behavior;
-   historical compatibility;
-   loss reporting;
-   unknown-field preservation;
-   version ambiguity;
-   code-set changes;
-   cross-version behavior.

Where redistribution of official samples is not permitted, synthetic
fixtures should be created from knowledge that may legitimately be used
rather than committing proprietary examples to the repository.

Fixtures should never contain confidential employer, customer, account,
or transaction information.

Executable cookbook examples should be preferred where they can
demonstrate the public API and be regression-tested against known
output.

------------------------------------------------------------------------

## 20. Initial Proving Set

The abstraction should not be declared stable after implementing only
one physical representation.

Initial proving formats should deliberately stress it from different
directions.

Candidates include:

``` text
NACHA
    fixed-width, record-oriented

ISO 20022 camt
    hierarchical XML

OFX
    tagged/document-oriented with historical variants

BAI2
    record-oriented and legacy-heavy

FIX
    tag/value stream

Synthetic vendor spreadsheet
    deliberately drifting tabular schema
```

The synthetic spreadsheet is not intended to reproduce a particular
proprietary vendor format. It exists to test general schema-drift
behavior such as:

-   columns added or removed;
-   columns reordered;
-   columns renamed;
-   one column split into several;
-   several columns combined;
-   changed code meanings;
-   new optional data;
-   deprecated data reappearing;
-   type changes;
-   unknown columns.

If the same architecture survives radically different representations,
the abstraction is more likely to describe the problem itself rather
than merely generalizing one parser.

------------------------------------------------------------------------

## 21. Proposed Project Phases

### Phase 0: Principles, Registry, and One Measurement

Define:

-   axioms;
-   registry schema;
-   provenance model;
-   acquisition classifications;
-   maintenance workflow;
-   legal/source metadata.

No large adapter implementation is required to complete this phase. **One
measurement is**, because a single number decides the shape of the provenance
model rather than tuning it.

Axiom 2 says every interpreted value is traceable to the source it came from. A
100,000-record file at fifteen fields is **1.5 million** source locations and
transformation chains. Whether gBASIC carries that comfortably is not knowable
by reading: it decides whether provenance is **per value**, **per record**, or
**reconstructed on demand from a retained source**, and those are three
different architectures, not three settings.

So: take one real file of a realistic size, build the provenance structure the
model implies, and measure the time and the memory. The point is not a
threshold to pass — it is to know which of the three designs is affordable
before §4 and §5 are fixed.

That is the shape `estateforge`'s EF-0 takes for the same reason: a cost
discovered in Phase 3 is a rewrite.

#### Phase 0 result — measured 2026-09-12

**Status: done.** `examples/finio_lab/provenance_cost.bas` builds all three
architectures over the same NACHA-shaped fixed-width file and answers an
identical query workload from each. Fixed width is deliberately the shape
measured on: every field carries an exact byte offset and length of its own, so
a number here is an **upper bound** rather than a best case — delimited, JSON
and spreadsheet locations are all smaller.

100,000 records × 11 fields = **1.1 million source locations**, from a **9.5 MB**
file. Three repetitions; `/usr/bin/time` for peak RSS; wall clock from outside.
Query cost is the difference between a run with 100,000 queries and one with
none.

| architecture | build | 100k queries | peak RSS | vs. source |
|---|---|---|---|---|
| reconstructed on demand | 0.40–0.50 s | 0.70–0.80 s | **54 MB** | 5.7× |
| per record | 3.80–4.60 s | 0.30–0.60 s | 482 MB | 51× |
| per value | 7.31–7.61 s | 0.70–1.80 s | **2 792 MB** | 294× |

**Per-value provenance is refused.** 2.79 GB to hold the ancestry of a 9.5 MB
file is not a tuning problem, and it is not even bought with speed: it is the
slowest to build *and* the slowest and noisiest to answer, because a query walks
four levels of record and array indirection through a working set that defeats
the cache. §4 and §5 are therefore specified as **shapes a caller receives**,
not as objects the framework stores one of per field.

**The framework retains the source and computes locations on demand.** The
measurement was built expecting a *trade* — that on-demand would buy its memory
back with a query penalty — and **that expectation was wrong**. Per-record is
faster to answer by roughly 0.2 s per 100,000 queries, i.e. about **two
microseconds a query**, and pays 3.8 s of build and 428 MB for it. A consumer
would need some two million provenance queries before the build cost alone paid
back, while holding nine times the memory throughout.

Two consequences for the phases below:

- A layout is a property of the **format**, not of the data, so it is held once
  whatever the record count. That is the whole reason per-record storage buys
  so little: the per-record location is `record_base + layout_offset`, and the
  base is already implied by the record index.
- §5's `sources[]` and `transformations[]` remain **many-to-many and plural**,
  because a revision may compose one concept from several fields. Nothing above
  is an argument for a thinner model — only for not *materialising* it a
  million times over.

The oracle that makes the table mean anything is asserted in
`tests/run_finio.sh`: all three architectures must answer the identical query
workload with an **identical checksum**. Without it a mode could look cheap by
answering a different question.

### Phase 1: Framework

Implement:

-   generic adapter interface;
-   source-location model;
-   source and semantic value models;
-   recognition API;
-   adapter resolver;
-   provenance relationships;
-   loss-reporting primitives.

### Phase 2: Diverse Adapters

Implement at least two or three structurally different formats,
preferably including:

-   one fixed-width format;
-   one hierarchical format;
-   one additional representation.

Use the results to revise the abstraction before treating the API as
stable.

### Phase 3: Historical and Translation Support

Implement:

-   archive scanning;
-   version resolution;
-   historical compatibility tests;
-   round-trip framework;
-   target representability analysis;
-   explicit loss reporting.

### Phase 4: Broad Industry Discovery

Systematically research financial-services formats and populate the
registry.

Classify each discovered format according to availability, authority,
licensing, implementation feasibility, and human acquisition
requirements.

### Phase 5 and Beyond: Continuous Maintenance

Continue:

-   discovery;
-   specification acquisition;
-   implementation;
-   verification;
-   revision monitoring;
-   historical preservation.

The framework is a maintained body of financial interchange knowledge,
not a finite parser project.

------------------------------------------------------------------------

## 22. Relationship to Existing gBASIC Finance Libraries

The adapter framework should remain separate from `finance`.

`finance` performs financial calculations and models financial
mathematics.

`finio` performs financial-industry interchange and interpretation.

The distinction should remain visible to callers:

``` basic
load finance   ' financial calculations
load finio     ' financial industry interchange
```

Higher-level libraries and applications may compose both.

For example, a lending application might use `finio` to interpret an
interchange file and `finance` to perform calculations on the resulting
financial values.

### Four libraries already solve parts of §20, and should be reused

`finio` should delegate rather than grow its own second copy of work this tree
has already done and tested:

| library | what it already does | where `finio` meets it |
| --- | --- | --- |
| `consolidate` | many sources with the same **meaning** and a different **surface** merged into one frame: column aliases, required columns **rejected by name**, per-column type, the percent-scale ambiguity **reported rather than guessed**, and every row carrying **its source** | §20's "synthetic vendor spreadsheet with drifting schema" — this *is* that problem, already built |
| `grid` | a messy worksheet turned into clean frames, reporting a **confidence and its reasons** rather than a plausible wrong frame | recognising structure inside a spreadsheet source |
| `ari` | anchor-relative parsing of irregular reports, with its own spec language, furniture removal, and a money dialect that answers **unknown rather than zero** | fixed-width and report-shaped sources whose layout drifts between producers |
| `xlsx` | the workbook container, retaining **every part** so a read-modify-write loses nothing | the spreadsheet representation itself |

Two of those already hold positions this design argues for independently:
`consolidate` reports an ambiguous percent scale instead of picking one, and
`ari` answers `unknown` for an unparseable amount rather than zero. That is
Axiom 6 and Axiom 7 in code, with suites behind them.

The decision worth making **before** Phase 2 is which shapes `finio` owns and
which it delegates. Building a second spreadsheet reader is the expensive way to
discover the answer.

------------------------------------------------------------------------

## 23. Design Principle in One Sentence

> **gBASIC `finio` is a loss-aware, provenance-preserving, historically
> aware framework and maintenance process for interpreting and emitting
> financial-services data without confusing representation, meaning,
> policy, or time.**

That sentence should remain true even as every individual format
supported by the library changes.
