# gBASIC Financial Adapter Framework

**Status:** Partial — Phases 0–4 built; see §21 and the sections marked with a
phase result. Worked recipes: [finio_cookbook.md](finio_cookbook.md).\
**Path:** `docs/financial_adapters_design.md`\
**Library name:** `finio`\
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

> **Corrected by Phase 1 (see §21).** The entry points are
> `finio.read_text(reg, text, options)` and `finio.read_file(reg, path,
> options)`, and they take a **registry** as their first argument. `read` is a
> gBASIC built-in, so a `finio` function of that name would shadow it for every
> unqualified call inside the library and call itself; and the registry is a
> value rather than a global because gBASIC's actors are fork+exec, so a
> registration performed in a parent is not the child's.
>
> **The samples throughout this document were rewritten to the shipped
> spelling on 2026-09-16**, having kept the proposed one (`finio.read`,
> `finio.readall`, `finio.write`) until then. What forced it is worth
> recording: this document was exempt from `tests/run_stdlib_docs.sh`'s
> removed-function tier *because the index marked it **Proposal***, and the
> moment that status became `Partial` the suite named all three. A document
> that teaches a function which does not exist is worse than one missing a
> function that does — and the exemption that hid it was load-bearing on a
> status line nobody thought of as a test input. The prose of each section
> stands unchanged.

The ordinary API should remain small and stable.

For example:

``` basic
load finio
load finio_nacha

reg = finio.registry([ finio_nacha.adapter() ])
doc = finio.read_file(reg, "payments.ach", {})
```

Resolution should proceed using evidence in roughly this order — but see
§21's Phase 1 result for the step this diagram assumes and a real format does
not supply: **an adapter may honestly be unable to determine a revision**, and
the honest answer is to say so rather than to name one.

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

doc = finio.read_file(reg, "payments.ach", { adapter: "aba.nacha", asof: asof })
```

An options record also gives the framework somewhere to **refuse an
unrecognised option by name**, which a positional list cannot: a misspelled
`reivsion` that is silently ignored leaves the caller believing they pinned an
adapter they did not.

A caller that requires reproducibility can fully pin the adapter:

``` basic
doc = finio.read_file(reg, "payments.ach", { adapter: "aba.nacha", revision: "2023" })
```

Recognition should also be available independently:

``` basic
r = finio.identify(reg, text)
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
report = finio.scan(reg, "archive/")
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
found = finio.scan(reg, "archive/")
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

### The monitoring mechanism

§13 above says what to **record** and what to do **once a revision is
discovered**. It does not say what a watch source *is*, what checking one
*does*, what **triggers** a check, or what a check **emits** — and without
those, "continually maintained" is an intention. This subsection specifies
them.

**The mechanism detects. It never updates.** This is the load-bearing decision
and it follows from Axiom 4 and from step 7 of the procedure above ("preserve
the old revision unchanged"). An automated process that modified an adapter
would silently change how a file written in 2019 is read, which is the one
thing this framework exists to prevent. So a check produces **work**, never a
patch: its output is an entry in a review queue naming what moved and what
evidence says so. A person acquires the documentation, compares the revisions
and adds a new branch of meaning.

#### What a watch source is

A watch source must be checkable **without a human**, which rules out "the
standards body's newsletter" and rules in anything whose state can be observed
and compared:

``` text
WatchSource
    kind        version_catalogue | document | changelog | registry_page
    reference   a URL or other retrievable address
    watching    what a change in it would MEAN
```

`watching` is required and is not decoration. A catalogue page changes when its
footer year changes; that is not evidence a message definition moved. Recording
what a change would *mean* is what separates a signal from a diff.

#### What a check does, and what it may not do

**Checking is a pure function of what the caller fetched.** The framework does
no I/O: `finio_watch.check(source, observed)` takes the content (or the failure)
the caller obtained and returns a finding. That is the shape `agent.apply` and
`nlq`'s three steps already use in this tree, and it is what makes the
mechanism testable with no network — which matters, because a maintenance
process whose tests need the internet is one that goes red when somebody else's
site is down, and gets turned off.

A finding is one of:

``` text
first_sight   nothing was known before; this becomes the baseline
unchanged     the source is as last seen
changed       the source differs, and the finding carries what `watching` said it would mean
unreachable   the source could not be retrieved
```

**`unreachable` is its own outcome and not a quiet `unchanged`.** A source that
has 403'd for six months is not a stable format; it is a watch that stopped
working, and reporting it as "no change" is how a monitoring process comes to
assert the world is still by observing nothing. (Nacha's own developer guide
returned 403 to an automated fetch during the first survey — this is not a
hypothetical.)

#### What triggers a check

Staleness is **derivable, not remembered**, as §13 already requires.
`finio_watch.due(entries, today)` answers it from `next_review_due` and
`maintenance_priority`, so nothing has to keep a schedule in its head, and a
format whose final revision is decades old is not asked about monthly.

#### The other half: what production has seen

Top-down watching finds a revision when a standards body publishes it.
**Bottom-up observation finds one when files start arriving that the adapter
cannot fully explain**, which is usually sooner and is always more specific.
§9's `ObservationLog` is that half.

The adapters already produce the raw material: every `loss_note("uninterpreted",
…)` is a token the adapter preserved and could not account for. What was
missing is that **nothing counted them**. §9 puts it exactly: *"Preserving an
unknown value is only half the benefit; counting it is what turns it into
work."*

An observation records a **token and a location, never content**. That is not a
guideline — it is enforced: the permitted fields carry no amount, name or
account, and a `detail` longer than a token is refused, because a whole record
passed as a "token" is a customer record in a log.

#### The signal is the two together

Neither half decides alone, and §13 already says why: an adapter with old
evidence and no observations may be perfectly healthy, because a stable format
is not moving. `finio_watch.review_queue(entries, log, today)` combines them and
ranks, so the output is a short list of formats to go and read about rather
than a report.


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
doc = finio.read_file(reg, path, {})
issues = finio.validate(reg, doc)
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

> **Only EXPORT is built. TRANSLATION is future work, and it is the largest
> remaining question in this design** (annotated 2026-09-16).
>
> What exists: `finio.write_text`/`write_file` serialize a document back to
> **the format it was read as** — `write_text` resolves `doc.adapter` and
> nothing else — with the three classifications below asked first and a lossy
> write refused by default. `finio_nacha.write_doc` is re-emission byte for
> byte; `finio_pain001` classifies against the scheme's limits.
>
> What does not exist: **any path from a document read as one format to bytes
> written as another.** There is no shared concept vocabulary across adapters —
> NACHA names a field `individual_name`, camt.053 names its counterpart
> something else, and nothing maps between them. Reading a camt.053 statement
> and writing a BAI2 one has never been attempted, and the three
> classifications have therefore only ever judged a document against its own
> format, where "representable" is nearly free.
>
> **This is the remaining item that could still move the architecture**, and
> the reason to think so is that two of the five adapters already did. camt.053
> turned a location from a byte range into a value with a kind (§4); pain.001
> put weight on this section, which was prose until an outbound format leaned
> on it. A cross-format write would ask whether the value model carries enough
> to state a concept independently of the adapter that read it, and that
> question is not answerable by reading.
>
> It should wait for a real requirement. A concept vocabulary built with no
> consumer asking for a specific conversion is an ontology nobody uses, and
> the failure would be invisible: every adapter still works, and the shared
> layer is simply never right for the next format.

Writing requires stronger guarantees than reading.

For example:

``` basic
result = finio.write_file(reg, doc, "output.ach")
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
NACHA                                       [done -- stdlib/finio_nacha.bas]
    fixed-width, record-oriented

ISO 20022 camt                              [done -- stdlib/finio_camt.bas]
ISO 20022 pain.001                          [done -- stdlib/finio_pain001.bas]
    hierarchical XML

OFX                                         [done -- stdlib/finio_ofx.bas]
    tagged/document-oriented with historical variants

BAI2                                        [done -- stdlib/finio_bai2.bas]
    record-oriented and legacy-heavy

FIX                                         [NOT BUILT -- future work]
    tag/value stream

Synthetic vendor spreadsheet                [NOT BUILT -- future work]
    deliberately drifting tabular schema
```

> **Proving set status 2026-09-16: five of seven.** The two that remain are not
> arbitrary leftovers; each is the only candidate stressing something the other
> five do not.
>
> **FIX** is the one remaining *shape*. The five built adapters cover
> fixed-width, delimited, hierarchical XML and tagged-document; a tag/value
> stream is the fifth, and it is `OPEN` in the registry, so it is implementable
> now rather than blocked.
>
> **The synthetic vendor spreadsheet** is the only candidate that stresses
> **drift within one format** rather than difference between formats, and it is
> why §4's `spreadsheet` location kind still has no caller. It is also the only
> proving-set item that is deliberately not a real format — which makes it the
> one whose answer key is exactly known, so recall and false-positive rates can
> be measured rather than judged.
>
> Until both are built, §20's own conclusion — "if the same architecture
> survives radically different representations, the abstraction is more likely
> to describe the problem itself" — is supported by five representations and
> four shapes, not seven and five.

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

#### Phase 1 result — built 2026-09-14

**Status: done, with the first adapter beside it.** `stdlib/finio.bas` carries
the adapter interface, the recognition API, the resolver, validation and the
loss primitives; `stdlib/finio_nacha.bas` is the first adapter (§20's
fixed-width entry). Phase 1 and the first of Phase 2 shipped together
deliberately: a framework whose only consumer is imaginary cannot be tested at
all, and the tiers that matter here are the ones an adapter makes possible.

**NACHA was chosen first because a NACHA file checks itself.** Every batch ends
with a control record stating its entry count, a hash of the routing numbers it
touched and its debit and credit totals, and the file ends with one saying the
same across batches. Those numbers were computed by whoever produced the file,
so the reader is held to arithmetic it did not supply — which is what separates
a test from a transcript, and is the same property that makes `accounting`'s
balance identity and `credit`'s reconciliation good tests rather than goldens.
`tests/run_finio_nacha.sh` makes it three-way: `awk` recomputes the totals from
the bytes, the control records declare them, and `finio` reports them, and all
three must agree batch by batch — with the negative control that the corrupted
fixture must make awk and the control records **differ**, and `finio` must side
with the bytes.

Five things building it corrected, none of them visible by reading.

**1. Phase 0's offsets were codepoints, and its own suite could not see it.**
`open_text` accumulated offsets with `len` and `source_value` sliced with
`mid`, both of which count codepoints, so the field named `byte_offset` stopped
being one the moment a record carried a non-ASCII byte — and in a fixed-width
format the *extraction* moves with it: one UTF-8 É inside a 22-byte name field
shifts every later field one place left, and a 15-digit trace number comes back
as an ordinary-looking 13-digit one with nothing raised. Phase 0's fixture is
pure ASCII, so every check passed either way. Both are bytes now, and both
suites assert it as a **difference** — the two slicings must disagree on the
accented record and `finio` must give the byte one, because asserting only that
it returns the right digits passes on a codepoint reader whenever the fixture
happens to be ASCII. That is PLAT-NUL's standing lesson one library along: a
defect in how one place reads a string is evidence about every place that does.

**2. Framing is evidence, not a setting.** A great many real fixed-width files
— NACHA among them — arrive with **no record separator at all**, as one blocked
run of 94-byte records straight off a mainframe, and a newline-assuming reader
sees a single enormous record. `open_text` is told the framing and retains each
record's actual terminator, so a source can be re-emitted as it arrived; the
adapter's `recognise` decides which framing a file has. The suite asserts the
same logical file in three physical framings — LF, CRLF and blocked — reads
identically, and the runner separately asserts the three are genuinely
different bytes, since "they agree" is otherwise satisfied by three copies of
one file.

**3. §7's resolution diagram assumes a revision can always be settled, and for
this format it cannot.** The Nacha Operating Rules are revised annually; the
record layout has outlived twenty of those revisions and **a file does not say
which produced it**. The adapter therefore declares one revision named
`unresolved` and `recognise` reports no revision at all, rather than naming a
year that would then travel in every document written. The framework uses the
adapter's first declared revision and **records in `reasons` that the source
carried no evidence**. §8's archive report keys on the adapter alone for the
same reason: its example counts "NACHA 2020 82", and a key naming a year here
would be an invention repeated once per file.

**4. `read` could not be spelled `read`.** It is a built-in, and a library
function of that name shadows it for every unqualified call inside the library,
so the §7 spelling would have called itself and `scan` could not have read a
file at all. The entry points are `finio.read_text(reg, text, options)` and
`finio.read_file(reg, path, options)`; `discovery` made the same choice for the
same reason. **The registry is also an argument rather than a global**, which
is the other deviation from §7's one-argument shape: gBASIC's actors are
fork+exec, so a registration performed in a parent is not the child's and a
global would work in a script while quietly holding nothing in a worker.

**5. A claim in the adapter was withdrawn because the perturbation written to
prove it did not go red.** The amount reader carried a comment saying that
dividing cents by 100 in floating point is "wrong in the last cent for values a
test does not try". Measured over every shape a NACHA amount field can hold
plus 300,000 random twelve-digit values, the two agree on **all** of them: a
twelve-digit count of cents is under 10¹², doubles carry integers exactly to
2⁵³, and the quotient's shortest round-trip decimal is the exact one. The exact
path is still the one written — it is exact by construction rather than by the
field width happening to stay inside a range — but the stronger claim was false
and a tier asserting the two differ would have asserted something false.

**6. Two checks never fired, because `/` is float division.** `n - (n / 10) *
10 != 0` is always false in gBASIC: division is not integer division, so the
product reconstructs the dividend exactly. Written that way, the blocking check
(is the record count a multiple of ten) and the blocked-framing length check
(does the byte count divide by 94) both passed everything handed to them. The
first was found by a probe on a deliberately damaged file — **not by reading,
and not by any tier, because every fixture happened to be the right length** —
and the second by sweeping the tree for the same *shape*, which is the standing
rule that a defect found in one place is evidence about every place with the
same form. Both are `floor(...)` now and both are proven red.

Eleven perturbations were proven red, each caught by the tier written for it:
cents read as dollars (**not red — see 5 above**), the direction derived from
the second digit, the entry hash left untruncated, addenda left out of the
count, padding read as a file control, a layout transcribed 1-based as
published guides number it, framing detection guarded with `is_unknown` rather
than `is_nothing`, a write that returns the source instead of reconstructing
it, Phase 0's codepoint slicing restored, validation no longer checking the
record-type sequence, and each of the two float-division checks put back. The
hash tier is the one worth
naming: with two small batches the rightmost-ten-digits rule never fires, so
the fixture carries a **third batch of 120 entries** whose routing numbers sum
past 10¹⁰ — without it the rule would be dead code the suite asserted nothing
about.

**Deliberately not built, and why.** §7's `compare` (interpreting a source
under each candidate and reporting where the readings differ) needs at least
two adapters or two revisions that genuinely disagree; with one of each it
could only be exercised against an invented difference, which is a tier that
asserts nothing. §8's `readall` is `scan` plus a loop and adds no decision.
Both wait for Phase 2's second adapter, which is also what §20 says the
abstraction must not be declared stable without.


### Phase 2: Diverse Adapters

Implement at least two or three structurally different formats,
preferably including:

-   one fixed-width format;
-   one hierarchical format;
-   one additional representation.

Use the results to revise the abstraction before treating the API as
stable.

#### Phase 2 first result — camt.053, built 2026-09-14

**Status: the second adapter is built.** `stdlib/finio_camt.bas` reads ISO
20022 camt.053 bank-to-customer statements, and it was chosen over BAI2
deliberately: BAI2 is record-oriented and legacy-heavy, the **same family** as
NACHA, so it would have exercised the identical layout-and-record path and
settled nothing. §20's argument is that an abstraction surviving only one
representation is a generalized parser rather than a description of the
problem, and only a different representation can ask that question.

**It pushed back at once, in the place that mattered.** A hierarchical source
has no byte range. `xml.parse` builds nodes carrying a name, a namespace,
attributes and children and **no position**; the streaming reader carries a
*line*, not an offset. So `finio.source_value`'s
`{ record, byte_offset, byte_length }` could not serve this adapter, and §4's
own sentence — "a universal byte-offset model is too representation-specific.
The framework should instead provide a generic source-location abstraction" —
became `finio.location(kind, detail)` over §4's own list, with the fields
checked **per kind**. An `xml` location requires a `path` *and* an
`occurrence`, because a statement with four hundred entries has four hundred
elements at the same path.

**The document shape survived. The record did not, and should not.** Both
adapters produce a flat `records` list of `{ kind, fields }` and an `entities`
hierarchy of indices, and the suite walks an ACH file and an XML statement in
one program requiring the same fields by the same names. What differs is the
record: a fixed-width record carries `raw` and a byte range because it **has**
them, and a re-serialization put there under that name would be a lie about
where those bytes came from. **The uniform part is the field and its location;
the record is representation-shaped** — which is true, and was worth
discovering rather than deciding.

Four further results, none of them reachable with one adapter:

**1. The revision is the opposite answer.** A NACHA file does not say which
rule book produced it. A camt document declares its version in the namespace,
so the revision is *determined from the source* and a document declaring one
this adapter does not implement is refused by name. Between them the two
adapters cover both branches of §7's resolution diagram, and §8's archive
report keys on the revision for camt and cannot for NACHA — which is §8's own
example ("NACHA 2020 82") working as written for the first time, and only for
a format whose files declare a version.

**2. `byte_fidelity` stopped being a constant.** Every adapter said `true`
until one could not: re-serializing a parsed XML document cannot reproduce its
bytes, so camt declares `false` and has to earn §17's weaker claim instead —
reparsing the output must yield equivalent financial meaning, and the
difference is reported as `narrowed` loss. The suite asserts both halves and
the contrast between the two adapters.

**3. A document that cannot be stored is not a document.** The writer's first
draft carried the parsed tree on the document; the framework keeps only
`records`, `entities` and `loss` and drops the rest, and it is right to. A live
libxml2 tree is a handle, not a value — `encode` refuses one, it cannot cross
`spawn`, and a document held between two HTTP requests would come back holding
nothing. Axiom 1 makes the fix free: the source is retained, so the writer
reparses. That is the rule `agent` reached from the other direction when its
run had to keep `tools.schema` rather than a toolset.

**4. A correct language-level guard still has to be caught.** An entry
denominated in a currency the account is not held in made `money` refuse the
addition — which is the right guard, and is why this defect cannot silently
produce a nonsense total — but a validator that let the refusal propagate
**died on the malformed statement instead of describing it**, which is exactly
what §15 forbids. Such an entry is now excluded from the totals and reported.

Seven perturbations proven red, each caught by the tier written for it, and one
of them found a hole rather than confirming a tier: dropping the **record's**
occurrence went uncaught, because every location check read a *field's*
occurrence and the record's is a second place the same fact lives. Both are
asserted now and both are proven red separately.

Still deferred, and now for a narrower reason: §7's `compare` needs two
revisions whose differences are **in hand**. camt makes revisions determinable,
which is half of what `compare` needs, and the other half is a second
implemented version — which would mean writing one from memory, the same
invention the NACHA adapter declined to make about rule-book years.


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

#### Phase 4 first tranche — the registry, 2026-09-15

**Status: started, and the honest answer to "has a full survey been done" is
no.** Before this date the registry held **two** entries and both lived inside
the adapter that implemented them, so it recorded only what had already been
built — which is the one thing §9 says a registry is not for. §14's twenty-two
candidate domains had not been surveyed at all.

**Two things were wrong before anything could be populated.**

First, **five of the fields §9 itself specifies were refused by name** by
`finio.check_registry_entry` — `source_type`, `source_url_or_reference`,
`date_retrieved`, `spec_acquisition_method` and `implementation_status` — so a
discovery pass could not have recorded what it found, which is exactly what §14
asks of one. A specification source is now a **record**, as §9's own
indentation always implied, requiring a reference *and a retrieval date*: a URL
with no date is a claim about a page as it is today, and §13's whole
maintenance story is that specifications move. `implementation_status` is
deliberately **not** added — it names the same fact as `state`, whose five
values §9 then enumerates, and two fields for one fact is drift rather than
completeness.

Second, **both existing entries overclaimed, and one was downgraded.** NACHA
said `researched` on no retrievable evidence; camt said `spec_obtained` when no
schema had ever been downloaded. camt is now `discovered` — the specification
genuinely is public and free, which is `spec_public: true` and
`acquisition_class: OPEN`, and that is a **different claim from holding it**.

**And the correction produced something better than a citation.** A freely
published bank ACH layout guide turned out to carry a complete field-position
table, so `finio_nacha`'s six layouts were checked against it: **61 fields, all
matching**, in `tests/finio/nacha_positions_test.bas`. Every other check on
those layouts compares the adapter against fixtures this project generated from
the same understanding the adapter reads them with — a layout wrong in both
places agrees with itself perfectly and every suite stays green. This is the
first statement of those positions that did not originate here. The table is
transcribed 1-based and inclusive **as printed**, and the conversion to
0-based-offset-and-length happens in the checker, because moving it into the
fixture would move the off-by-one out of the code under test.

**The first tranche: 10 formats across 6 families**, chosen to sit in
*different* acquisition classes so the classification means something rather
than being a constant with a type.

| class | formats |
|---|---|
| OPEN | BAI2, ISO 20022 pain.001, ISO 20022 camt.053, FIX, OFX |
| DE_FACTO | NACHA, Swift MT940 |
| CONTROLLED | X12 820, X12 835, ISO 8583 |

**The finding is the shape of that table, not its length.** The formats a small
business most needs are **open**: BAI2 is no longer charged for and several
banks publish complete field-level guides; OFX carries an explicit
royalty-free, worldwide, perpetual implementation licence, which is Axiom 12
answered outright rather than inferred and is rare; FIX is free from its own
standards body; the ISO 20022 family downloads without registration. The ones
behind a licence are the **interchange** standards: X12 licenses its
transaction sets per tier, ISO 8583 must be purchased *and* its useful content
is per-scheme anyway, and Swift's Category 9 reference guide is not freely
published. A suite asserts that split, so a later tranche reversing it has to
say so.

**What this is not.** Six families against §14's twenty-two domains.
`finio_registry.coverage()` reports the gap **as a value**, and the note says
outright that absent formats are absent because nobody has looked — a registry
that knows what it does not know is worth something, one that merely looks
short is not. Checks, image exchange, wires, mortgage, market data, regulatory
and tax reporting, credit reporting, KYC, insurance, accounting interchange,
open banking and the legacy archival formats have had no pass at all.

**Next, on the registry's own evidence rather than on preference:** BAI2 is the
best-supported unbuilt format in the table — open, abundantly documented,
record-oriented, and the format US banks still hand to businesses that camt has
not replaced.


#### Phase 2 second result — BAI2, built 2026-09-15

**The third adapter and the third structural shape.** NACHA is fixed-width,
camt is hierarchical XML, and BAI2 is **delimited and variable-length with a
logical record that can span several physical ones** — the first caller of §4's
`delimited` location kind, which had sat in the enum with nothing using it.
§21's Phase 2 asked for "at least two or three structurally different formats";
this is the third, and the abstraction now carries all three location kinds it
has adapters for.

**The foreign corpus was read before a line of the adapter was written.** That
is the correction from the verification work above, where real files arrived
last and found three defects that had already shipped. Six files from
`moov-io/bai2` (Apache-2.0), including **the specification's own worked
example**, produced four facts no fixture written here would have contained:

- **the record separator is a slash, not a newline** — one real file packs two
  whole records onto a line, and a newline-per-record reader merges them;
- a record whose last field is free text often has **no terminator at all**,
  running to end of line: 102 of 116 records in one sample;
- **that text contains slashes**, which shatters a reader splitting on each
  one — one sample in the corpus is a bug report about exactly this;
- **an `88` continues the previous record's *field list*, not its text.**

The last is the defining feature of the format and the thing this adapter got
wrong first, at a cost that is an ordinary-looking number: read as text, one
sample's first account totals 3,280,000 where its own trailer says 9,150,000,
with four summary amounts simply absent. Verified by hand against that file:
4350000 + 2830000 + 1020000 + 500000 + 450000 = 9150000.

Two more the corpus forced. **A summary group is not four fields** — the funds
type carries its own (`S` three availability amounts, `V` a value date and
time, `D` a count and that many pairs), so stepping a fixed four lands the next
group on a value date, reads it as a type code, and displaces everything after
it. And **`+4350000` is a signed amount**, which the first reader rejected as
non-numeric, losing 4.35 million from a control total.

`finio.open_text` gained a **`"terminated"`** framing with a
`record_terminator`. The fold that reassembles text containing the terminator
is format knowledge and lives in the adapter: it restores the slash to any
fragment that does not begin with a record code it knows. **Only a fragment
that came from a slash split may be folded** — one that begins a *line* is a
record however odd its code, since one real sample carries a line of fifteen 1s
as block padding, and folding that appends it to a transaction's text and loses
a record from the count the file's own trailer states.

**BAI2's self-checking property is the strongest of the three formats here**: a
three-level control total, each account trailer summing its own account, each
group trailer its accounts, the file trailer its groups, with a record count at
every level. A record whose code the format does not define is **reported once
and left out of those counts**; counting it turned one cause into four
findings, three of them pointing away from it.

**Evidence, strongest first.** The specification's own example validates clean
and its arithmetic checks by hand: 500000 + 70000000 + 1500000 = 72000000.
Three of the six foreign files validate clean. An independent Python reading
agrees with this adapter on every controlled file **and on one where both
disagree with the file**: sample4 declares an account total of
−1,260,161,341,762 against 666,917,818 computed twice independently, and claims
four accounts while containing five, which `grep` confirms. That file is
internally inconsistent and the adapter is right about it.

Six perturbations proven red, and **one only after being noticed**: disabling
the fold broke nothing, because the fixtures generated here had no slash inside
a text field. There is now a controlled one.

**BAI2 moved out of the registry queue into its own adapter entry, and the
duplicate-id rule made that compulsory rather than tidy** — `finio_registry.all`
refuses an id that appears in both and would not run until the queue entry was
removed. The registry suite then failed for the other half of the same move,
which is the rule working in both directions.


#### Phase 2 third result — OFX, built 2026-09-15

**§20's schema-drift case, occurring inside a single format.** OFX 1.x is
SGML-like and *not well-formed XML* — a leaf element is `<CODE>0` with no
closing tag — while 2.x is proper XML. Same element tree, two serializations,
and a file of each kind is ordinary. Until this, the drift §20 describes had
only ever arrived *between* formats.

**Conversion was rejected, and that is the decision worth recording.** The
obvious approach is to insert the missing closing tags and hand the result to
an XML parser, which is what more than one public tool does. It is refused here
because **the locations would point into text the bank never sent**: Axiom 2
says an interpreted value is traceable to its source, and a byte offset into a
document this library invented is not provenance. One reader walks the tag
stream of the original bytes, and the single rule that makes that possible is
that a closing tag whose name does not match the innermost open aggregate is a
*leaf's* — which 2.x writes and 1.x does not. Both dialects close aggregates;
the difference is about leaves alone, and a tripwire written without knowing
that failed on its first run.

**OFX states no control total, and the three formats before it all did.** NACHA
states hashes and totals, camt an opening and closing balance, BAI2 a
three-level checksum — it would have been easy to generalise from three. An OFX
ledger balance is a *balance*, not a sum of anything in the file. So validation
here cannot reconcile arithmetic, and checks what consumers actually depend on
instead:

- **the response is not an error.** Two corpus files are error responses (codes
  15500 and 2000), and one carries a zero status at signon with the failure
  further in — so the check cannot stop at the first. A reader that walks past
  a failed response finds no transactions and reports a balance of nothing,
  which looks exactly like an account with no activity.
- **FITIDs are unique.** The FITID exists so a consumer can recognise a
  transaction it has already imported, and every piece of accounting software
  that reads OFX keys on it. A duplicate silently drops or doubles a
  transaction and nothing anywhere reports it.
- **a transaction belonging to no statement is reported**, which catches a
  structure the adapter does not model rather than a file that is wrong. One
  fixture carries four transactions inside an `INVSTMTRS` investment statement;
  without the check it reads as a clean bank statement with no activity.

**One defect found by the corpus and worth naming, because it produced silence
rather than an error.** Containment was written as close order — attach each
record to "the current statement" — and a transaction closes *before* the
statement holding it, so every transaction attached to nothing. Ten real files
read with **zero transactions and validated clean**. An empty statement and a
healthy one are indistinguishable, which is the failure this library exists to
prevent; containment is an index range now, known at close time.

**Evidence.** Ten files published by real financial institutions (ANZ, Suncorp,
Fidelity and others) from the ofxparse project's MIT-licensed corpus, read
**before the adapter was written** — it carries both dialects, two error
responses, an entirely empty document, one file opening with twelve blank lines
before its header, and vendor extension tags with a dot in the name. The
controlled fixtures supply what the corpus cannot: **the same logical statement
in both dialects**, since every real file is one or the other, and a duplicate
FITID, which no publisher ships on purpose. Six perturbations proven red.

**OFX is the only format in this registry whose specification grants an
explicit royalty-free, worldwide, perpetual implementation licence** — Axiom 12
answered in writing rather than inferred.

##### 2026-09-18 — a file from an institution nobody here had seen

A real credit-union download, in the four formats its online banking offers:
`.CSV`, `.OFX`, `.QBO` and `.QFX`. The last three are all **OFX 1.0.2 SGML** —
QBO and QFX add Intuit's `<FI>`, `<INTU.BID>` and `<INTU.USER>` and differ from
each other only in the BID value. The adapter recognised all three `exact`,
read 13 transactions from each, kept the extension tags as ordinary fields, and
reported nothing. That is a fifth institution confirming the reader.

**The CSV was the most valuable of the four, and not because of CSV.** It is an
*independent second rendering of the same statement*, which is the one thing a
corpus written here can never supply. Against it the OFX read was checked rather
than observed: 13 ↔ 13, every amount and every date agreeing, the CSV's running
balance reconciling end to end, and OFX's stated `LEDGERBAL` equal to the CSV's
newest balance. Agreement between two files neither of us produced is evidence;
one file alone is a transcript.

**IT FOUND A DEFECT, AND IT WAS IN OUR CODE.** Every value this adapter produced
was TEXT — it was the only one of the five that did. Measured:

``` text
summing three transaction amounts the obvious way gives
"-28.00-5.00-28.00", type string
```

No raise, no diagnostic, a plausible-looking answer of the wrong kind. It
survived the adapter's whole life because **every assertion about an amount in
this tree goes through `string(...)`, and `string(money)` and the decimal text it
was parsed from are the same characters.** Only arithmetic tells them apart, and
every fixture was written by the hands that wrote the reader. `TRNAMT` and
`BALAMT` are now `money`, denominated by the statement's own `CURDEF`, with the
not-a-decimal check moved from validation to construction so an unreadable
amount is `invalid` where it is built — the rule camt already followed.

**Dates are deliberately NOT typed**, because *that* is the consistent choice:
camt leaves `BookgDt` and `CreDtTm` as text and so does every other adapter. An
OFX date additionally carries a bank-stated zone, and this file states
`[-8:PST]` on `DTPOSTED` while stating `[-5:EST]` inside every `FITID`, so
choosing one and calling it the instant would be an invention.

**The generalisable outcome is `tests/finio/amount_kind_test.bas`**, a declared
table of what kind of thing each adapter's transaction amount is, wired into
`run_finio.sh`. It sits above the adapters because no single adapter's suite can
see that one of them is the odd one out. Every adapter must appear in it, so a
new one picking a third representation fails there rather than in the first
program that totals its amounts. Two exceptions are recorded **with their
arguments**, because an exception nobody wrote down is indistinguishable from
the defect: BAI2 amounts are in minor units with no currency, and pain.001's
`CtrlSum` carries no currency of its own. *The first draft of that tripwire
reported pain.001 as broken* — it had scanned for the first field whose name
contained "amount" or "sum" and found `control_sum`. A tripwire whose false
positives look exactly like its true ones is not a tripwire.

**A second defect, found by construction rather than by a bank, fixed anyway.**
The scanner classified **any element with no content as an aggregate**. In 1.x —
where a leaf has no closing tag — that pushes a name nothing will ever pop:
every close after it mismatches, containment collapses, and the document comes
back with **zero statements and no error**. Measured at 9 records and 0
statements. That is the same shape this adapter already shipped once from a
different cause, and an empty `<MEMO>` or `<CHECKNUM>` is ordinary enough in 1.x
to reach it. No file in the corpus has one — not the ten foreign vectors, not
the credit-union download — but the rule is the format's own convention rather
than a guess about data: an element whose closing tag never appears after it is
a leaf, and one whose closing tag is the *very next* tag is the 2.x spelling of
the same thing. One answer in both dialects, which is this adapter's whole claim.

**What these files could not give.** Nothing here is a print image, so `ari`'s
L0 is untouched. And one month cannot establish a recurring charge's *pattern* —
only that the charge exists.

**Two lessons about the fixtures themselves**, both the same class and both hit
in one session: a perturbation that collapses the record tree made the suite die
on an **index** at the exact check written to report it, and a second one made
it die on a **missing field**. Both are guarded now — a collapsed document and
an absent field are answers, not crashes — because a red tier that reports
nothing is the failure mode this suite exists to prevent, one level up.


#### Phase 2 fourth result — pain.001, and §16 implemented, 2026-09-15

**The first adapter in this tree for a file you SEND.** Every previous one
reads a report — an ACH file, a statement, a balance report, an OFX download —
and all four say some version of "re-emission only" for `write_status`. A
pain.001 is an instruction: the message a business sends its bank to say *make
these payments*. That inverts where the risk lives. Reading a statement wrongly
gives a wrong number on a screen; writing a pain.001 wrongly gives a payment
run the bank rejects, or executes.

**Which is why §16 had never been implemented.** Its sentence — "writing
requires stronger guarantees than reading… classify the requested conversion as
representable, lossy or impossible… the default should favor refusal when
semantic information would be silently lost" — had four read-only adapters
under it and no weight at all. It is real now:

- `finio.classify_write(reg, doc)` asks **before** anything is serialized. An
  adapter that can answer declares `classify_write`; one that cannot is
  `representable`, which is the honest reading of *this adapter knows no reason
  the target cannot hold it*.
- `finio.write_text(reg, doc, allow_lossy = false)` **refuses a lossy write by
  default** and refuses an impossible one **with no override**. The difference
  is the point: one is a cost a caller may accept having been told, the other
  is a fact about the target format.
- The refusal is enforced **twice**, at the framework boundary and again in the
  adapter's own writer, so a caller reaching the writer directly cannot get
  past it. That is R9's treatment in `reasoning` and it was measured rather
  than assumed: removing the framework check alone still refused.

**The limits checked are the SCHEME's, not the SCHEMA's**, and that distinction
is the whole reason this is worth doing. An XSD accepts a 200-character
creditor name; the SEPA credit transfer scheme carries 70. An adapter that
serialized it would produce a file that **validates**, gets sent, and comes
back refused — or pays the right amount to a name nobody can match. A non-EUR
amount is `impossible` rather than lossy, because no truncation makes it
representable.

**The control totals point the other way from every previous format.** A
pain.001 states `NbOfTxs` and `CtrlSum` at two levels, per payment block and
per message. Reading a file, those are somebody else's arithmetic to check;
writing one, they are arithmetic **you must get right**. The same numbers that
were a test oracle in four adapters are, here, most of what the adapter is for.

**Two things the corpus could not supply, and both mattered.** Every real SEPA
sample has a *single* payment-information block, so its group control sum is a
copy of the block's rather than a total — a two-level checksum tested only
where the two levels are equal tests one level. And no publisher ships a scheme
violation on purpose. The generated fixtures supply both, and the suite asserts
the two levels are caught **independently**.

**`finio_iso20022` was extracted at the second caller, not the first.**
`finio_camt` held the namespace-version rule, the `Ccy`-attribute amount and
Axiom 7's absent-versus-invalid field privately, which was right while it was
alone. pain.001 needs all three. The same argument says **not** to extract
`finio_ofx`'s tag scanner, which still has one caller.

**Two defects worth recording, both found by running rather than reading.** The
write classification returned `representable` for a document with a
73-character creditor name, because `_check_length` appended to an array passed
as a parameter — and `append` inside a function mutates a **local copy**. The
failure was in the permissive direction: a file that would be truncated or
rejected, declared fit to send. And a "corrupted" fixture whose block control
sum was set to 1337.45 corrupted nothing, because that is exactly the correct
total for that block — a number picked by hand from the same arithmetic the
block already had.

Six perturbations proven red. **And the `finio_all` tripwire fired on its first
real occasion**, one day after it was built: a fifth adapter on disk, not wired
into the one list, caught immediately rather than surfacing later as a count
that was one short.


#### Verification against foreign files — 2026-09-15

**Nothing in `finio` had ever met a file it did not write.** Both registry
entries said so, and §9's `verified` state had never been reached by anything.
Every fixture was generated here from this project's model of each format, so
the arithmetic oracles — strong as they are — check our reader against our
generator's **shared** understanding. A layout wrong in both places agrees with
itself perfectly and the whole gate stays green.

Ten ACH files were taken from the **moov-io/ach** project — files produced by a
different implementation, in a different language — under Apache-2.0, which
permits use and redistribution with attribution. They are committed with the
licence beside them and a provenance manifest recording, for each file, its
source, the date it was retrieved, its terms, and a hash.

**The rule applied, and it has three arms rather than two:** an explicit
prohibition on use means the file is **excluded and the exclusion recorded**; an
explicit permission means it may be used and redistributed only if it says so;
and **silence** — the common case, a bank publishing a sample in its developer
documentation having granted nothing in writing — means the file may be **read
and not committed**. `finio.check_test_vector` enforces this, refusing a
prohibited vector **by name** rather than recording it with a flag a caller can
forget to read. Two sources were excluded for a fourth reason that is neither
permission nor prohibition: the Goldman Sachs camt.053 samples and Nacha's own
developer guide both returned **HTTP 403** to an automated fetch. That is
unavailability, it is recorded as such, and it is precisely why `finio_watch`
has `unreachable` as a finding distinct from `unchanged`.

**Three defects, none of which our own testing could have found.**

**1. Four of ten files were refused outright.** Their producer strips trailing
blanks, so a file header arrives at 75 bytes and a file control at **exactly
55** — 94 less the 39-byte blank reserved field. Nothing is lost; the missing
tail is the blank part. A strict 94-byte rule rejected the file entirely, which
is what §15 forbids: refusing destroys the only thing an operator can work
from. Short records are now read and **reported**, recognition answers `strong`
rather than `exact` because the widths do not match what the header itself
declares, and a field beyond the end of a record is **`unknown`** under Axiom 7
— not blank, not zero, because padding the record would turn *absent* into
*blank*, which is the distinction Axiom 7 exists for. A partially present field
is `invalid`: the first six digits of a truncated ten-digit amount are an
ordinary number a hundred times too small.

**2. One file carries non-ASCII** — a record of 94 codepoints and 95 bytes, in
a format the specification says is ASCII. It is read by byte and **reported**,
rather than the reader silently switching to counting codepoints: doing that
would choose one producer's interpretation of a fixed-width format over
another's with nothing to go on. An overlong record that is over in *both*
measures is still fatal to recognition, because that is wrong framing rather
than a trimmed blank.

**3. `block_count` was computed as a floor** where the field counts the blocks
the file **occupies**, which is a ceiling. A conforming file is a multiple of
ten records so the two agree — and every fixture generated here is conforming,
which is why it survived until a real file arrived with its trailing 9-filler
stripped. 93 records occupy 10 blocks; the file says 10 and we said 9.

**What the corpus also established.** Two of the ten validate with **zero
issues** — our reading of somebody else's file, reconciling against control
records we did not compute. The other eight each produce a precisely diagnosed
finding: two real defects in the files themselves (one declares five batches
and holds four; one has lost its padding), and two honest limitations here —
ADV batches use an entry layout this adapter does not implement, now **named**
rather than silently mis-read, and several real transaction codes are absent
from the direction table and are **reported rather than guessed**.

`recognition_status` and `read_status` are therefore `verified`, while `state`
stays `researched` and `validation_status` stays `implemented`. Those are
different axes and the distinction is the entire reason §9 has five states and
four per-capability statuses rather than one flag. **camt.053 remains
unverified**: no bank's statement has been read, because the published samples
could not be fetched.


### What is NOT built, as of 2026-09-16

Consolidated here so a reader does not have to reconstruct it from five
sections. The framework itself is complete: every mechanism §21's phases name
exists and carries a suite. What remains is listed in the order it could still
change the design.

**1. Translation (§16).** Every write re-emits the format it was read as.
There is no cross-format path and no shared concept vocabulary. This is the
only remaining item that could still move the architecture — see the note at
§16 — and it should wait for a real requirement rather than be invented.

**2. Two of §20's seven proving formats.** `FIX` (tag/value stream, the one
remaining *shape*, and `OPEN` so implementable now) and the **synthetic vendor
spreadsheet** (the only candidate stressing drift *within* one format, and the
reason §4's `spreadsheet` location kind has no caller).

**3. Two registered formats that could be built today.** `fix` (`OPEN`) and
`swift.mt940` (`DE_FACTO` — the authoritative Category 9 guide is not freely
published, but public implementation documentation is abundant and consistent).
Three more — `x12.820`, `x12.835`, `iso8583` — are `CONTROLLED` and blocked on
a **licence purchase by a person**, which is a purchase order and not work.

**4. camt.053 has never met a bank's statement**, and pain.001 rests on two
foreign files. ACH, BAI2 and OFX each have a foreign corpus and those corpora
found defects in three of the four adapters they touched, so the evidence base
is **not uniform across adapters** and the registry's per-capability statuses
are where to read which is which.

**5. The `json` location kind has no caller.** `spreadsheet` is covered by item
2; `json` awaits a format that is natively JSON.

------------------------------------------------------------------------

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
