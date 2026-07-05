# EDGAR Suite — Development Plan for Claude Code

Audience: **Claude Code**, executing one work package (WP) per session, and
**Matthew**, who verifies. This plan governs implementation of three designs,
all in `~/development/gbasic/docs/`:

- `edgar_design.md` — EDGAR & securities analysis library (v2)
- `xml_design.md` — XML module (native, libxml2)
- `llm_design.md` — LLM client library (`llm.bas`)

The plan is **immutable during execution** (changes go through Matthew).
State lives in `docs/PROGRESS.md` (§3). Design documents are authoritative
for *what* to build; this plan is authoritative for *order, scope boundaries,
and evidence requirements*.

---

## 1. Session protocol (Claude Code: read this every session)

1. Matthew assigns exactly **one WP by ID**. Do not choose your own.
2. Read, in this order and **nothing else** to start:
   - this §1 and §2
   - your WP's section in §5
   - `docs/PROGRESS.md`
   - the WP's **Read** list (files and design-doc *sections* — read only the
     listed sections, not whole documents unless listed as whole)
3. If a dependency WP is `done-claimed` but not `verified`, say so before
   building on it; proceed only if Matthew confirms.
4. Implement only what the WP's **Build** list says. If the design seems
   wrong, ambiguous, or in conflict with the code you find: **stop, mark the
   WP `blocked` in PROGRESS.md with a precise question, and end the
   session.** Do not improvise design decisions.
5. Run the WP's **Done when** checks. Then update `PROGRESS.md` per §3 with
   a full evidence block.
6. **Stop at the WP boundary.** Do not begin the next WP, do not refactor
   neighboring code, do not "also fix" unrelated issues (note them in the
   evidence block's `observations` instead).

### Hard rules (all sessions)

- **Never** mark a WP `verified` — that status is Matthew's alone.
- **Never** weaken, delete, or skip an existing test to make a suite pass.
  A newly failing pre-existing test is a `blocked` condition, not a cleanup
  opportunity.
- **No network access in any test.** Tests consume checked-in fixtures only.
  Live smoke scripts exist but are run manually by Matthew, never in `make
  test`, never in a WP's Done-when.
- Identifiers (CIK, accession, CUSIP) are **strings**, everywhere, always.
- Missing/unavailable data is `unknown`, never a guessed value, per the
  standing NA policy.
- New C code must be **valgrind-clean** (0 errors; 0 definitely/indirectly
  lost) on its test suite before `done-claimed`.
- Follow the repo's golden-file convention (`.out` fixtures, exact match;
  floats rounded to the project display precision).
- Do not read `historical_development_archive.md` or unrelated module
  designs; they waste context.

---

## 2. Repo layout for this work

```
src/modules/xml.c              new native module (Track B)
stdlib/edgar.bas               Track A
stdlib/fundamentals.bas        Track A
stdlib/forensics.bas           Track A
stdlib/insiders.bas            Track C
stdlib/ownership.bas           Track C
stdlib/llm.bas                 Track E
stdlib/mdna.bas                Track F
stdlib/screener.bas            Track G
examples/edgar/*.bas           worked examples & demos
examples/fixtures/edgar/       recorded EDGAR JSON/XML/HTML (checked in)
examples/fixtures/llm/         recorded provider request/response pairs
tools/edgar_capture.sh         fixture capture (Matthew runs; needs network)
docs/PROGRESS.md               state (created by WP-0)
```

Build integration for `xml.c` follows the existing optional-module pattern
(sqlite/pg/gui): compiled when libxml2 is detected, clean runtime error
otherwise.

---

## 3. PROGRESS.md specification

Created by WP-0. One entry per WP, newest first within its track. Statuses:

```
todo | in-progress | blocked(<reason>) | done-claimed | verified
```

`verified` is written only by Matthew, after independently re-running the
decisive commands. Entry format:

```markdown
### WP-XML-2 — done-claimed — 2026-07-05
- commands run:
    make test-xml 2>&1 | tail -6
    valgrind --leak-check=full ./gbasic tests/xml/run_all.bas 2>&1 | tail -4
- output tail (verbatim, unedited):
    xml suite: 41 positive / 12 negative — ALL GREEN
    ERROR SUMMARY: 0 errors from 0 contexts
    definitely lost: 0 bytes ... indirectly lost: 0 bytes
- files added/changed: src/modules/xml.c, tests/xml/find_test.bas, ...
- deviations from plan: none
- observations: (anything noticed but out of scope)
```

The **output tail is verbatim terminal output**, never a paraphrase. A claim
without decisive values in the tail is treated as unsubstantiated.

---

## 4. Tracks, dependencies, and the fast path

| Track | Contents | Depends on |
|---|---|---|
| 0 | Bootstrap + tiny core builtins | — |
| A | `edgar.bas`, `fundamentals.bas`, `forensics.bas`, demo | Track 0 |
| B | `xml` module | Track 0 |
| C | `insiders.bas`, `ownership.bas` | A (edgar), B (xml ≤ WP-XML-5) |
| D | Events + watcher monitor | A, WP-CORE-1 |
| E | `llm.bas` | WP-CORE-1 |
| F | `mdna.bas` | B (WP-XML-7), E, A (forensics) |
| G | `screener.bas` | A |

**Fast path to the Adrian demo:** WP-0 → WP-EDG-1…6 → WP-FOR-1…4 →
WP-DEMO-1. No XML, no LLM. Track B can interleave anytime for variety;
Tracks C–G follow.

---

## 5. Work packages

Format per WP: **Goal / Depends / Read / Build / Done when / Notes.**
"Done when" items are the decisive values that must appear in the evidence
block. Where a WP says "record the counts," the exact numbers are fixed at
implementation time and become the regression baseline.

---

### Track 0 — Bootstrap

#### WP-0 — Plan bootstrap
- **Goal:** create tracking + scaffolding; no product code.
- **Depends:** —
- **Read:** this plan §1–§4 (whole); repo `CLAUDE.md` if present.
- **Build:** `docs/PROGRESS.md` with every WP in this plan listed as `todo`;
  the §2 directories (empty `.gitkeep`s); append to `CLAUDE.md`:

  ```markdown
  ## EDGAR suite work
  Governed by docs/edgar_suite_development_plan.md. One WP per session,
  assigned by Matthew. Follow its §1 session protocol exactly: read only
  the WP's Read list, stop at the WP boundary, record verbatim evidence in
  docs/PROGRESS.md. Never mark anything `verified`. No network in tests.
  ```
- **Done when:** files exist; `git status` tail recorded.

#### WP-CORE-1 — `sleep` and `env` builtins
- **Goal:** `sleep(seconds)` (fractional ok) and `env(name)` → string or
  `unknown` when unset.
- **Depends:** WP-0.
- **Read:** `edgar_design.md` §8 items 2–3; `llm_design.md` §2 (key
  sourcing); the builtin-registration site in `src/eval.c` (locate by
  grepping an existing builtin, e.g. `secure_token`); `reference.md`
  entries for two neighboring builtins as format models.
- **Build:** the two builtins; arity/type errors per existing conventions;
  tests (env set/unset via the test harness; sleep asserts elapsed ≥
  requested); `reference.md` entries.
- **Done when:** test tail green with counts recorded; valgrind-clean tail.
- **Notes:** if either builtin already exists, record that in PROGRESS and
  do only the missing one; if the name collides with the §5 modifier
  limitation (declared-function bias), mark `blocked` with the specifics.

---

### Track A — EDGAR numeric path (the fast path)

#### WP-EDG-1 — Fixture capture tooling *(network; Matthew executes)*
- **Goal:** scripts that record every fixture Track A needs. CC writes
  them; **Matthew runs them** (his User-Agent identity, his rate
  compliance); outputs are then checked in.
- **Depends:** WP-0.
- **Read:** `edgar_design.md` §2, §4.1; this WP only.
- **Build:** `tools/edgar_capture.sh` (curl-based, 1 req/sec, mandatory
  `--user-agent "$EDGAR_IDENT"`, fails loudly if unset) fetching, for
  CIKs of **AAPL, JPM, and one small-cap Matthew picks**:
  `company_tickers.json`, each `submissions/CIK{10}.json`, each
  `companyfacts/CIK{10}.json` — into `examples/fixtures/edgar/` with a
  `MANIFEST.md` (URL → file → capture date).
- **Done when:** script exists and passes `bash -n`; dry-run mode
  (`--print-only`) output recorded. Fixture presence is verified by
  Matthew after he runs it.
- **Notes:** JPM is deliberate — a bank stress-tests the concept map and
  the sector-caveat question (`edgar_design.md` §9.1).

#### WP-EDG-2 — `edgar.bas`: gate, identity, cache, offline seam
- **Goal:** the acquisition core with a test seam.
- **Depends:** WP-EDG-1 fixtures verified.
- **Read:** `edgar_design.md` §2, §4.1; `webclient_design.md` (request +
  header surface only); sqlite module usage from one existing example.
- **Build:** `edgar.identify(s)` (unset ⇒ any fetch raises);
  `edgar.offline(dir)` — all fetches resolve URL→fixture file, **miss ⇒
  structured error** (tests must never silently fall through to network);
  internal single fetch gate (rate limit ≥100ms spacing, UA header, sqlite
  cache at configurable path with immutable-document + TTL-index policy);
  `edgar.cik(ticker)`; `edgar.submissions(cik)` → filing-index frame.
- **Done when:** suite green over fixtures (counts recorded); a test proves
  unset-identify raises; a test proves offline-miss raises; cache-hit test
  (second call touches no transport — assert via a transport call counter).
- **Notes:** the offline seam is the spec; do not invent a different
  mocking mechanism.

#### WP-EDG-3 — `edgar.bas`: facts, documents, poll
- **Goal:** complete the acquisition surface.
- **Depends:** WP-EDG-2.
- **Read:** `edgar_design.md` §2, §4.1; your own WP-EDG-2 code.
- **Build:** `edgar.company_facts(cik)` (decoded record);
  `edgar.document(cik, accession, filename)`; `edgar.poll(watchlist,
  last_seen)` → new-filings frame (pure function over `submissions`, so
  offline-testable).
- **Done when:** suite green (counts); a poll golden built from a fixture
  with a chosen `last_seen` split.

#### WP-EDG-4 — `fundamentals.bas`: concept map + series
- **Goal:** tag fallback chains, period dedup, `as_filed`.
- **Depends:** WP-EDG-3.
- **Read:** `edgar_design.md` §4.2 (whole); the companyfacts fixture for
  AAPL (inspect structure directly — this is the ground truth).
- **Build:** the concept map as a data record (concept → ordered tag list)
  covering the §4.2 v2 list; `fundamentals.series(facts, concept)` →
  deduped fact frame (prefer latest accession per (end, fp)); `series(...,
  as_filed)` keeping originals; `unknown` row/result when no tag in the
  chain is present.
- **Done when:** goldens for ≥3 concepts × 3 filers (9 series) green;
  a dedup test where a period appears in 10-K and 10-Q fixtures proves
  latest-accession wins; an unmapped-concept test yields `unknown`.
- **Notes:** where JPM lacks an industrial tag, the correct output is
  `unknown` — encode that as a passing test, not a failure.

#### WP-EDG-5 — `fundamentals.bas`: derived metrics
- **Goal:** `fcf`, `debt`, `margins`, `ratios`.
- **Depends:** WP-EDG-4.
- **Read:** `edgar_design.md` §4.2; your WP-EDG-4 code.
- **Build:** the four functions per design; all outputs fact frames; NA
  propagation per policy.
- **Done when:** goldens green (counts recorded); **hand-check anchor:**
  the evidence block must list, for one AAPL fiscal year, the computed FCF
  and the two ingredient values, so Matthew can check them against the
  actual 10-K.
- **Notes:** decisive values here are dollar figures — Matthew verifies
  against the filing itself, not against the code.

#### WP-EDG-6 — `fundamentals.compare` + Track A hardening pass
- **Goal:** peer-comparison frame; close out fundamentals.
- **Depends:** WP-EDG-5.
- **Read:** `edgar_design.md` §4.2; frame `join`/`summarize` sections of
  `statistics_design.md` §4 only.
- **Build:** `compare(ciks, concepts)` → one row per company, latest
  period, `unknown` where absent; a worked example
  `examples/edgar/fundamentals_demo.bas` printing a small comparison table
  for the three fixture filers.
- **Done when:** suite green; demo output golden recorded.

#### WP-FOR-1 — `forensics.bas`: accruals + Piotroski
- **Goal:** the two simplest scores, establishing the scorecard-frame shape.
- **Depends:** WP-EDG-5.
- **Read:** `edgar_design.md` §4.5 (whole); your `fundamentals.bas`
  surface (signatures only — grep, don't read whole file).
- **Build:** `forensics.accruals(facts)` (Sloan ratio, average total
  assets denominator); `forensics.piotroski(facts)` returning all nine
  booleans + sum per period; shared scorecard-frame conventions
  (`period`, components, score, `unknown` handling).
- **Done when:** goldens green; **hand-check anchor:** one filer-year's
  nine Piotroski components listed in the evidence block with the
  ingredient values, for Matthew's independent check against the filing.
- **Notes:** exact published test definitions matter (e.g., "no new shares
  issued" compares shares outstanding YoY). Where the design or literature
  admits a variant, pick the standard form and record the choice under
  `deviations`.

#### WP-FOR-2 — `forensics.bas`: Beneish M-Score
- **Goal:** the eight indices + weighted composite + flag threshold.
- **Depends:** WP-FOR-1.
- **Read:** `edgar_design.md` §4.5; WP-FOR-1's scorecard conventions
  (your own code).
- **Build:** `forensics.beneish(facts)` returning DSRI, GMI, AQI, SGI,
  DEPI, SGAI, LVGI, TATA, `mscore`, `flag` per period, using the
  published coefficients and −1.78 threshold; `unknown` propagation per
  component.
- **Done when:** golden against a **published worked example** (Matthew
  supplies or approves the reference values before coding — if none is on
  hand, mark `blocked` requesting them); suite counts recorded.
- **Notes:** do not source coefficients from memory alone; they must match
  the reference example to golden precision.

#### WP-FOR-3 — `forensics.bas`: Altman + dilution
- **Goal:** distress score + quiet-dilution tracker.
- **Depends:** WP-FOR-1.
- **Read:** `edgar_design.md` §4.5, §4.7 (price contract).
- **Build:** `forensics.altman(facts)` (Z″, book equity) and
  `forensics.altman(facts, prices)` (classic Z; prices is the §4.7 frame);
  zone classification; `forensics.dilution(facts)` (shares trend, SBC,
  buybacks, net).
- **Done when:** Z″ golden against a hand-computed example (evidence block
  lists the component ratios + ingredients); a synthetic price-frame test
  exercises classic Z; dilution golden on AAPL fixtures (large buybacks —
  good signal-shape test).

#### WP-FOR-4 — `forensics.flags`
- **Goal:** the composite red-flag dossier frame.
- **Depends:** WP-FOR-2, WP-FOR-3, WP-EDG-3.
- **Read:** `edgar_design.md` §4.5 (flags item), §1 (form/item meanings).
- **Build:** `forensics.flags(facts, subs)` joining: NT filings, /A
  amendments, 8-K items 4.01/4.02, clustered 5.02s (window parameter),
  rising accruals, M-Score flag, positive-NI-negative-FCF. Each row:
  `kind`, `date`, `accession`/`period`, `detail`. Fixtures likely lack NT
  filings — add a **synthetic submissions fixture** exercising every flag
  kind.
- **Done when:** synthetic-fixture golden covers all flag kinds; real-filer
  run produces a (possibly empty) frame without error; counts recorded.

#### WP-DEMO-1 — The Adrian demo
- **Goal:** `examples/edgar/scorecard.bas` — ticker in, dossier out:
  fundamentals trends (FCF, margins, debt), full forensic scorecard,
  flags — cleanly printed.
- **Depends:** WP-FOR-4, WP-EDG-6.
- **Read:** the public surfaces you built (signatures via grep).
- **Build:** the example program; runs offline against fixtures by default,
  online with `edgar.identify` set.
- **Done when:** offline golden of full output recorded; program length and
  readability matter — this is a showpiece, comments welcome.

---

### Track B — XML module

#### WP-XML-1 — Scaffold + tree parse
- **Goal:** module skeleton, libxml2 detection, `xml.parse`/`parse_file`,
  node mapping, security defaults, structured errors.
- **Depends:** WP-0.
- **Read:** `xml_design.md` §1–§3, §7–§8; build-system integration of one
  existing optional module (sqlite) as the pattern.
- **Build:** `src/modules/xml.c`; §2 node records (name/qname/ns/attrs/
  children, coalesced text, whitespace-dropped default + `keep_space`);
  NONET / no-DTD-load / no-entity-expansion / depth-cap defaults; errors
  with line/column, source `xml`.
- **Done when:** parse goldens green incl. namespaces, CDATA, entities,
  not-well-formed error with line/col asserted; **valgrind-clean** tail;
  build-without-libxml2 degrades to the standard clean error (assert like
  other optional modules do).

#### WP-XML-2 — Navigation helpers
- **Goal:** `find`, `find_all`, `text`, `attr` + mini-path semantics.
- **Depends:** WP-XML-1.
- **Read:** `xml_design.md` §3; a real Form 4 XML fixture (capture: add it
  to `tools/edgar_capture.sh` and have Matthew run — or Matthew drops one
  in; treat missing fixture as `blocked`).
- **Build:** the four helpers as pure functions over node records
  (implementable in C or stdlib `.bas` — design allows either; pick
  stdlib `.bas` first per compositions rule, drop to C only if profiling
  demands).
- **Done when:** goldens green including the **real Form 4 field-check**
  (owner CIK, transaction code, shares — listed in evidence for Matthew's
  hand check against the document); absence semantics tested (`unknown` /
  `""` per §8).

#### WP-XML-3 — Encode
- **Goal:** `xml.encode` (+ pretty), escaping, round-trip.
- **Depends:** WP-XML-2.
- **Read:** `xml_design.md` §5.
- **Build:** per design; entry validation raises on malformed node records.
- **Done when:** parse→encode→parse round-trip equality golden (structural
  compare); escaping goldens (`&`, `<`, quotes in attrs); valgrind-clean
  if implemented in C.

#### WP-XML-4 — Reader core
- **Goal:** `reader`/`read`/`close`, event records, depth cap,
  scope-cleanup close.
- **Depends:** WP-XML-1.
- **Read:** `xml_design.md` §4, §7–§8.
- **Build:** per design over xmlReader; use-after-close is a structured
  error; depth-cap violation test.
- **Done when:** event-stream golden over a small document (kinds, names,
  depths, lines); error tests green; **valgrind-clean** (readers are the
  leak-prone surface — this tail is the decisive value).

#### WP-XML-5 — `skip_to` + `subtree` (the windowing pattern)
- **Goal:** the idiom the finance library depends on.
- **Depends:** WP-XML-4.
- **Read:** `xml_design.md` §4 (windowing example); a real 13F
  information-table fixture (same acquisition note as WP-XML-2).
- **Build:** per design; `subtree` on non-start-element raises.
- **Done when:** the design's 13F windowing loop, as a test, produces a
  holdings frame field-checked against hand-read values (evidence block
  lists 2–3 rows for Matthew); valgrind-clean.

#### WP-XML-6 — Constant-memory verification
- **Goal:** prove the streaming claim.
- **Depends:** WP-XML-5.
- **Read:** `xml_design.md` §11 Phase 2 note.
- **Build:** `tools/xml_bigfile_gen.sh` (synthesizes a ≥100MB document;
  not checked in); a harness test that streams it via skip_to/subtree
  while sampling `/proc/self/status` VmHWM, asserting peak RSS under a
  stated bound (e.g. 64MB) and flat across the run.
- **Done when:** harness output tail recorded (file size, VmHWM before/
  after/peak). Marked as a **manual-tier test** (not in `make test` — too
  slow) with its invocation documented.

#### WP-XML-7 — Lenient HTML
- **Goal:** `parse_html` + whole-document `text` extraction.
- **Depends:** WP-XML-2.
- **Read:** `xml_design.md` §6; a real 10-K HTML fixture (capture note as
  before).
- **Build:** per design (htmlRead*, same node shape).
- **Done when:** parse of the real 10-K succeeds; `xml.text` output
  contains 3 sentinel strings Matthew picks from the document (evidence
  lists them); tag-soup fixture (unclosed tags) parses without error;
  valgrind-clean.

---

### Track C — Ownership (needs Track A + WP-XML-1…5)

#### WP-OWN-1 — `insiders.bas`: transactions + screen
- **Goal:** Form 4 → transaction frame; `open_market_buys`.
- **Depends:** WP-EDG-3, WP-XML-2.
- **Read:** `edgar_design.md` §1 (Form 4), §4.3; the Form 4 fixture.
- **Build:** per design — all §4.3 columns; multiple Form 4s per filer
  supported (frame concat).
- **Done when:** field-check golden vs hand-read fixture values (evidence
  lists them); code-P filter test; A/D and officer-title columns asserted.

#### WP-OWN-2 — `insiders.bas`: cluster + conviction
- **Goal:** the two convenience compositions.
- **Depends:** WP-OWN-1.
- **Read:** `edgar_design.md` §4.3; your WP-OWN-1 code.
- **Build:** `cluster(buys, window)` (distinct owners within window);
  `conviction(buys)` (value / prior stake from `post_shares` − `shares`);
  synthetic fixtures for both (real fixtures unlikely to contain clusters).
- **Done when:** synthetic goldens green; division-by-zero / zero-prior-
  stake case yields `unknown` (tested).

#### WP-OWN-3 — `ownership.bas`: 13F + delta
- **Goal:** report frame, unit normalization, quarter diff.
- **Depends:** WP-XML-5, WP-EDG-3.
- **Read:** `edgar_design.md` §1 (13F), §4.4; the 13F fixture(s) — two
  quarters of one filer needed (capture note).
- **Build:** `report_13f` (windowed parse via skip_to/subtree); date-based
  value-unit normalization; `delta` via `frame.join` on cusip with
  new/exited/delta columns.
- **Done when:** report field-check golden; a delta golden from a
  hand-computed pair (evidence lists one new, one exited, one changed
  position for Matthew's check).

#### WP-OWN-4 — `ownership.bas`: 13D/G stakes
- **Goal:** the fast ownership signal.
- **Depends:** WP-OWN-3.
- **Read:** `edgar_design.md` §1 (13D/G), §4.4; a structured-era 13D or
  13G fixture (capture note; pre-2025 text filings are out of scope —
  encode that as a tested error/skip, not silence).
- **Build:** `stakes(cik)` per design: filer, form, percent, filed,
  amended.
- **Done when:** field-check golden; a 13G-then-13D synthetic sequence
  shows both rows (the flip is visible).

---

### Track D — Events + monitor

#### WP-EVT-1 — Event classification into flags
- **Goal:** ensure NT / /A / 4.01 / 4.02 / 5.02-cluster detection (built in
  WP-FOR-4) is exposed as a standalone events surface too, per
  `edgar_design.md` §1/§4.5 — refactor only if the design demands it.
- **Depends:** WP-FOR-4.
- **Read:** `edgar_design.md` §4.5 flags item, §7; your WP-FOR-4 code.
- **Build:** whatever thin exposure is missing (possibly nothing — then
  this WP records that finding and closes).
- **Done when:** existing suites still green (counts unchanged or grown,
  never shrunk); PROGRESS records the outcome.

#### WP-EVT-2 — The watcher monitor (flagship example)
- **Goal:** `examples/edgar/monitor.bas` per `edgar_design.md` §7.
- **Depends:** WP-EVT-1, WP-CORE-1 (`sleep`).
- **Read:** `edgar_design.md` §7; gbasic-design §9 (watchers) — sections
  on registration, immediate firing, and `without watchers` only.
- **Build:** the polling loop + watched inbox + item-code alert watchers;
  an **offline test harness** that drives the same watcher logic by
  appending synthetic filings (no sleep, no network) and asserts alert
  output.
- **Done when:** harness golden green; the live program is demo-run by
  Matthew, not tested.

---

### Track E — LLM client

#### WP-LLM-1 — Adapters + ask/chat
- **Goal:** both wire formats over a fixture transport.
- **Depends:** WP-CORE-1 (`env`).
- **Read:** `llm_design.md` §1–§4 (whole); `webclient` request surface
  (same limited read as WP-EDG-2).
- **Build:** constructors (`anthropic`/`openai`/`local`), handle record
  fields; adapters as pure request-record→wire / wire→response-record
  functions; `ask`/`chat`; failure policy §4 (transport raises after
  backoff budget; the backoff loop itself unit-tested with an injected
  failing transport — no real waiting: make the sleep injectable);
  `llm.offline(dir)` fixture transport mirroring the edgar seam;
  `examples/llm/smoke_*.bas` manual live scripts (not in make test).
- **Done when:** adapter goldens green for both formats (request built +
  response parsed from checked-in fixture pairs); usage extraction
  asserted; retry test proves 3 backoffs then raise.

#### WP-LLM-2 — `ask_json`
- **Goal:** structured output with the one corrective retry.
- **Depends:** WP-LLM-1.
- **Read:** `llm_design.md` §3 (structured output), §7.1.
- **Build:** fence-stripping; decode; one corrective retry; `unknown`
  after; retry is its own budget (design leaning, adopt it).
- **Done when:** tests green: clean JSON, fenced JSON, garbage-then-clean
  (retry succeeds), garbage-then-garbage (`unknown`); counts recorded.

---

### Track F — MD&A judgment

#### WP-MDA-1 — Extraction
- **Goal:** HTML → MD&A / risk-factor sections.
- **Depends:** WP-XML-7.
- **Read:** `edgar_design.md` §5.2 (extraction); the 10-K HTML fixture(s) —
  ideally two consecutive years of one filer (capture note).
- **Build:** `mdna.sections(html_text)` → `{mdna, risk_factors}` strings or
  `unknown` per section on sectioning failure; Item-heading location per
  design; whole-document fallback accessor.
- **Done when:** for each fixture year, evidence lists the first ~10 words
  of each extracted section for Matthew's eyeball check against the
  document; failure-path test (headerless synthetic HTML → `unknown`).

#### WP-MDA-2 — Deterministic pre-pass
- **Goal:** YoY diff + hedge-density.
- **Depends:** WP-MDA-1.
- **Read:** `edgar_design.md` §5.2 (pre-pass).
- **Build:** risk-factor added/removed diff (sentence- or
  item-granularity — pick, record the choice); hedge-lexicon rate (lexicon
  as a data list in the module, editable); evidence-record assembly
  including a supplied forensics scorecard.
- **Done when:** diff golden on the two fixture years plus a synthetic pair
  with known adds/removes; hedge-rate golden.

#### WP-MDA-3 — Panel + referee
- **Goal:** the fight.
- **Depends:** WP-MDA-2, WP-LLM-2.
- **Read:** `edgar_design.md` §5.2 (panel); `llm_design.md` §3, §5.
- **Build:** stance prompts as library defaults (overridable); verdict
  schema (fields per design + `edgar_design.md` §9.6 — absent fields are
  `unknown`); `mdna.panel` over `llm.ask_json` with fixture models;
  `mdna.referee`; disagreement measure (variance on candor) as a helper.
- **Done when:** **shape/schema tests only** over fixture responses
  (including one malformed-panelist fixture → `unknown` row, panel
  continues); one manually-recorded real transcript saved to
  `examples/edgar/panel_transcript.md` as a worked example, not a test.

---

### Track G — Screener

#### WP-SCR-1 — Bulk ingest
- **Goal:** `screener.ingest(zip)` → local sqlite, resumable.
- **Depends:** WP-EDG-4.
- **Read:** `edgar_design.md` §4.6, §8.5; sqlite module surface (limited).
- **Build:** ingest over a **truncated sample zip** fixture (a fixture
  script builds one from the three filers' facts files); resumability
  (re-run continues, doesn't duplicate — tested by interrupting via a
  row-count cap injection); `screener.universe()`.
- **Done when:** ingest-twice row counts equal ingest-once (asserted);
  universe golden over the sample.
- **Notes:** zip handling may hit gBASIC capability limits (no binary-safe
  strings). If unzip must be shelled out or pre-extracted by the user,
  that is acceptable — record the decision; if it needs a design change,
  `blocked`.

#### WP-SCR-2 — Cross-sectional scoring + `run`
- **Goal:** the market screen.
- **Depends:** WP-SCR-1, WP-FOR-4.
- **Read:** `edgar_design.md` §4.6; screener + forensics surfaces (grep).
- **Build:** incremental score computation cached in sqlite; `run(u, fn)`;
  the `unknown` long-tail report per concept (the concept map's report
  card, per design).
- **Done when:** screen golden over the sample universe; unknown-tail
  report golden. Full-market acceptance is a Matthew-run milestone against
  real bulk data, recorded in PROGRESS when done.

---

## 6. Suggested assignment order

Fast path first, then interleave:

```
WP-0, WP-CORE-1,
WP-EDG-1 (Matthew runs capture), WP-EDG-2, WP-EDG-3, WP-EDG-4, WP-EDG-5, WP-EDG-6,
WP-FOR-1, WP-FOR-2, WP-FOR-3, WP-FOR-4, WP-DEMO-1,          <- Adrian demo
WP-XML-1 … WP-XML-5 (anytime after WP-0, interleavable),
WP-OWN-1 … WP-OWN-4,
WP-XML-6, WP-XML-7,
WP-EVT-1, WP-EVT-2,
WP-LLM-1, WP-LLM-2,
WP-MDA-1 … WP-MDA-3,
WP-SCR-1, WP-SCR-2
```

---

End of development plan.
