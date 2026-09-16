# ARI Discover: Automatic ARI Specification Inference

Status: Initial design draft  
Target library: `stdlib/ari_discover.bas`  
Companion runtime: `stdlib/ari.bas`  
Proposed public name: **ARI Discover**

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
load "ari.bas"
load "ari_discover.bas"

profile = ari_discover.profile(report_text, options)
proposal = ari_discover.infer(report_texts, options)
validation = ari_discover.validate(report_texts, proposal.spec, options)
```

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

## 6. Intermediate representation

Discovery should not operate directly on raw strings after ingestion. It should build an intermediate representation that preserves both content and provenance.

### 6.1 Source grid

Each physical line becomes a record containing at least:

```text
source_id, physical_line, page_guess, text, byte_start, byte_length,
indent, trimmed_length, blank, separator_score
```

No preprocessing step may destroy the mapping back to the original bytes.

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

The exact API must follow established gBASIC conventions and arity rules. A preliminary surface is:

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

Because gBASIC functions are arity-strict, convenience functions may be preferable to simulated optional arguments:

```basic
profile = ari_discover.profile_default(report_text)
proposal = ari_discover.infer_default(sources)
validation = ari_discover.validate_default(sources, spec_text)
```

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

### 15.1 Golden fixtures

Use existing ARI teller and delinquency fixtures as the initial known-answer corpus. The expected result should focus first on structural equivalence and successful extraction, not byte-identical generated formatting.

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

### 15.4 Regression requirement

An accepted generated specification must parse the discovery corpus through the normal `ari` runtime. Tests should preserve both the generated proposal's scorecard and the parsed values that establish correctness.

## 16. Phased implementation

### Phase 0: Profiling foundation

- Source grid and provenance.
- Typed-span recognition.
- Line signatures.
- Repetition and page-furniture report.
- Human-readable profile; no spec generation.

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
9. produce the same proposal from the same ordered corpus and options.

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
stdlib/ari_discover.bas                discovery and inference library
docs/ari_spec_language.md              existing ARI language reference
docs/ari_discover_design.md            this design
docs/ari_discover_reference.md         eventual public API/reference
examples/ari_discover_profile.bas      profiling example
examples/ari_discover_infer.bas        multi-report inference example
tests/run_ari_discover.sh              discovery test entry point
examples/fixtures/ari_discover/        multi-sample and synthetic corpora
```

If the implementation becomes too large for one pure-gBASIC library, internal helpers may be split by responsibility, but `ari_discover` should remain the public facade.

## 20. Open decisions

- What exact subset of ARI syntax should the first generator emit?
- Should inference return only source text, or also a structured specification AST?
- Which existing type recognizers can be shared directly rather than duplicated?
- Does ARI need a diagnostic parse mode that reports claimed and unclaimed spans?
- How should an accepted human decision file be represented and versioned?
- What minimum corpus size should trigger holdout validation automatically?
- Should fixed-column rules require explicit caller permission in all cases?
- Should generated specifications embed their discovery provenance or store it in a sidecar record?

The most consequential likely prerequisite is a diagnostic execution surface in `ari`: discovery benefits greatly if ARI can report which source spans each rule claimed, which lines remained unclaimed, and where candidate rules collided. That capability would improve both automatic inference and ordinary hand-written ARI debugging.
