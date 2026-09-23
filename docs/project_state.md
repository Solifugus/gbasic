# gBASIC Project State

Last updated: 2026-09-19 (0.2.2)

This file is the compact source of truth for current implementation status.
Detailed language behavior belongs in `docs/reference.md`; completed development
phases are summarized in `docs/historical_development_archive.md`; the full
document list, with a status column, is `docs/README.md`.

## Current Version

- Version: `0.2.2`
- Implementation: C11
- Front end: hand-written lexer and Bison parser
- Runtime: tree-walking evaluator
- Build entry point: `make`

## Implemented Language

- variables, strict expressions, assignment (including the compound forms
  `+=`, `-=`, `*=`, `/=`), input, and output
- multiline and inline `if`/`else`
- `consider`, `while`, the post-test `do ... until c`, and `break`/`continue` —
  each optionally naming the loop it means (`break x`, `continue x`)
- array iteration with `for each` and compatible `for ... in`; a counted `for`
  closing with `end for`, `next`, or `next <name>`
- arrays, records, dynamic record access, and nested lvalue assignment
  (records are copy-on-write; a keyword may be a field name, in a literal and
  after a dot)
- functions with literal **default parameter values** (`function f(a, b = 10)`),
  programs, libraries, labels, `goto`, and `gosub`
- `load`, `load NAME from "path"`, and `load ... as ALIAS`. `load` is a
  DECLARATION: written at the top level or inside the `program` block, it is
  registered before anything runs, by one pass the parent and a spawned actor
  share. The alias is the
  name the loading file qualifies by, and is a distinct import identity, so two
  libraries whose own declared name is the same can coexist. Defining a function
  twice in one scope is refused, and **a function from another library must be
  qualified**; a library still calls its own functions unqualified
- first-class function values (references) that can be stored, passed, and called
- Policy-Based Inheritance object model (`new`, `constructor`, methods via `this`)
- shared-nothing actors over `spawn`/`send`/`receive` with monitor/link
- assignment and comparison modifiers, written in braces (`p{USD} = 19.95`)
- watchers, locks, and a **frame-scoped error model** — `on error goto next`,
  `on error goto LABEL`, `if error then` (`docs/error_model_design.md`)
- a **warning channel** beside it — `on warning print|ignore|stop|goto next`,
  `if warning then`, `warning(…)` (`docs/warning_model_design.md`)
- comparison across kinds: equality **answers**, ordering **refuses** — a
  string is never equal to a number (`0 = "stop"` is false, `1 > "stop"`
  raises); numbers and booleans coerce (`0 = false`)
- distinct `nothing` and `unknown` values
- date/time, duration, money, file, and directory values
- binary-safe, Unicode-aware strings, codepoint operations, and byte builtins
- regular expressions as a **value kind**, overloading `contains`/`replace`/
  `split`, plus `match`/`match_all`
- bitwise builtins (`band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`)
- `mod` (floored), `concat`, `merge`
- three serializers with stated jobs: `json_encode`/`json_encodable` (strict RFC
  8259, for anything leaving gBASIC), `encode`/`decode` (+ non-raising
  `try_decode`) for gBASIC-to-gBASIC round trips, and `serialize`/`deserialize`
  for exact typed round trips
- `server` blocks — a routed HTTP server as a declarative block

## Implemented Runtime Areas

- core type, conversion, string, array, record, and counting helpers
- file read/write/append/overwrite and file metadata
- file copy/move/delete and deterministic directory listing
- non-recursive directory creation/removal, `atomic_replace`, `file_type`
- path manipulation helpers
- `read_lines`, `monotonic()`, `reflect.*`, `source_outline`
- process control — `process.run`, and `process.start`/`poll`/`read`/`wait`/
  `stop`/`release`/`which` for a live child. Every child is bound to the
  interpreter's lifetime by the kernel, so none survives it, even a `kill -9`
- optional synchronous SQLite module backed by sqlite3
- optional synchronous PostgreSQL module backed by libpq
- optional synchronous ODBC module backed by unixODBC — one connection string
  reaches SQL Server, MySQL, Oracle, DB2 and anything else with a driver
  installed; `bigint`/`decimal`/`numeric` answer as strings so a double cannot
  quietly eat the cents
- optional synchronous WebClient module backed by libcurl
- optional non-blocking `http` module backed by libcurl's multi interface — a
  request is a handle, and readiness is delivered by the same event loop that
  serves sockets, so a handler can start one and return
- a `timer` module: periodic work on the event loop, which nothing else could
  express. Ticks are coalesced rather than caught up, and what is dropped is
  reported; CLOCK_MONOTONIC, so an NTP step cannot stall or storm it
- a built-in WebServer: TLS, routing, streaming, hardening, and a process worker
  pool with listener transfer over `LISTEN_FDS`
- optional XML module backed by libxml2 (tree parse, navigation, encode,
  lenient HTML, constant-memory streaming reader)
- optional cryptography builtins backed by libcrypto (hashing, HMAC, AES-GCM,
  Ed25519, and the PBKDF2/scrypt key derivations that turn a passphrase into key
  bytes) plus a `crypto` stdlib library (JWT/HS256, signed cookies, CSRF)
- optional GTK 3 GUI proof of concept through Stage 6A, and a generic
  GObject-Introspection bridge (`gi`, libgirepository) with GTK 4 as the first
  target — what gBASIC Studio is built on
- diagnostics: `--json-diagnostics` emits one JSON object per diagnostic and
  nothing else, and any reported diagnostic is a nonzero exit

## Standard-Library Toolkits (pure gBASIC)

- **statistics** (`stats`) — descriptive/inferential statistics, regression and the GLM
  suite, mediation/moderation, time-series and econometric diagnostics, finance
  metrics, survival analysis (Kaplan-Meier, log-rank, Cox), meta-analysis,
  exploratory factor analysis, event studies, and causal inference (DiD,
  IV/2SLS); verified against reference implementations, published trial results,
  or — where a method can be right in the estimate and wrong in the uncertainty
  — against a second independent derivation rather than a golden.
- **the time value of money** — `finance`: payment, present value, future
  value and term for loans and leases; net present value and internal rate of
  return for project appraisal; an amortization schedule whose payments sum to
  the principal exactly; and straight-line, sum-of-years and declining-balance
  depreciation. Amounts are money values and rates are plain numbers, per
  PERIOD rather than per year, so the compounding convention stays the
  caller's to state.
- **market data** — `market`, daily price history as a frame, with pluggable
  providers and an offline fixture seam so tests never touch the network.
- **spreadsheets** — `xlsx` (read/write plus a formula engine measured against
  15,871 real workbooks; `xlsx.try_open` reports a bad workbook as a value so a
  batch survives one), `grid`, `frame`/`dbframe`, `consolidate`, `chart`.
- **test data** — `fake`, fabricated but realistic populations: pure functions
  of (seed, index) so generation is order-independent and reproducible across
  machines, with lognormal amounts, business-day dates, and referential and
  arithmetic consistency strong enough to post to a ledger; plus `fake.plant`,
  which puts a known defect in a known place and reports which rows it took
  without marking them.
- **deposits** — `deposits`, the other side of the book: declared balance
  method, compounding kept separate from crediting, tiered rates, and
  certificates whose early-withdrawal penalty may reduce principal. Worked
  recipes: docs/deposits_cookbook.md.
- **directory** — `ldap`, a native module for bind and search against LDAP(S):
  the identity tier an intranet application needs. the security mode is required with
  no default, referral chasing is off and not configurable, and bind reports
  failure as a value so *wrong password* and *directory unreachable* can never
  be the same answer. The gBASIC layer above it is the application's, because
  which attribute holds groups is policy. Design: docs/ldap_design.md.
- **business automation reasoning** — `reasoning` (the shared value model) and
  `insight` (observes and reasons), the first increment of
  docs/automation_reasoning_design.md. `insight.explain_change` decomposes a
  change across dimensions and says whether any of it means anything: the
  reference distribution is the sibling cells, the significance cut is a
  family-wise quantile derived from how many cells were searched, and a
  contribution share is withheld when the net change is not distinguishable
  from zero. A Finding refuses to carry a cause, a materiality or an assurance.
  `decision` (evaluates and chooses; never executes) scores every alternative,
  computes materiality from the context, states the authority a recommendation
  needs without enforcing it, and reports assurance as a sensitivity sweep with
  the crossing named. It refuses to size a decision off a quantity the finding
  declined to establish. `insight.weigh` reaches the third rung of the causal
  ladder without ever reaching the fifth: a hypothesis states which cells it
  predicts and the observation that would discriminate it, is scored by set
  agreement rather than a probability, and two that predict identically are
  reported tied rather than ranked. `automation` is the only layer that changes external
  state: it enforces the authority `decision` merely states, refuses to act at
  all until the process has been replayed against history, and shares one gate
  between its dry run and its live path so a rehearsal describes the program
  that will actually run. An action's outcome must be measured before it may be
  cited as evidence — and, since an action is taken precisely when a measure is
  extreme, only a measurement against a deliberately held-back comparison
  counts as evidence that acting *worked*. `decision.calibrate` closes the
  cycle by turning that evidence into the assumption behind the next decision,
  as an interval rather than a number, so *enough evidence* is answered
  relative to the decision at hand rather than by a sample-size rule.
  `decision.quantity` is the layer's second shape, for answers that are a
  continuous quantity produced by a model rather than a choice from a list: it
  carries the parameter's interval through the model, refuses to recommend
  anything when that interval reaches a value at which the model is undefined,
  and reports how hard the model magnifies uncertainty.
- **permission-filtered retrieval** — `retrieval`, where the ACL predicate and
  the nearest-neighbour ordering are one pgvector query. Ranking first and
  filtering after returns an empty list to a narrowly permitted user whose own
  best matches were never looked at, and an empty list reads as "nothing
  matched".
- **a fabricated business estate with its truth written down** — `estate`,
  which exists so discovery can be asked "did you find the right thing" AND
  "did you invent one where there is none". One declaration produces both the
  database and the answer key, so they cannot drift. Real column vocabulary
  from the Enron corpus rather than invented names.
- **what a database estate says about itself** — `discovery`, reading declared
  facts through the ODBC driver manager: tables, columns, types, nullability,
  primary keys and foreign keys, across several databases at once. It infers
  NOTHING, deliberately — an inferred relationship is the result of a search,
  and a search over a 500-table estate is ~50 million candidate pairs where
  coincidences are a certainty. Every identity rule was measured against four
  drivers: the qualifier is in `TABLE_CAT` on MariaDB, `TABLE_SCHEM` on
  PostgreSQL and SQL Server and neither on SQLite, so an id built as
  `schema.table` silently loses one. It also reads SQL: what a module touches,
  why two same-named columns disagree, and **where a column came from across
  hops** — a report's number traced back through views and procedures to the
  operational table it originated in. The object level answers "which table
  does this read", which rarely settles anything; the column level answers the
  one that costs a day. Worked recipes in `discovery_cookbook.md`, none of
  which needs a database.
- **a question over an estate** — `nlq`, first increment: grounding, the
  refusals, and three pure steps an application drives — plan, interpret,
  settle — none of which performs I/O. Scored against estateforge's
  independently computed key — no model, no database, so it is a gate: **18 of
  19** on a 127-object estate and **16 of 19** on a 517-object one. End to end,
  a real 4B model's SQL against a live estate: 7 of 10 on PostgreSQL, 8 of 12 on
  SQLite. Everything it will not invent is declared — synonyms, value
  vocabulary, format exemplars, derivation, and the terms an estate does not
  model — written once on the catalog through `discovery.annotate`, which is
  also where writing the cookbook found that the two libraries' catalog shapes
  did not meet at all (`nlq.from_discovery` is the bridge, and preserving the id
  exactly is the whole difficulty). Worked recipes in `nlq_cookbook.md`, none of
  which calls a model or touches a database. See nlq_design.md.
- **PDF documents** — `gpdf` with its generated metric table `gpdf_metrics`.
  Phase 1: the document, the core-14 fonts, text measured against the
  published Adobe widths, and word wrap that measures rather than counting
  characters. Written clean from ISO 32000 rather than ported, so the licence
  is ours to set. Output is byte-deterministic (no clock; an undated document
  carries no CreationDate), and an unrepresentable character is refused by
  name rather than substituted, because a wrong customer name on a posted
  invoice is worse than a refusal. THE ORACLE IS NOT US: four documents are
  handed to mupdf, ghostscript and poppler on every run. That oracle earned
  its place before a line existed -- pointed at the Node library that inspired
  the shape, it found `startxref 753` for an xref beginning at byte 799, plus
  a bad flate checksum, on freshly generated output. Still to come: tables
  that flow across pages, chart SVG as PDF vectors, images, and embedded fonts
  for Unicode.
- **maintenance for the adapters** — `finio_watch`: watch sources, the four
  findings (unreachable is its own), §9's ObservationLog with counting, derivable
  staleness, and a review queue combining both halves. DETECTS, NEVER UPDATES
  (Axiom 4), enforced by a source tripwire; no I/O, so it tests without network.
- **one list of every finio adapter** — `finio_all`, after the hand-built list
  was missed three times in one day; a tripwire counts it against the adapter
  libraries on disk.
- **a registry of financial formats** — `finio_registry`, the §9 research and
  acquisition queue. First tranche 2026-09-15: 10 formats, 6 families, against
  §14's 22 candidate domains, with the gap reported as a value. The open/licensed
  split is asserted by a suite, and both implemented entries were corrected
  against retrieved evidence -- camt DOWNGRADED from spec_obtained to discovered,
  because no schema was ever downloaded.
- **financial-format adapters** — `finio` (framework), `finio_nacha` (ACH,
  fixed-width) `finio_camt` (ISO 20022 camt.053, hierarchical XML) `finio_bai2` (BAI2,
  delimited and variable-length, with continuation records) and `finio_ofx`
  (OFX, SGML-like 1.x and XML 2.x -- schema drift inside one format) and
  `finio_pain001` (ISO 20022 pain.001, THE FIRST FOR A FILE YOU SEND, which is
  what put weight on Sec 16's write classification); `finio_iso20022` holds the
  mechanics camt and pain.001 share. The
  second adapter is a different REPRESENTATION on purpose (§20), and it is what
  turned a location from a byte range into a value with a kind. Provenance is reconstructed from a
  retained source because the alternative was measured at 2.79 GB for a 9.5 MB
  file; see financial_adapters_design.md §21. Phase 1 corrected five things
  invisible by reading, the sharpest being that Phase 0's byte offsets were
  codepoints and its own ASCII fixture could not tell. **Documented
  2026-09-16**: `docs/finio_tutorial.md` (one continuous problem -- a
  counterparty's drop directory scanned, read, validated, counted, and one file
  sent back with a field corrected) and `docs/finio_cookbook.md` (10 task
  recipes), both on the cannot-lie harness. The tutorial is the FIRST in this
  tree whose code blocks are verified at all -- `gui_tutorial.md` and
  `edgar_tutorial.md` are checked by nothing -- which took three path overrides
  in `tests/cookbook_harness.sh` rather than a second harness. Writing the
  cookbook CORRECTED a claim it was itself making: recipe 5 said a reader that
  assumed newlines would report one record and zero cents for a blocked file,
  and perturbing the library shows it produces a REFUSAL instead, because
  framing is decided once and recognition and reading go through that one
  decision.
- **benchmark and reference rates** — `finio_rates`, in the finio family
  because provenance is the subject: a bank that priced a loan off SOFR must be
  able to say years later which published value it used. Four KEYLESS sources
  (NY Fed SOFR/EFFR/OBFR, Treasury FiscalData). A rate is the publisher's own
  decimal TEXT with the number beside it -- measured, Treasury sends a string
  and the NY Fed sends a float, and one recorded value is 3.490 which a double
  renders 3.49. the the revision field field is three-valued. Replayed from committed recordings,
  so the gate never reaches a central bank.
- **text out of an image** — `ocr`, pure gBASIC over the tesseract CLI (a
  native module would be absent from the lean download). Words with boxes and
  confidence, never a bare string; orientation settled first and how
  confidently reported; a missing language refused BY NAME, because reading a
  script with the wrong data returns words at 30-45% confidence rather than an
  error. `ocr.grid` output is a print-image report, so `ari` and `ari_discover`
  consume it unchanged.
- **an optional LLM advisor for report discovery** — `ari_advisor`
  (`ari_discover` Phase 4). Proposes better field names from a bounded,
  masked evidence package built from the PROPOSAL rather than the report, and
  **never adopts one**: a field called posted and the same field called
  posted_date parse a corpus identically, so nothing can score a name and a
  person decides. Separate from
  `ari_discover` so that library keeps no HTTP dependency. Refuses to advise a
  proposal the engine refused.
- **an ARI limitations register, deliberately NOT a set of fixes** —
  `docs/ari_limitations.md` plus executable probes in `tests/run_ari.sh`.
  Matthew's call, and it is a methodology decision rather than a feature.
  THE HAZARD IS NOT THAT THE CORPUS IS GENERATED -- a first draft said it was
  and that was CORRECTED, because it points at the wrong remedy. Generating it
  is what buys an answer key that cannot drift, nine axes covered EVENLY rather
  than however a sample fell, one-variable-at-a-time attribution, and a NULL
  CORPUS obtainable no other way, which is the tier that caught 18 false
  positives nothing else could. The hazard is THE FEEDBACK LOOP: a corpus is a
  good MEASUREMENT INSTRUMENT and a poor REQUIREMENTS SOURCE, and when one
  corpus does both, the engine converges on the corpus rather than the world --
  and since we wrote it, that looks like progress. The rule is record now,
  require evidence from OUTSIDE the corpus to DECIDE, test any addition where
  `ari` is tested, and sweep generally at the end. A real corpus does the one
  thing generation cannot -- contain what nobody thought of, which finio
  measured at three defects in four adapters while every suite was green -- so
  the two answer DIFFERENT QUESTIONS and neither replaces the other.
  MEASURED rather than guessed, 14 entries split by what matters. CLASS A IS
  SILENT WRONG ANSWERS and dwarfs the rest: `1.234,56` (European grouping) reads
  as **1.23**, a thousandfold error with no diagnostic; three decimals are
  truncated to two; a `DR` suffix reads POSITIVE while `CR` reads negative, so
  a DR/CR report gets HALF ITS SIGNS RIGHT, which is worse than none. Class B
  is honest misses, which correctly answer with the unknown value and a
  diagnostic rather than a guess; class C is capability gaps.
  EVERY ENTRY IS A NEGATIVE CONTROL that goes red WHEN THE LIMITATION IS FIXED
  (the run_limitations.sh pattern, for the reason measured there -- five of
  fourteen DOGFOOD entries were false), plus a coverage tripwire on an entry
  with no probe. Four perturbations proven red, and one of them TAUGHT
  SOMETHING: adding the two-digit-year pattern alone does not produce a wrong
  date, it produces a RAISE that ends the parse -- so a class-B entry is a
  question to answer, never a regex to paste. RECORDED HONESTLY: the
  DD-MMM-YYYY addition was prompted by the corpus, is sanctioned by
  text_design 5.1's "union of common forms" independent of it, and its
  SELECTION was corpus-led, which is the part to be careful about.
- **ARI Discover Phase 2 COMPLETE** — multi-source refinement and holdout
  validation close what the section work opened, and EACH HALF IS A MEASURED
  DIFFERENCE. Built from one source: 0 of 8 sources get the right section count.
  Refined from the corpus: 8 of 8. Branch totals go 19/37 to 37/37. THE
  PAGINATION DIRECTIVE is the sharper one -- `break: formfeed` describes the
  source the rules came from AND NO OTHER, because the form feed is in half the
  corpus while THE HEADER LINE IS IN ALL OF IT; the rule is not "prefer regex"
  but prefer the directive every source supports. THE LABEL ALTERNATION is
  Sec 13 reached from evidence: the corpus says BRANCH TOTAL and TOTAL FOR
  BRANCH for one concept, `ari` takes a regex in a locator, so one spec carries
  both rather than two specs or a lost field. BOTH PATTERNS JOIN WORDS WITH
  `[ ]+` -- a print-image label is column-aligned and a signature normalises the
  gaps away, so joined with single spaces the break matched NOTHING, silently.
  And A GAP OF FURNITURE DOES NOT BREAK A RUN: a section straddling a page
  boundary is one section, and getting that wrong made region coverage report
  `wanted 8 found 7` where SEVEN WAS RIGHT. RESULT: one generated specification
  recovers 121/121 branch numbers, totals and row counts across all 24 sources,
  including the 16 inference never read; `holdout: n` makes that explicit,
  reserved from the END rather than at random so the proposal stays reproducible
  from an ordered corpus. Four more perturbations proven red.
- **ARI Discover Phase 2 (sections)** — sections, nesting and the furniture directive.
  THE HEADLINE IS A DIFFERENCE BETWEEN TWO MEASURES OF ONE RUN: source coverage
  1.0 against REGION COVERAGE 0.125 -- a nested specification that parses every
  source without error and finds the WRONG NUMBER OF SECTIONS in seven of eight
  (4 where there are 3, 10 where there are 7). Three things in it are
  source-specific and NONE MAKES A PARSE FAIL: `break: formfeed` does nothing on
  a source paginated by a header line, so the page header matches the section
  pattern and becomes a section; the total's label differs; the columns differ.
  The CONTROL is that on the source it was built from the section count is
  EXACT, without which the finding would be about incompetence rather than
  heterogeneity. Sections are found BY POSITION relative to the detail runs, not
  by vocabulary (keying on the word TOTAL works here and on no report that says
  SUMMARY), and candidates are grouped by LITERAL PREFIX not by family --
  keyed by family no heading was found at all, since two-word branch names form
  a different family and eight headings split across three. TWO RULES ARE
  RECORDED AS UNPROVEN in the source and the suite: removing BOTH the
  must-vary rule and the outermost-indent preference changes no answer here,
  because the branch heading happens to precede the column caption and insertion
  order then picks it; separating them needs a caption-first report or two
  levels of nesting, which the delinquency fixture has and this corpus does not.
  Kept rather than removed -- a rule with a reason is not dead code.
- **ARI Discover Phase 1** — `ari_discover` gains inference: a GENERATED specification,
  judged by `ari` itself (design principle 4 -- every number comes from running the
  candidate, never from the model that produced it). THE RESULT IS THE DESIGN'S
  OWN PRINCIPLE AS A NUMBER: over 8 sources carrying 230 planted rows, an
  anchor-relative field recovers 230/230 and a positional one recovers 147/230
  -- AND THE SECOND IS SILENT, since source_coverage is 1.0, unknown_rate is 0,
  and every value is an ordinary-looking account number from the wrong column.
  So positional rules stay OFF BY DEFAULT and what cannot be located becomes a
  QUESTION WITH ITS OPTIONS. THREE DEFECTS, each a plausible specification:
  columns inferred from the extent of observed VALUES rather than from GUTTERS
  (leading zeros gone, a name truncated to `YES, YUKI`); a scorecard that
  reported success while a third of the values were wrong, because nothing
  measured anchor stability (now computed WITHOUT the answer key, from whether
  the family's column structure is the same in every source -- 7 layouts across
  8); and THE NULL CORPUS CAUGHT PHASE 1 TOO, since inference proposed a spec for
  structureless text from a ONE-LINE family recurring in 11 of 12 sources by
  chance -- Sec 16 asks for a DOMINANT family and the first version required
  only recurrence (55% of content lines against 1.5%). Two of Sec 8's nine
  measures are reported UNKNOWN rather than estimated, since both need ari's
  absent span-level surface (limitation C1) and estimating them from the
  inference model would be the tool grading its own homework. Five perturbations
  proven red -- and the gutter rule needed ITS OWN adversarial source, because
  in the main corpus a space inside a value moves with the surname's length and
  the all-rows test rejects it for free.
- **ARI Discover Phase 0** — `ari_discover`: profiling for
  print-image reports, which PROPOSES NOTHING (no specification generation --
  a layer that also guessed would make the guess impossible to evaluate apart
  from the measurement under it). Furniture precision and recall are both 1.0
  against the planted answer key over 20 multi-page sources, and the 4
  single-page sources REFUSE WITH A REASON, since a header appearing once
  cannot be told from a first heading. FOUR DEFECTS, each of which produced a
  PLAUSIBLE PROFILE rather than an error: every word taken as a literal (39
  families in a report with 8); a single recurring line shape taken as a page
  period (invented pages in 14 of 18 single-page sources, and 18 furniture
  lines across the NULL corpus); shape agreement without literal agreement (a
  page break falling before a section heading made page two open with the same
  eight shapes, 17 claimed where 9 were planted); and a period search bounded
  at half the document, which cannot find a 50-line page in a 55-line report.
  THE NULL CORPUS CAUGHT THE SECOND AND NOTHING ELSE COULD. Building it also
  found that `ari` could not read DD-MMM-YYYY -- the classic mainframe date --
  at all, in a library whose whole subject is print-image reports; nothing in
  the tree used the format, so nothing could see it. The recognizer TABLES are
  now shared (`ari.money_patterns()`, `ari.date_patterns()`) rather than
  copied, which is Sec 20's open decision answered and a tripwire enforces it.
- **a corpus for ARI Discover, before the library** — `examples/fixtures/ari_discover/`
  (24 branch-activity reports across nine declared drift axes with a PLANTED
  answer key, plus a NULL corpus of 12 structureless sources) and the two
  generators that emit them. `docs/ari_discover_design.md` is revised to fit
  gBASIC as MEASURED rather than as described: four premises did not hold, the
  costliest being arity-strictness (default parameters shipped, so the proposed
  `*_default` twins would have permanently doubled the API -- the trade
  finance_design §6 made and struck a day later), and the subtlest being that
  the draft located in BYTES while `ari` locates in CODEPOINTS (it slices with
  the codepoint-indexed string builtins),
  a rule correct on every ASCII line and silently wrong on the first accented
  description, which is the defect finio Phase 0 shipped one library over and
  which THIS CORPUS CANNOT CATCH because every file in it is ASCII. The null
  corpus is the load-bearing addition: recipe 1 already measured that a search
  returns a winner whether or not there is anything there, so acceptance
  criteria 1-9 are all satisfiable by a confident guesser and criterion 10 is
  the only one that is not.
- **a textual form that keeps every gBASIC type** — `notation`, the gap between
  the JSON dialect (readable, and refuses a date, money, a duration or a file)
  and the binary serializer (keeps every type, opaque). Neither can be reviewed by
  eye, and a business record is mostly the values that dialect refuses. The type
  tag goes on the KEY side because that is already the language's own typed
  assignment with a colon where the equals goes; a tag on an array is a default
  its elements may override, cascading into nested arrays and stopping at a
  record. Version 1 discards comments, deliberately: preserving them is an API
  fork rather than a detail.
- **a second factor** — `otp`, one-time passwords (RFC 4226 HOTP, RFC 6238
  TOTP). gBASIC owns the factor rather than delegating it, so it works whether
  the directory is AD, OpenLDAP, a table or nothing. Replay is made structural:
  `otp.check` requires the last counter the account accepted and returns the one it
  matched, so a caller cannot reach it without saying what was last used. Rate
  limiting is the actual break and is the caller's, which the design says
  plainly rather than leaving to be inferred.
- **publishing tools to other agents** — `mcp`, the Model Context Protocol over
  stdio or HTTP through one dispatcher that does no I/O. A failed tool comes
  back as a result the model can react to; a malformed request comes back as a
  protocol error. A mapped principal needs a declared ceiling, because the
  mapping is what a published read-only tool answers as.
- **agent runs** — `agent`, where a conversation in progress is a value and the
  loop is a pure step returning the actions its caller must perform. The run is
  plain data, so it survives storage and an approval can arrive later on another
  request; a tool that declares mutations asks a person first.
- **model tool-calling** — `tools`, one declaration of what a model may call:
  the parameter list is the source both the published schema and the argument
  validation derive from, so what the model is told and what is enforced cannot
  drift. A raising tool returns a result rather than ending the run; declared
  effects are published but not gated; and bodies run in a pre-spawned worker
  pool so a tool call does not freeze the event loop.
- **credit scorecards** — `scoring`, turning a population into a model that
  ranks risk and then into the artefact a credit committee approves: binning by
  weight of evidence, information value, AUC/KS/Gini, calibration onto a point
  scale, and population stability. The WOE orientation is declared, an empty
  bin is refused rather than smoothed, and an AUC below 0.5 is reported as it
  stands rather than flipped. Design: docs/scoring_design.md.
- **credit analytics** — `credit`, questions about a book rather than a loan:
  vintage curves by months on book, roll rates, migration and charge-off, over
  a status table so it can be pointed at a real servicer extract. Attrition is
  a state in the matrix, not a hole, and the reconciliation that falls out of
  that is the invariant the suite asserts. Worked recipes:
  docs/credit_cookbook.md.
- **lending** — `lending`, loans and servicing over the finance and accounting
  libraries: declared accrual basis, payment waterfall and day count; servicing
  as a fold over events so a balance is always explainable by replay; payoff
  with per-diem; underwriting ratios; and journal entries emitted for the
  caller's own ledger. Worked recipes: docs/lending_cookbook.md.
- **accounting** — `accounting`, double-entry bookkeeping over the exact
  money type: a validated chart of accounts, journal entries that must balance
  in every currency or are refused where they are written, ledger, trial
  balance, balance sheet, income statement, and a period close that is refused
  rather than repeated.
- **EDGAR securities-analysis suite** — `edgar` (acquisition), `fundamentals`,
  `forensics` (accruals/Beneish/Piotroski/Altman/dilution/flags/events),
  `insiders` and `ownership` (Form 4 / 13F / 13D-G), `mdna` (MD&A + LLM panel),
  `llm` (chat client), and `screener` (whole-market scoring). See
  `docs/edgar_tutorial.md` and `docs/edgar_reference.md`.
- **application platform** — `web` (routing), `gtk`/`gtkui`/`datagrid`/
  `sourceeditor` (GTK 4 over `gi`), `filetree`, `persist` (crash-safe versioned
  storage), `dates`/`schedule` (business calendars, appointment slots), `ari`
  (anchor-relative report parsing), `matrix`, `crypto` (JWT, CSRF, signed
  cookies), `mail` (RFC 5322 composition, for the native SMTP transport), and
  the GTK 3 `gui` proof of concept.

Loadable names are exactly the filenames in `stdlib/`; `tests/run_docs_gate.sh`
checks this list against that directory, because it previously named two
libraries that do not exist (`sourceview`, `text`) and omitted four that do.

## Optional Dependencies

- GTK 3 enables the GUI implementation; libgirepository enables `gi` (GTK 4).
- sqlite3 enables `load sqlite`.
- libpq enables `load pg`.
- unixODBC (or iODBC) enables `load odbc`; the database driver itself is the
  operator's to install.
- libcurl enables `load webclient` and `load smtp`.
- libcurl enables `load webclient` (and the EDGAR/LLM network paths).
- libxml2 enables `load xml`.
- libcrypto enables the cryptographic builtins.
- WebServer uses POSIX sockets and has no external HTTP dependency; TLS uses
  libssl.

The interpreter builds without optional dependencies and reports unavailable
modules clearly at runtime.

## Verification

`tests/` holds the suites; each `run_*.sh` is self-contained, builds first, and
prints PASS/FAIL per case. `tests/run_all.sh` discovers every suite by glob and
is the gate; the useful thing to know is which to run while working rather than
a list that goes stale:

```sh
make clean && make
./tests/run_core.sh ./tests/run_examples.sh ./tests/run_negative.sh   # the floor
```

Everything else is topical — `run_error_model.sh`, `run_warning_model.sh`,
`run_parse_exit.sh`, `run_process_lifetime.sh`, `run_web_*.sh`, `run_xlsx*.sh`,
and so on. Suites needing a service or an optional dependency SKIP cleanly
rather than fail. `tests/run_docs_gate.sh` checks the documentation index and
the performance claims. GUI verification beyond `run_gui_parse.sh` is manual.

gBASIC Studio (`~/development/gbasic-studio`) is the largest dogfooding
consumer; its `tests/run_studio.sh` runs against whatever interpreter `GBASIC`
points at, so it doubles as an integration suite for this repository.

## Current Limitations

- tree-walking, non-optimized interpreter
- pre-1.0: evolving diagnostics and module APIs
- non-raising `try_*` twins (`try_decode`, `xlsx.try_open`, `process.which`,
  `has_builtin`) predate the frame-scoped error model and remain, now as an
  ergonomic choice rather than a necessity — they report *where* an input is
  malformed, which a caught raise does not
- SQLite is synchronous and has no prepared-statement API exposed to gBASIC
- PostgreSQL is synchronous and has no pooling or prepared-statement API
- WebClient is synchronous
- GUI (GTK 3) supports existing-widget synchronization but not dynamic tree
  mutation; the `gi`/GTK 4 path does not share that limit
- there is no dedicated map type; a record serves as one (hash-indexed since
  PLAT-RECIDX, so lookup is not linear, but the ergonomics are a record's)
- `finio_ofx` carries a private tag scanner, and **deliberately**: OFX 1.x omits
  closing tags, so converting it for `xml.parse` would invent text and a byte
  offset into an invented document is not provenance. Unrelated to the
  position gap closed 2026-09-20, which `finio_camt` now uses
- `DOGFOOD.md`'s "Open — worth fixing" list is **empty** as of 2026-09-20. It held five more until 2026-09-19 — a condition
  that could not be judged raised and then stepped into the `else`, `not` bound
  tighter than `=`, `round(x)` refused one argument, `--add-loads` had silently
  become a no-op, and `? x = 5` at the prompt silently ASSIGNED — all closed
  with suites, plus the prompt reporting a question's diagnostics against a
  line the author never typed (closed by `gb_parse_at`). The rest of that file
  is the "accepted as documented limitations" section, which is doctrine rather
  than a to-do list

## Current Documents

See `docs/README.md`. It lists every document with a **status** column
(Shipped / Proposal / Partial / Record) and `tests/run_docs_gate.sh` fails if a
document is missing from it — which is the protection this section used to lack:
a second, hand-maintained index here went thirteen documents and seven weeks out
of date without anything noticing.
