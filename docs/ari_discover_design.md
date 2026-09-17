# ARI Discover: Automatic ARI Specification Inference

Status: Design draft — **revised 2026-09-16 to fit gBASIC as it now is.**
Unbuilt.  
Target library: `stdlib/ari_discover.bas`  
Companion runtime: `stdlib/ari.bas`  
Proposed public name: **ARI Discover**  
Corpus: `examples/fixtures/ari_discover/` (built; see its MANIFEST)

> **What the revision changed.** The draft was written against gBASIC as
> described rather than as measured, and four of its premises were checked
> against the tree and did not hold. Each correction is marked in place with a
> `> **Corrected**` note so the original reasoning stays legible:
>
> 1. **§9 — gBASIC is no longer arity-strict.** Literal default parameter
>    values shipped (PLAT-OPTPARAM), so the proposed `*_default` convenience
>    twins would permanently double the public API to work around a limitation
>    that no longer exists.
> 2. **§6.1 — the index space disagrees with the engine.** The draft proposes
>    byte offsets; `ari` locates with `len`/`mid`, which are **codepoints**.
>    Mixing them yields a rule that is correct until a description contains a
>    non-ASCII character.
> 3. **§20 — the "prerequisite" is half built.** `ari.inspect` and
>    `ari.clean_grid` already exist; only span-level claimed/unclaimed is
>    missing.
> 4. **§2 — `load "ari.bas"` is not gBASIC syntax.**
>
> Added: **§5.3** (the language constraints an implementer will actually hit),
> **§15.5** (the null corpus, and why it is load-bearing), and a corpus that
> exists rather than one assumed.

## 1. Purpose

ARI Discover analyzes one or more flat-file, print-image reports and proposes an ARI specification capable of parsing them. It is intended for reports whose logical structure is expressed through recurring text, whitespace, indentation, value shapes, page furniture, and relative placement rather than through a reliable machine-readable schema.

The system does not promise to infer the author's intent perfectly. Its contract is narrower and testable:

1. discover repeated and variable structures in the supplied corpus;
2. propose one or more explainable ARI parsing hypotheses;
3. execute those hypotheses with the existing `ari` parser;
4. measure coverage, consistency, ambiguity, and failures;
5. return a candidate specification plus evidence and unresolved questions.

The expected workflow is assisted specification development, not opaque one-shot code generation.

## 2. Architectural boundary

The existing `ari` library remains the deterministic execution engine:

```basic
result = ari.parse(report_text, spec_text)
result = ari.import(path, spec_text)
```

Discovery belongs in a companion library:

```basic
load ari
load ari_discover

profile = ari_discover.profile(report_text)
proposal = ari_discover.infer(sources)
validation = ari_discover.validate(sources, proposal.spec)
```

> **Corrected.** The draft wrote `load "ari.bas"`, which is not gBASIC: `load`
> takes a library **name**, optionally with `as` to bind a different local name
> or `from` to name a file. Inside `ari_discover.bas` itself the sibling form is
> `load ari from "ari.bas"`, which is what every stdlib library that depends on
> another already uses.
>
> The calls above also drop the trailing `options` argument, because it now has
> a default — see §9.

This separation preserves several useful properties:

- Known specifications can be executed without paying discovery costs.
- The dependable parser has no dependency on clustering or an LLM.
- Discovery can evolve without destabilizing ARI's specification semantics.
- Candidate specifications are tested by the same runtime that will execute them in production.
- The generated artifact is ordinary ARI source and is not tied to the discovery engine.

`ari_discover` may depend on `ari`, `frame`, and other deterministic gBASIC libraries. The dependency must never point in the opposite direction.

## 3. Goals

ARI Discover should identify and describe:

- repeated page headers, footers, page numbers, timestamps, and separator lines;
- document sections and nested subsections;
- repeated row families such as detail rows, totals, subtotals, and continuation lines;
- invariant literals suitable as anchors;
- variable spans and their likely types;
- relationships such as “last money value on the row” or “date following this label”;
- optional sections, alternate line forms, and layout drift;
- likely field names derived from headings or adjacent labels;
- unclaimed text and structures that do not fit the dominant model.

The design should work without network access, API credentials, or an LLM. Optional intelligence providers may improve naming and interpretation but must not replace measured validation.

## 4. Non-goals

The first implementation will not:

- guarantee semantic understanding from a single example;
- silently choose among materially ambiguous interpretations;
- treat fixed columns as authoritative merely because samples happen to align;
- rewrite or extend the ARI language as part of inference;
- send report contents to an external model without an explicit caller choice;
- make a generated specification production-ready without validation evidence;
- learn continuously or mutate an accepted production specification automatically.

## 5. Inputs

### 5.1 Corpus

Inference accepts either one report or a corpus of related reports. Multiple reports are strongly preferred because variation across files distinguishes true constants from accidental constants.

Useful corpus diversity includes:

- different report dates;
- different branches, departments, or accounts;
- reports with and without optional sections;
- multiple page counts;
- positive, negative, zero, and missing values;
- long and short identifiers or descriptions;
- known layout revisions.

Each source should retain an identifier and, where available, non-content metadata such as report date or known format version. The source identifier must appear in every diagnostic.

### 5.2 Options

Initial options should include:

```basic
options = {
    minimum_support: 0.80,
    minimum_confidence: 0.70,
    maximum_section_depth: 4,
    allow_fixed_columns: false,
    redact_examples: false,
    llm: nothing
}
```

Defaults should favor resilient anchor-relative rules over positional rules.

**The record must be validated by name, and an unrecognised field refused.**
This is the rule `webserver.listen`, `odbc.connect`, `discovery.scan` and
`finio.open_text` already follow, and the reason is not tidiness: a misspelled
`minimum_suport` that is silently ignored leaves the caller believing they
raised the threshold when they did not, and the run that follows looks like a
successful one. The same decision cost `reasoning.check_context` a whole
increment to retrofit — a `Context` written with the singular `objective`
produced `materiality: unknown`, which is *also* the designed honest answer when
no threshold was declared, so a typo was indistinguishable from a deliberate
omission.

`llm: nothing` is deliberate and is not the same as `unknown`. `nothing` is "no
advisor supplied"; `unknown` is the answer to a question nobody asked. A caller
reading back `options.llm` must be able to tell "I chose to run without one"
from "this field was never set".

**`minimum_support` is meaningless below about ten sources**, and the design
should say so rather than let a caller discover it. With three sources, `0.80`
means "3 of 3" because 2/3 is 0.67 — so `0.80` and `0.95` are the same
threshold and neither can be calibrated. §15 sizes the corpus for this.

### 5.3 gBASIC constraints this design has to live inside

Added by the 2026-09-16 revision. None of these is a limitation to route
around; each one changes a shape the draft proposes, and each has already cost
another library in this tree a defect. They are listed here so an implementer
meets them on the page rather than in a debugger.

**Arrays and records are values, and `append` inside a function mutates a
local copy.** A helper written as `add_family(families, f)` does nothing to the
caller's array. Every pipeline stage must **return** its accumulator. This cost
`accounting` its whole API shape — `post` returns the new ledger — and it is why
`fake`'s generators are pure functions of `(seed, index)` rather than streams:
a record is a value, so `s.n = s.n + 1` inside a function cannot advance a
stream object, measured.

**`for each item in list` does not write back.** `item.type = "money"` inside
the loop is discarded silently. Since 2026 the index form exists —
`for each item, i in list` — and the idiom is `list[i] = item`, because that is
an lvalue path and paths write in place. A write to the element that nothing
reads afterwards raises warning 2107 (`discarded write`).

**There are no closures.** `map` cannot capture, so a per-model or per-corpus
callback needs one named function per instance. This bites §11.1's advisor
contract directly: an adapter cannot be a closure over a configured endpoint, so
it must be a function value taking everything it needs as arguments, or a record
carrying a function value plus its configuration. It also bites `spawn`, which
resolves a **bare function name** — neither a library function nor a function
value — so a parallel inference pass cannot be spawned over a lambda.

**String concatenation in a loop is quadratic.** Building a signature or a
generated specification by `s = s + part` over thousands of lines is the trap
`UNLEARN.md` names. Collect into an array and `join` once. Indexing and `append`
are linear (PLAT-STRIDX, PLAT-ARRIDX), so an array accumulator is cheap.

**`keys()` is insertion order**, which is what makes §17's determinism criterion
achievable at all — but only if every tally is built in a deterministic order.
A cluster map keyed by signature is deterministic; one keyed by an id drawn from
a hash is not.

**`new` and `step` are reserved words and cannot be function names.** `chart`
had to be `chart.spec` rather than `chart.new` for exactly this, and
`agent.apply` is named that because `step` is taken by `for ... step`. If a
constructor is wanted here, `ari_discover.spec(...)` or `.candidate(...)` works
and `.new(...)` will not parse.

**Comparison of two compound values is deep, and ordering them is refused.**
`=` on two records compares field by field, by name; `<` on two records raises.
That is what makes "have I seen this signature before?" writable as
`contains(seen, sig)` — but a `find` that misses returns `nothing`, and
`is_unknown(nothing)` is **false**, so an `is_unknown` guard on `find`'s result
reads as "found at index nothing". Use `contains`.

**A raise can be caught, and `on error` is frame-scoped.** A candidate
specification that fails to parse is an ordinary outcome here, not a crash, so
Phase 8 runs each candidate under `on error goto next` and records the failure
as evidence. Note the anti-silence rules: a second raise while one is pending
escapes the frame, and returning with an unacknowledged pending error re-raises
— so a scoring loop must claim each error before moving to the next candidate.

------------------------------------------------------------------------

## 6. Intermediate representation

Discovery should not operate directly on raw strings after ingestion. It should build an intermediate representation that preserves both content and provenance.

### 6.1 Source grid

Each physical line becomes a record containing at least:

```text
source_id, physical_line, page_guess, text,
byte_start, byte_length,          ' provenance, into the file
cp_start,   cp_length,            ' rules, into ARI's own index space
indent, trimmed_length, blank, separator_score
```

No preprocessing step may destroy the mapping back to the original bytes.

> **Corrected — and this is the subtle one.** The draft carried `byte_start`
> and `byte_length` alone. **`ari` does not locate in bytes.** Its
> `_apply_columns`, `_locate_in_line` and every recognizer use `len` and `mid`,
> which count **codepoints**, so `columns 12 34` in a generated specification
> means codepoints 12..34 and not bytes.
>
> A discovery engine that measured in bytes and emitted a `columns` rule would
> produce a specification that is correct on every ASCII line and silently wrong
> on the first line containing a non-ASCII description — one UTF-8 `É` shifts
> every later field one place left, and the extracted value is an
> ordinary-looking shorter string with nothing raised.
>
> **This exact defect has already been shipped in this tree, one library over.**
> `finio` Phase 0 accumulated offsets with `len` and sliced with `mid`, called
> the result `byte_offset`, and its own fixture could not see it because the
> fixture was pure ASCII. It took a foreign corpus carrying one 95-byte,
> 94-codepoint record to expose it.
>
> So **both are carried and they are never mixed.** The rule is:
>
> - **codepoints** are what a generated ARI rule may contain, because ARI is
>   the judge and that is ARI's space;
> - **bytes** are what provenance reports (§12, principle 6), because that is
>   what maps back to the file a person will open;
> - a field name says which, always. No field called `offset`, `start` or
>   `position` without a `byte_`/`cp_` prefix.
>
> The two coincide on ASCII, which is precisely why this cannot be left to
> be noticed later: every fixture in `examples/fixtures/ari_discover/` is
> ASCII today, so the corpus **cannot** catch it. §15.3 therefore requires a
> non-ASCII adversarial source, and it is the one adversarial case that must
> exist before Phase 1 rather than after.

### 6.2 Token spans

Recognized spans include their original location, normalized value, and competing type hypotheses:

```text
text, start, length, type_candidates, selected_type, confidence
```

Candidate types initially include:

- money;
- integer and decimal number;
- percentage;
- date and time;
- identifier/code;
- page number;
- free text;
- punctuation or separator;
- blank padding.

Types may overlap. For example, `20260916` may plausibly be a date, identifier, or integer. Ambiguity is recorded rather than discarded.

### 6.3 Line signature

A line signature replaces variable spans with typed placeholders while preserving stable literals and rough spatial relationships. For example:

```text
12345  SMITH, JOHN       $1,245.00
8291   JONES, MARY          82.50-
```

may yield a common structural signature resembling:

```text
<ID> <TEXT> <MONEY>
```

The signature should preserve useful features such as indentation, token order, gaps, terminal punctuation, and whether a typed value is first or last. Exact columns are evidence, not identity.

### 6.4 Region and row-family hypotheses

Lines are clustered into row families. Contiguous and recurring families form candidate regions. Regions may be nested to represent document, section, subsection, and repeating record scopes.

Every inferred object carries:

```text
id, kind, support, confidence, evidence[], counterexamples[], alternatives[]
```

## 7. Deterministic discovery pipeline

### Phase 1: Ingest and normalize

- Detect newline convention and form-feed page breaks.
- Preserve original bytes and line numbers.
- Expand no tabs by default; instead record tab positions so provenance remains exact.
- Record blank lines, indentation, repeated characters, and printable/non-printable anomalies.
- Produce a normalized comparison view without changing the source view.

### Phase 2: Recognize typed spans

Use ARI-compatible recognizers wherever possible so discovery and execution agree about what constitutes money, dates, and other values. Run specific forms before generic forms and reject overlapping generic matches already claimed by a more specific recognizer.

The recognizer should emit alternatives instead of forcing premature selection.

### Phase 3: Detect page furniture

Candidate furniture is identified by recurrence near page boundaries, similarity across pages, and variable slots such as page number or run date.

A line is not furniture merely because it repeats. A repeated section heading inside the report body must remain available to section inference. Furniture classification therefore requires positional evidence in addition to textual recurrence.

### Phase 4: Create line signatures and clusters

Generate increasingly abstract signatures:

1. literal signature;
2. typed-token signature;
3. whitespace-tolerant structural signature;
4. anchor-and-type signature.

Cluster similar lines using interpretable distances over token sequence, stable literals, indentation, and gap patterns. Retain minority clusters rather than forcing all lines into a dominant family.

### Phase 5: Infer sections and subsections

Section boundary evidence includes:

- headings recurring in the same structural role;
- indentation changes;
- blank-line boundaries;
- separator lines;
- changes in row-family distribution;
- totals or terminators;
- repeated heading/body/total sequences;
- page furniture removal exposing continuity across page breaks.

Infer a hierarchy only where the evidence supports it. A flat list of regions is preferable to a confident but invented hierarchy.

### Phase 6: Infer anchors and fields

For each variable span or repeated field position, generate candidate locators such as:

- typed value after a stable literal;
- typed value before a stable literal;
- first or last value of a type on the row;
- value between two anchors;
- nth typed value within a row family;
- value on a line relative to a section anchor;
- fixed span, but only when explicitly allowed or proven more stable than alternatives.

Anchor quality should consider:

- uniqueness within the relevant scope;
- recurrence across sources;
- proximity to the target;
- stability under drift;
- collision rate;
- whether it survives page-boundary variation;
- whether it accidentally occurs inside variable data.

### Phase 7: Generate candidate ARI specifications

Generate more than one candidate when meaningful alternatives remain. Prefer the smallest specification that explains the corpus without discarding exceptions.

Generated source should include comments identifying:

- confidence and support;
- source examples used as evidence;
- assumptions;
- unresolved alternatives;
- rules that rely on exact position.

### Phase 8: Execute and score

Run every candidate through `ari.parse` against every source. Scoring must be based on observed behavior, not only on the inference model.

### Phase 9: Refine

Use parse failures, collisions, inconsistent types, and unclaimed spans to revise candidates. Refinement terminates when:

- all acceptance thresholds are met;
- no candidate improves materially;
- or the configured iteration limit is reached.

The system must report which condition ended refinement.

## 8. Validation and scoring

A single confidence number is insufficient. Return a scorecard containing at least:

| Measure | Meaning |
| --- | --- |
| Source coverage | Fraction of reports parsed without structural failure |
| Region coverage | Fraction of inferred regions claimed by the specification |
| Content coverage | Fraction of meaningful non-furniture text explained |
| Field consistency | Agreement of field types and roles across repeated rows |
| Anchor stability | Success of anchors across sources and layout drift |
| Collision rate | Frequency with which a rule matches multiple unintended locations |
| Unknown rate | Fraction of extracted values converted to `unknown` |
| Positional dependence | Degree to which exact columns are required |
| Exception count | Unmodeled or specially handled cases |

Confidence should be computed from these measured components and should retain the component values. A high aggregate score must not hide a serious collision or a report that was excluded.

### 8.1 Counterexample testing

Where feasible, discovery should perturb a source in harmless ways and check whether a rule remains valid:

- shift a heading horizontally;
- widen a money value with a trailing minus;
- add or remove page furniture;
- change page count digit width;
- alter description length;
- place a page break between logical records.

Perturbations are never treated as real training examples, but they provide evidence that a candidate relies on the intended invariant.

### 8.2 Holdout validation

With a sufficiently large corpus, reserve one or more reports from inference and use them only for validation. Report training and holdout scores separately.

## 9. Public API proposal

The exact API must follow established gBASIC conventions. A preliminary surface is:

```basic
' Analyze one report without producing a final spec.
profile = ari_discover.profile(report_text, options)

' Infer from an array of report records.
proposal = ari_discover.infer(sources, options)

' Validate an existing or generated spec against a corpus.
validation = ari_discover.validate(sources, spec_text, options)

' Revise a candidate using validation evidence and optional decisions.
revised = ari_discover.refine(sources, proposal, decisions, options)

' Render a human-readable analysis report.
text = ari_discover.explain(proposal, options)
```

> **Corrected, and this was the costliest premise in the draft.** gBASIC
> functions are **no longer arity-strict**: literal default parameter values
> shipped as PLAT-OPTPARAM. Measured —
>
> ```basic
> function f(a, b = 10)
>     return a + b
> end function
> ' f(1) is 11; f(1, 2) is 3
> ```
>
> So the `profile_default` / `infer_default` / `validate_default` twins above
> are **struck**. They would have permanently doubled the public surface to work
> around a limitation that no longer exists, and every caller would have had to
> know which of two names to reach for.
>
> This is not a hypothetical cost. `docs/finance_design.md` §6 made exactly this
> trade — it settled on a two-form API to work around the missing feature — and
> was **superseded one day later** when default parameters were built. That
> section is still in the tree marked superseded, as a record: had Phase 1
> shipped first, its first deliverable would have been a workaround for a
> limitation that then disappeared.
>
> **Defaults must be literals**, which is the whole of the feature and is
> exactly right here: an options record cannot be a default value, so the
> signature is `options = nothing` and the function substitutes
> `default_options()` when it sees `nothing`. That also gives the caller a way
> to ask what the defaults *are*, which a buried literal does not.
>
> ```basic
> function profile(report_text, options = nothing)
> function infer(sources, options = nothing)
> function validate(sources, spec_text, options = nothing)
> function refine(sources, proposal, decisions, options = nothing)
> function explain(proposal, options = nothing)
>
> function default_options()      ' the record §5.2 shows, as a value
> ```
>
> Verified to work as written:
>
> ```basic
> function profile(text, options = nothing)
>     o = options
>     if is_nothing(o) then
>         o = default_options()
>     end if
>     ...
> ' profile("rpt")                            -> uses 0.80
> ' profile("rpt", { minimum_support: 0.95 }) -> uses 0.95
> ```
>
> Note `is_nothing`, not `is_unknown`. They are different values and the
> distinction is load-bearing here — see §5.2.
>
> **A note on names.** `validate` collides with `finio.validate` and `explain`
> with `discovery.explain`. Both are benign and neither needs renaming: since
> the 2026 scope change an unqualified call reaches only the library whose code
> is running plus the root program's own functions, so a cross-library call must
> be qualified and cannot resolve by load order. `library_collisions()` will
> report both, which is expected — stdlib already carries seven such shared
> names (`at`, `create`, `merge`, `offline`, `select`, `series`,
> `with_transport`) and a program loading `dates` and `frame` legitimately
> reports `select`.

### 9.1 Source record

```basic
source = {
    id: "branch-17-2026-08-31",
    text: report_text,
    metadata: { report_date: "2026-08-31", branch: "17" }
}
```

Metadata informs comparison and diagnostics but is not assumed to occur in the report.

### 9.2 Proposal record

```basic
proposal = {
    spec: spec_text,
    confidence: 0.88,
    scorecard: scorecard,
    profile: profile,
    assumptions: assumptions,
    questions: questions,
    alternatives: alternatives,
    diagnostics: diagnostics,
    provenance: provenance
}
```

## 10. Human review model

Human review should resolve meaningful uncertainty rather than repair opaque output. Questions should be concrete and evidence-backed, for example:

```text
The eight-digit value following "RUN DATE" is compatible with both a date
and an identifier. It parses as a valid YYYYMMDD date in 14 of 14 sources.
Treat it as:
  A. date (recommended, confidence 0.94)
  B. identifier
  C. ignore
```

Review decisions become explicit constraints for the next refinement pass. They should be serializable so inference can be reproduced.

Useful decisions include:

- accept or reject a row family;
- choose a type;
- rename a field;
- mark a line as page furniture;
- establish a section relationship;
- prefer or forbid positional extraction;
- declare two apparent report forms to be distinct variants.

## 11. Optional LLM assistance

LLM analysis is an optional advisory stage. The deterministic engine first produces a bounded evidence package; the provider may then suggest interpretations.

Appropriate LLM tasks include:

- proposing readable field and section names;
- interpreting abbreviations and headings;
- grouping semantically related row families;
- explaining competing hypotheses;
- proposing an ARI rule from already identified anchors and spans;
- identifying likely domain concepts for human confirmation.

The LLM must not be authoritative for:

- byte offsets or spans;
- whether a rule actually parses the corpus;
- coverage and collision measurements;
- final type conversion;
- acceptance of a candidate specification.

Every LLM suggestion must be translated into a deterministic candidate, executed by ARI, and scored. Unsupported suggestions are discarded or presented as unresolved advice.

### 11.1 Provider boundary

`ari_discover` should not hard-code a model vendor. It accepts a function or adapter with a small contract conceptually equivalent to:

```basic
response = advisor.analyze(evidence_package)
```

The evidence package should be minimized and may contain redacted examples. External transmission must be explicitly enabled by the caller. Provider, model, prompt version, and response hash should be recorded in provenance.

### 11.2 Privacy modes

- `off`: no LLM use.
- `local`: only a caller-provided local model adapter.
- `redacted`: external model receives structural signatures and selected masked samples.
- `full`: external model may receive selected report excerpts after explicit authorization.

## 12. Explainability and provenance

Every generated rule must be traceable to evidence. A rule explanation should answer:

- Which sources and lines support it?
- Which literal or typed anchors does it use?
- How often did it succeed?
- Where did it collide or fail?
- Which alternatives were considered?
- Was any part proposed by an LLM?
- Does it depend on a fixed column?

ARI Discover should be able to emit both a machine-readable proposal and a human-readable report. Examples may be masked, but source identifiers and positions remain available unless the caller requests redaction.

## 13. Version and variant handling

Some corpora contain multiple legitimate layouts. Discovery should not force these into one brittle specification.

If clustering shows stable, materially different report grammars, return variants:

```text
variant A: observed in 18 sources; anchor "TELLER TOTALS"
variant B: observed in 6 sources; anchor "BRANCH TOTALS"
```

The result may recommend:

- one specification with alternate sections;
- multiple specifications with a deterministic selector;
- or additional samples because the distinction is not yet reliable.

Variant detection should integrate later with ARI or FinIO-style format detection, but that integration is outside the first implementation.

## 14. Failure behavior

Discovery must fail visibly and diagnostically. Important outcomes include:

- insufficient variation to distinguish constants from variables;
- no stable anchors;
- excessive row-family fragmentation;
- incompatible formats mixed in one corpus;
- candidate rules with unacceptable collision rates;
- unsupported encoding or damaged input;
- LLM unavailable, refused, or malformed response.

LLM failure never prevents deterministic discovery. When evidence is insufficient, return a partial profile and questions rather than fabricated certainty.

## 15. Testing strategy

> ### A rule about changing `ari`
>
> **Added 2026-09-16, after the first change to `ari` that this project
> prompted.** Discovery is being built against a corpus this project generated.
> That creates a hazard with a name: every time discovery meets a form `ari`
> cannot parse, the cheapest response is to change `ari` — and after enough of
> those, the parser is shaped to one invented corpus rather than to the
> population of real reports. **Teaching to the test.**
>
> So:
>
> 1. A deficiency found while building discovery is **recorded** in
>    [ari_limitations.md](ari_limitations.md), not fixed on the spot.
> 2. A change to `ari` needs evidence from **outside** the discovery corpus —
>    the format literature, a real report, or `ari`'s own hand-authored
>    fixtures. "Our corpus needs it" is not evidence, because we wrote the
>    corpus.
> 3. Any capability that is added is tested **where `ari` is tested**, against
>    `examples/fixtures/ari/`, not only in `run_ari_discover.sh`.
> 4. The sweep happens **at the end and in one pass**, against the whole
>    register, so the result generalises rather than accumulating the order in
>    which discovery happened to trip.
> 5. When discovery meets a form `ari` cannot read, **it reports it as
>    unrecognised.** That is the honest profile, and it is information a person
>    can act on — a profile that silently agreed with a parser that would fail
>    later is worse than one that says so now.
>
> The register is executable: every entry carries a probe in `run_ari.sh` that
> asserts the limitation *still holds*, so a fixed-but-still-recorded entry goes
> red. That rule already earned itself once — measured over `DOGFOOD.md`, five
> of fourteen entries were false.


### 15.1 The corpus, and why the existing ARI fixtures are not it

> **Corrected — the draft's starting corpus cannot measure what Phase 0
> measures.** `examples/fixtures/ari/` holds **three files, two of which are the
> same report**. §5.1 says variation across files is what separates a true
> constant from an accidental one, and three sources cannot supply it: at that
> size `minimum_support: 0.80` means "3 of 3", so 0.80 and 0.95 are the same
> threshold, and §8.2's holdout would leave two sources for inference.
>
> Starting there would test Phase 0 against data that cannot distinguish the
> thing Phase 0 exists to distinguish — which is the lesson `finio` delivered
> twice: a fixture written by the same author shares the author's
> misunderstandings, and it took a foreign corpus to find defects in three of
> four adapters that every green suite had missed.

**Built 2026-09-16: `examples/fixtures/ari_discover/`** — 24 branch-activity
reports across nine declared drift axes, plus `truth.json`. See that
directory's `MANIFEST.md`; the generator is
`tools/gen_discover_corpus.bas` and both are pure functions of their arguments.

**One declaration produces both the report and the truth.** `truth.json` is
written by the same program that writes the reports, from the values it
*planted* — never read back out of the text it printed. That is R1 of
`stdlib/estate.bas`, and the reason is that a hand-written answer key drifts the
first time either side changes: **a fixture whose answer key is wrong teaches
the tool to be wrong and then certifies it.**

The key carries what §8's scorecard needs to be *measured* rather than reported:

| field | makes measurable |
|---|---|
| `furniture_lines` | §7 Phase 3, as precision **and** recall by line number |
| `families` | §7 Phase 4 row-family recall, including a minority family absorbed into the dominant one |
| `branches[].accounts[]` | **the planted values** — the strong oracle §15.4 asks for. A coverage percentage cannot substitute: a specification can claim every line and extract the wrong number |
| `variant` | which of the nine axes a source exercises, so a failure is attributable rather than merely noticed |

Verified on generation: 808 detail rows, every planted account, name and amount
appearing exactly once in its own file, every branch total equal to the sum of
its accounts, every declared furniture line really furniture — 0 mismatches.

The three existing ARI fixtures keep a job and it is a different one: they are
the **irregularity** corpus, hand-authored to be inconsistent with themselves in
the ways a real report is. They belong in §15.3's adversarial set, not as the
known-answer corpus.

The expected result should focus first on structural equivalence and successful
extraction, not byte-identical generated formatting.

### 15.2 Synthetic report generator

Generate controlled report families varying:

- heading drift;
- field width and description length;
- negative-money notation;
- page breaks and page-number width;
- optional sections;
- missing values;
- continuation lines;
- reordered subsections;
- layout versions.

Because the generating schema is known, recall and false-positive rates can be measured exactly.

**Built: `tools/gen_discover_corpus.bas`.** Nine of those axes are implemented —
money notation, account-column heading, total label, date dialect, table indent,
name-column width, page length, form feeds versus header-only pagination, and an
optional `REMARKS:` section. Three are **not yet**: missing values, continuation
lines and reordered subsections. Each needs a decision about what the *truth*
records, which is why they were left rather than guessed:

- a **missing value** must be distinguishable in the key from a value the
  generator planted and discovery failed to find — Axiom 7's split one library
  over, and the direction that hurts is a gap read as zero;
- a **continuation line** belongs to the record above it, so the key has to say
  which, or a family count cannot be scored;
- a **reordered subsection** changes what "the same family" means across
  sources, which is the one axis that could make a family count ambiguous rather
  than merely harder.

Variants are chosen **by index, not at random**, so each axis is covered evenly.
A randomly drawn corpus can leave an axis with one sample or none, and a
discovery failure on that axis then looks like luck rather than like a finding.

### 15.3 Adversarial cases

Include:

- an anchor literal repeated inside data;
- dates that resemble IDs;
- money-like text in headings;
- totals formatted like detail rows;
- section headings at page boundaries;
- a single outlier report mixed into an otherwise consistent corpus;
- a trailing-minus value wider than neighboring positives;
- tabs, form feeds, CRLF, and non-ASCII descriptions.

**The non-ASCII case is required before Phase 1, not after**, and it is the only
one with that status. Every file in the generated corpus is ASCII, so the corpus
*cannot* catch the byte-versus-codepoint confusion §6.1 corrects — the two index
spaces coincide exactly until a multi-byte character appears. A source with an
accented description inside a column-aligned table is what separates a discovery
engine that measured in ARI's own space from one that measured in bytes and got
the right answer by luck.

The three existing fixtures in `examples/fixtures/ari/` belong here too. They
are hand-authored to be inconsistent with themselves — the same field spelled
`Teller #:` and `Teller#:`, summary fields in a different order per teller,
column headings shifted between tables, identifiers glued into prose columns,
some amounts malformed on purpose. That is an adversarial set, and a better one
than a generator produces, because a template cannot invent its own
inconsistencies.

### 15.4 Regression requirement

An accepted generated specification must parse the discovery corpus through the normal `ari` runtime. Tests should preserve both the generated proposal's scorecard and the parsed values that establish correctness.

------------------------------------------------------------------------

### 15.5 The null corpus

**Added by the 2026-09-16 revision, and it is the load-bearing tier.**

Inference is a search, and a search always returns a winner. This project has
measured that once already, at cost. `examples/automation_lab` recipe 1 ran the
same decomposition over a population with a real 45% collapse planted in a known
cell, and over one holding nothing but lognormal noise. **The output could not
tell them apart**: both produced a confident three-level causal chain, both
declined 1.8%, top-region share 82.6% against 80.3%. The finding was not that
the decomposition was buggy. It was that a drill-down *only ever pointed at data
with a known answer* always looks like it works.

ARI Discover is the same shape of machine. It proposes families, anchors and
field rules from a corpus and scores them. Any token that recurs is a candidate
anchor, and at `minimum_support: 0.80` on a small corpus, coincidences clear.
**A scorecard full of high numbers on a corpus that has structure is not
evidence that discovery found the structure** — it is equally consistent with a
tool that always finds something.

The only thing separating those two is running it where the right answer is
nothing.

**Built: `examples/fixtures/ari_discover/null/`** — 12 sources, generated by
`tools/gen_discover_null.bas`.

**What makes it a fair null** is the whole difficulty, and getting it wrong is
easy in the direction that flatters the tool. Random letters would measure
nothing: discovery would reject them for reasons — no money, no dates, no
report-like density — unrelated to structure. So the null is deliberately
indistinguishable from the real corpus **at the token level**: the same token
kinds from the same vocabularies, the same three money notations and two date
dialects, comparable line lengths, plausible indentation, blank lines at
irregular intervals. It differs in exactly one respect — token order, token
count and indentation are drawn per line, no literal recurs at a stable
position, and there is no page furniture, heading, total or repeating family.

Measured over the two corpora, on the features an inference engine keys on:

| | structured | null |
|---|---|---|
| non-blank lines | 1,564 | 679 |
| distinct structural signatures | **9** | **389** |
| share covered by the top 3 signatures | **73.8 %** | **6.5 %** |
| most common (indent, first token) pair | `BRANCH` at column 0, 169× (**10.8 %**) | 4× (**0.6 %**) |

Same tokens; structure absent by two orders of magnitude on exactly the measures
that matter.

**The expected result is a refusal, not a low score.** §14 already lists
"insufficient variation to distinguish constants from variables" and "no stable
anchors" as outcomes; this corpus is what turns them from a listed possibility
into a **measured false-positive rate**. That rate belongs in the scorecard as a
number, not as a box to tick — and §17 makes it an acceptance criterion.


## 16. Phased implementation

### Phase 0: Profiling foundation

- Source grid and provenance, carrying **both index spaces** (§6.1).
- Typed-span recognition.
- Line signatures.
- Repetition and page-furniture report.
- Human-readable profile; no spec generation.

> **BUILT 2026-09-16** — `stdlib/ari_discover.bas`, `tests/run_ari_discover.sh`,
> `examples/ari_discover_profile.bas`, `examples/ari_discover_infer.bas`.
>
> **Measured, against the corpus's own planted answer key**: furniture
> precision and recall are both **1.0** over the 20 multi-page sources (190
> lines planted, 190 claimed), and all 4 single-page sources **refuse with a
> reason** rather than guessing. Over the 12 structureless sources, **0**
> furniture lines are claimed.
>
> **Four things were wrong on the first working version, and every one produced
> a plausible profile rather than an error.** They are recorded in the source
> beside the rule that fixes each, because the rules are not obvious and a
> later reader will be tempted to simplify them back:
>
> 1. **Every word taken as a literal.** A member name is a word, so each detail
>    row became its own family — 39 families in a report with 8. Stability is
>    decided **by the group**, not by one line.
> 2. **A single recurring line shape taken as a page period.** It invented
>    pages in 14 of 18 single-page sources and claimed 18 furniture lines across
>    the null corpus. A header is a **block**, it starts at the **top of the
>    file**, and **page one defines it** — three separate constraints, each of
>    which was needed.
> 3. **Shape agreement without literal agreement.** A page break that fell just
>    before a section heading made page two open with the same eight *shapes* as
>    page one, so the whole window agreed: 17 lines claimed where 9 were
>    planted. §7 Phase 3's own words — "variable slots such as page number or
>    run date" — are the fix: a furniture line's **words** are stable.
> 4. **A period search bounded at half the document.** A 55-line report with a
>    50-line page has two pages; the second is a five-line tail. Bounding the
>    search at `n/2` found no period at all on most sources.
>
> **The null corpus earned its place immediately**: it is what caught defect 2's
> 18 false positives, and nothing else in the suite could have.
>
> **And building it found a gap in `ari` itself** — see §20 and the note in
> `ari.bas`: the engine could not read `DD-MMM-YYYY`, the classic mainframe
> print-image date, at all.

### Phase 1: Single-section inference

- Cluster one dominant repeating row family.
- Infer stable anchors and typed fields.
- Generate and execute a minimal candidate spec.
- Report coverage, collisions, unknowns, and unclaimed lines.

### Phase 2: Sections and pagination

- Page-furniture stripping proposals.
- Section/subsection boundaries.
- Detail, total, and continuation row families.
- Multi-source refinement and holdout validation.

### Phase 3: Interactive refinement

- Serializable human decisions.
- Alternatives and concrete questions.
- Rule-by-rule explanations.
- Variant detection.

### Phase 4: Optional LLM advisor

- Provider-neutral adapter.
- Privacy modes and evidence minimization.
- Naming and semantic interpretation.
- Deterministic validation of every suggestion.

## 17. Acceptance criteria for an initial useful release

The first useful release should demonstrate that it can:

1. profile the existing teller and delinquency examples;
2. identify page furniture separately from logical content;
3. find at least one repeating row family in each;
4. distinguish stable literals from typed variable spans across multiple samples;
5. generate an executable ARI candidate for the dominant row family;
6. validate the candidate using `ari.parse`;
7. report coverage, collisions, unknown values, assumptions, and unclaimed content;
8. work entirely without an LLM;
9. produce the same proposal from the same ordered corpus and options;
10. **propose no specification at or above the default confidence over
    `examples/fixtures/ari_discover/null/`, and report the false-positive rate
    it measured there as a number.**

> **Criterion 10 added by the 2026-09-16 revision, and criteria 1–9 are all
> satisfiable by a tool that always finds something.** Every one of them is
> scored on a corpus that *has* structure; none can distinguish a discovery
> engine from a confident guesser. §15.5 is the corpus that can, and the reason
> to believe it is necessary is that this project has already shipped the
> failure once — a decomposition that gave the same confident answer on a real
> planted effect and on pure noise, undetected until somebody ran the null.
>
> Stated as a **rate** rather than a pass/fail on purpose. "It refused all
> twelve" is a fact about twelve sources; the rate is what can be tracked as the
> engine changes and what a caller can weigh against `minimum_confidence`.
>
> **Criterion 9 (determinism) is achievable and has one requirement**: gBASIC's
> `keys()` returns **insertion order**, so any tally built in a deterministic
> order iterates deterministically. A map keyed by signature is fine; one keyed
> by an id drawn from a hash is not. See §5.3.

## 18. Design principles

1. **Measurement before interpretation.** Record what repeats, varies, and collides before naming what it means.
2. **Relative structure before columns.** Prefer relationships that survive drift.
3. **Types delimit as well as convert.** Preserve ARI's central insight that a recognizable type can define a variable-width field boundary.
4. **The runtime is the judge.** A proposed rule is not successful until ordinary ARI executes it against the corpus.
5. **Ambiguity is output.** Competing hypotheses and unanswered questions are legitimate results.
6. **Provenance is never optional.** Every finding maps back to source bytes and lines.
7. **LLMs advise; evidence decides.** Model output can expand hypotheses but cannot establish correctness.
8. **Generated specifications remain readable.** A person should be able to maintain the result without ARI Discover.

## 19. Recommended repository placement

```text
stdlib/ari.bas                         existing deterministic parser
stdlib/ari_discover.bas                discovery and inference library        [to build]
docs/ari_spec_language.md              existing ARI language reference
docs/ari_discover_design.md            this design                            [exists]
docs/ari_discover_reference.md         eventual public API/reference          [to write]
examples/ari_discover_profile.bas      profiling example                      [to build]
examples/ari_discover_infer.bas        multi-report inference example         [to build]
tests/run_ari_discover.sh              discovery test entry point             [to build]

examples/fixtures/ari_discover/        [BUILT 2026-09-16 -- see its MANIFEST.md]
  MANIFEST.md                          what these are, and that they are synthetic
  NN_branch_activity.rpt  x24          the structured corpus, nine drift axes
  truth.json                           the planted answer key
  null/null_NN_noise.rpt  x12          the null corpus (§15.5)
  null/truth.json                      "there is nothing here to find"
tools/gen_discover_corpus.bas          [BUILT] structured corpus + its truth
tools/gen_discover_null.bas            [BUILT] null corpus
```

Per-function documentation is **not optional**: `tests/run_stdlib_docs.sh`
requires every public function in `stdlib/*.bas` — every name not starting with
`_` — to appear in some document. There is no export list in gBASIC, so the
leading underscore is the entire privacy contract, and a helper that should not
be called is a function that should be renamed rather than an exception.

If the implementation becomes too large for one pure-gBASIC library, internal helpers may be split by responsibility, but `ari_discover` should remain the public facade.

## 20. Open decisions

- What exact subset of ARI syntax should the first generator emit?
- Should inference return only source text, or also a structured specification AST?
- ~~Which existing type recognizers can be shared directly rather than duplicated?~~
  **Answered by Phase 0.** The **pattern tables** are shared and the
  **functions** are not: `ari.money_patterns()` and `ari.date_patterns()` are
  public and `ari_discover` consumes them, while `_money_in` and `_date_in`
  stay private. That split is not a compromise — those two are built for
  *conversion* and throw their spans away (`_money_in` computes `beg`/`fin` and
  returns `best.val`; `_date_in` never has a position at all), whereas
  discovery needs *location* and no value. One table, two jobs, and a tripwire
  in `tests/run_ari_discover.sh` fails if discovery grows a money pattern of
  its own.
- Does ARI need a diagnostic parse mode that reports claimed and unclaimed spans?
- How should an accepted human decision file be represented and versioned?
- What minimum corpus size should trigger holdout validation automatically?
- Should fixed-column rules require explicit caller permission in all cases?
- Should generated specifications embed their discovery provenance or store it in a sidecar record?

The most consequential likely prerequisite is a diagnostic execution surface in `ari`: discovery benefits greatly if ARI can report which source spans each rule claimed, which lines remained unclaimed, and where candidate rules collided. That capability would improve both automatic inference and ordinary hand-written ARI debugging.

> **Corrected — it is half built, and the remaining half is smaller than this
> paragraph implies.** Two of the three things named already exist:
>
> - **`ari.inspect(report_text, spec_text)`** returns everything `parse` does
>   plus `findings` — the parse diagnostics grouped by reason, with a field
>   path and a remedial hint, instance indices collapsed so
>   `branches[0].opened` and `branches[1].opened` report as one field. It was
>   built for exactly this shape of problem: looking across instances to settle
>   a DD/MM column, done at authoring time so a guess never enters the parse
>   path.
> - **`ari.clean_grid(report_text, spec_text)`** exposes the page-furniture pass
>   on its own, independent of any spec — which is Phase 3's oracle, already
>   available.
>
> What is genuinely missing is only the **span level**: which source span each
> rule claimed, and what stayed unclaimed. That is one addition, it benefits
> hand-written ARI debugging as much as discovery, and it is the right thing to
> build first because §8's *content coverage* and *collision rate* cannot be
> measured without it — they would otherwise be estimated by the inference
> engine from its own model, which is the tool grading its own homework.
>
> The design note in `ari.bas` beside `_build_record` is worth reading before
> adding it: diagnostics are deliberately collected **out of band** rather than
> attached to the value, because gBASIC's `unknown` is a bare singleton with no
> payload and giving it one would change equality and serialization for every
> existing user. A span report should follow that precedent — a parallel
> structure, not a richer value.
