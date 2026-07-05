# EDGAR & Securities Analysis Library — Design (v2)

Status: **design proposal; nothing built.** v2 expands v1 in four ways: a
**forensic-accounting module** (the published manipulation/distress scores —
the "are they being honest" question, quantified), **activist ownership**
(13D/13G), a **red-flag events** surface (late filings, restatements, auditor
changes, officer exodus), and a **whole-market screener** over the SEC's bulk
data. It also retires v1's XML earn-it question: `xml_design.md` and
`llm_design.md` now exist as standalone dependencies.

Guiding constraint unchanged — **simplicity without sacrificing
functionality**, built on modules gBASIC already has (`webclient`, `sqlite`,
`frame`, `stats`, watchers, `unknown`) plus the two new libraries. Core
changes admitted **only if earned** (`statistics_design.md` §6).

The architecture still follows the domain expert's own seam:

> 10-K/10-Q balances and cash flows are numerical. 13F is numerical. Form 4 is
> a screen for non-standard buys. 8-K and MD&A are subjective.

A **numerical layer** (acquisition → extraction → derived metrics → screens)
and a **judgment layer** (LLM panels over extracted narrative). The v2 thesis
that extends the seam: *most of the "subjective" honesty question has a
computable core* — decades of forensic-accounting research reduced earnings
manipulation and distress to published arithmetic over exactly the XBRL facts
Phase 1 already fetches. The LLM panel then argues over computed evidence
instead of raw vibes.

---

## 1. Domain primer (what the forms are)

Recorded so the library's vocabulary is self-explanatory.

- **10-K / 10-Q** — annual / quarterly report. Financial statements are
  XBRL-tagged and republished by the SEC as JSON (§2): the numeric content
  never requires document parsing. The narrative (MD&A, risk factors) lives
  in the HTML document — judgment-layer territory.
- **Form 4** — insider transaction report, due within 2 business days. Clean
  XML. Each transaction carries a one-letter code; **code P** (open-market
  purchase) is the "non-standard buy" — the only code that is a voluntary
  purchase with the insider's own money. A (award), M (option exercise),
  F (tax withholding) are compensation mechanics; S is a sale (weak signal —
  sales have a hundred innocent explanations, purchases have one).
- **Schedule 13F** — quarterly long-position disclosure by institutions
  managing ≥ $100M, due 45 days after quarter end. XML information table.
  The signal is the **quarter-over-quarter diff** per filer; the data is up
  to ~4.5 months stale by construction.
- **Schedules 13D / 13G** — crossing **5% ownership** of a company forces
  disclosure, and the *choice of form* is the signal: **13G declares passive
  intent; 13D declares intent to influence** (activism). A 13G→13D switch
  means a large holder just turned hostile. Days-scale deadlines, so this is
  the *fast* ownership signal complementing 13F's slow one. Structured XML
  since the SEC's late-2024 mandate; earlier filings are text (out of v1
  scope).
- **8-K** — "something material happened; disclosure is legally required,"
  with numbered **items**: 2.02 earnings, 5.02 officer departure/appointment,
  4.01 auditor change, **4.02 non-reliance on previously issued financials
  (the fire alarm)**, 1.01 material agreement, 7.01 Reg FD, 8.01 other. Item
  codes ride in the submissions JSON — no document parsing.
- **NT 10-K / NT 10-Q** — "we cannot file on time." One of the most reliable
  cheap trouble signals in existence, and it is literally a form type in the
  submissions feed.
- **10-K/A, 10-Q/A** — amended financials; restatement territory.
- **MD&A** — Management's Discussion & Analysis: unstructured, evasive by
  nature, the primary LLM target.

---

## 2. Data sources: EDGAR's JSON APIs first

Everything from the SEC directly; no vendor. Preference order:

1. **XBRL Company Facts JSON** —
   `data.sec.gov/api/xbrl/companyfacts/CIK{10}.json`: every tagged financial
   fact the company ever filed (value, unit, fiscal year/period, period end,
   form, accession). One endpoint covers the whole 10-K/10-Q numeric surface.
2. **Submissions JSON** — `data.sec.gov/submissions/CIK{10}.json`: the filing
   index (form, date, accession, 8-K items). Polling target for §7.
3. **Filing documents** by accession, only when needed: Form 4 XML, 13F
   information-table XML, 13D/G XML, and (judgment layer) 10-K/Q HTML.
4. **Bulk** — the nightly `companyfacts.zip` (whole-market facts) feeds the
   §6 screener.

Ticker → CIK: `www.sec.gov/files/company_tickers.json`.

**Client discipline (non-negotiable, owned by `edgar.bas`):** declared
`User-Agent` with contact info, ≤ 10 requests/second, cache everything.
Violators get blocked; the module makes violation impossible.

**Identifier rule (inherited):** CIKs, accession numbers, CUSIPs are
**strings** — the big-integer rule the DB modules and `frame.read_csv`
already follow. Dollar amounts stay numbers (doubles are integer-exact to
~9e15; XBRL facts are typically dollar-rounded). `money` for facts remains
flagged-not-proposed (§9).

---

## 3. Representation: filings are records, tables are frames

No new value kinds — the `statistics_design.md` §3 argument verbatim.

- A **filing** is a record: `{cik, accession, form, filed, period, items, url}`.
- A **fact series** is a frame: `{end:[…], value:[…], fy:[…], fp:[…],
  form:[…], accession:[…]}` — so the shipped `stats` surface applies
  directly: `sma(facts.value, 4)`, `diff(facts.value, 1)`,
  `correlation(a.value, b.value)`.
- Form 4 transactions, 13F holdings and diffs, 13D/G events, red-flag lists,
  forensic scorecards, screener output, and LLM verdicts are **all frames** —
  screening is `frame.filter`, diffing is `frame.join`, ranking is
  `frame.sort_by`. This library is the frame layer's first serious external
  customer.

---

## 4. The numerical layer

### 4.1 `stdlib/edgar.bas` — acquisition

Owns transport, identity, rate limiting, caching. Knows nothing about
balance sheets.

```basic
load edgar

e = edgar.session()                                         ' explicit state handle
e = edgar.identify(e, "WorkSplicer research matt@example.com")  ' required; unset => any fetch raises
cik   = edgar.cik(e, "AAPL")
subs  = edgar.submissions(e, cik)           ' filing-index frame
facts = edgar.company_facts(e, cik)         ' decoded companyfacts record
doc   = edgar.document(e, cik, accession, filename)
new   = edgar.poll(subs, last_seen)         ' pure submissions diff for the monitor
```

**State handle (revised 2026-07-02).** gBASIC `.bas` libraries hold no mutable
module/global state (library top-level code does not execute on load, functions
are references not closures, records pass by value). So `edgar` threads an
explicit **session handle** — created by `edgar.session()`, updated by the
setters (`identify`, `offline`, `cache`), which each return the new handle, and
passed as the first argument to every call. This is the idiomatic gBASIC shape
(data in, data out — like frames). The handle carries config only (identity,
offline dir, cache path); genuinely mutable runtime state (cached bodies, the
transport counter) lives in the sqlite cache DB, which is real persistence.

Setters and test seam:

```basic
e = edgar.session()                         ' defaults cache to ~/.gbasic/edgar.db
e = edgar.cache(e, "/tmp/edgar_test.db")    ' override cache path (tests)
e = edgar.offline(e, "examples/fixtures/edgar")  ' fixture mode; miss => structured error
n = edgar.transport_count(e)                ' transport calls so far (cache-hit assertions)
```

Every fetch passes one internal gate: rate cap (≥100ms spacing on real
transport), User-Agent, cache-first. **Cache is `sqlite`** (`~/.gbasic/edgar.db`
by default): filed documents are immutable → cached forever; index/facts
endpoints on short TTL. The cache sits in front of the offline seam too, so the
transport counter counts fixture reads exactly as it counts network fetches — a
second identical call is a cache hit and touches no transport.

### 4.2 `stdlib/fundamentals.bas` — 10-K/10-Q numerics

**XBRL tag normalization is the real work.** Companies tag the same concept
differently, and facts repeat across the 10-K and later 10-Qs. Core
machinery:

- **Concept resolution** — a curated map from library concepts to ordered
  **tag fallback chains**; first present wins, none present ⇒ `unknown`
  (never a guess). The v2 concept map grows to feed §4.5:
  revenue, cost of revenue / gross profit, SG&A, depreciation & amortization,
  operating income (EBIT), net income, interest expense, operating cash
  flow, capex, cash & equivalents, receivables, current assets/liabilities,
  total assets/liabilities, long-term debt (± current portion), retained
  earnings, book equity, shares outstanding, share-based compensation,
  stock repurchased, dividends paid.
- **Period dedup** — collapse repeats by (period end, fiscal period),
  preferring the latest accession; an `as_filed` option keeps originals so
  restatement *deltas* are themselves inspectable (resolves v1 open q. 2 —
  for honesty work, the original number is often the interesting one).

```basic
load fundamentals

s   = fundamentals.series(facts, "operating_cash_flow")  ' deduped fact frame
fcf = fundamentals.fcf(facts)          ' op cash flow - capex, per period
d   = fundamentals.debt(facts)         ' total/current/noncurrent/net
m   = fundamentals.margins(facts)      ' gross/operating/net
r   = fundamentals.ratios(facts)       ' interest coverage, current ratio,
                                       ' net debt / EBITDA (proxy), FCF conversion
c   = fundamentals.compare(ciks, ["fcf", "margins"])   ' peer frame, one row per company
```

All outputs are fact frames; trend work is shipped Phase-4 stats (`diff`,
`sma`). Missing ingredients propagate as `unknown` rows under the standing
NA policy.

### 4.3 `stdlib/insiders.bas` — Form 4 (over the `xml` module)

```basic
load insiders

tx   = insiders.transactions(cik, since)   ' frame, one row per transaction
buys = insiders.open_market_buys(tx)       ' code P only
```

Columns: `code`, `acquired` (A/D), `shares`, `price`, `value`, `owner`,
`is_officer`, `is_director`, `officer_title`, `post_shares`, `filed`, `date`.
Convenience compositions: `insiders.cluster(buys, window)` (distinct insiders
buying within a window), `insiders.conviction(buys)` (buy size vs prior
stake, from `post_shares`). The library ranks nothing and recommends
nothing; it produces the columns humans and the judgment layer reason over.

### 4.4 `stdlib/ownership.bas` — 13F + 13D/G (over the `xml` module)

The slow signal and the fast signal in one module:

```basic
load ownership

q1 = ownership.report_13f(filer_cik, "2026Q1")   ' issuer/cusip/value/shares frame
d  = ownership.delta(q1, q2)      ' join on cusip: new/exited/delta_shares/delta_value

a  = ownership.stakes(cik)        ' 13D/13G events for a *subject* company:
                                  ' filer, form (13D|13G), percent, filed, amended
```

- `delta` is `frame.join` plus arithmetic. 13F `value` units normalized by
  filing date (dollars now, thousands historically) so user code never sees
  the seam. The 45-day staleness is documented loudly.
- `stakes` surfaces the form-choice signal directly; a filer appearing first
  as 13G and later as 13D is visible as two rows — the passive→activist
  flip. Pre-structured-XML-era (pre-2025) filings are out of scope in v1.
- 13F filer-identity aggregation (one manager, many filer CIKs) remains
  per-CIK in v1 (open question retained).

### 4.5 `stdlib/forensics.bas` — the honesty arithmetic (NEW)

The v2 centerpiece. Published, citable models computed **entirely from
§4.2's concept map** — no vendor data, no prices (one noted exception), no
LLM. Each returns a scorecard frame (per period) with components exposed,
never just the headline number, and `unknown` wherever an ingredient is
missing.

- **`forensics.accruals(facts)`** — Sloan's accrual ratio:
  `(net income − operating cash flow) / average total assets`. Profits are
  an opinion, cash is a fact; a persistently positive gap is the single most
  robust earnings-quality signal in the literature. This is the primitive
  under everything else.
- **`forensics.beneish(facts)`** — the **M-Score**, an eight-index model of
  earnings manipulation (receivables-vs-sales, gross-margin deterioration,
  asset quality, sales growth, depreciation slowdown, SG&A, leverage, total
  accruals), combined with the published weights into one score with a
  published flag threshold. Returns all eight indices plus the score —
  the components are more diagnostic than the composite.
- **`forensics.piotroski(facts)`** — the **F-Score**: nine binary
  fundamental-health tests (profitability, cash flow, cash-vs-income,
  leverage direction, liquidity direction, no new share issuance, margin
  direction, turnover direction), summed 0–9. Blunt on purpose; returns the
  nine booleans plus the sum.
- **`forensics.altman(facts)`** / **`forensics.altman(facts, prices)`** —
  distress. Default is the **Z″ variant** (book equity), computable without
  market data; with a §4.7 price frame supplied it also reports classic Z
  (market equity). Returns the component ratios plus score plus zone
  (distress / grey / safe).
- **`forensics.dilution(facts)`** — the quiet-dilution tracker: shares
  outstanding trend, share-based compensation expense, buyback spend, and
  the net of the three — exposing companies whose trumpeted buybacks merely
  mop up their own SBC issuance.
- **`forensics.flags(facts, subs)`** — one composite red-flag frame joining
  the cheap signals: NT filings, 10-K/A / 10-Q/A amendments, 8-K 4.01
  (auditor change) and 4.02 (non-reliance), clustered 5.02 departures
  (officer exodus within a window), rising accruals, M-Score over threshold,
  negative FCF with positive net income. Columns are evidence with dates and
  accessions — a dossier, not a verdict.

**Verification:** each score gets golden tests against published worked
examples and against values hand-computed from a real filer's statements;
Beneish additionally against a known historical manipulator-year (the
canonical Enron-vintage examples in the literature).

### 4.6 `stdlib/screener.bas` — the whole market (NEW; bulk tier)

The nightly `companyfacts.zip` contains every filer's facts. Ingest once
into sqlite, compute §4.5 cross-sectionally, and screening the entire US
market becomes local frame work:

```basic
load screener

screener.ingest(zip_path)               ' bulk load -> local sqlite (resumable)
u = screener.universe()                 ' cik/name/latest-period frame

hits = screener.run(u, function(f)
    return f.piotroski >= 8 and f.fcf > 0
       and f.mscore_flag = false and f.nt_count = 0
end function)
```

Honest costs stated up front: the archive is gigabytes, ingestion is a
batch job (hours, resumable), scores are computed incrementally and cached
in sqlite, and the concept map's fallback chains get stress-tested against
thousands of filers (expect a long tail of `unknown`s — that is the map
telling the truth). This phase is also the first real scale test of frames
and the sqlite module together.

### 4.7 The price contract (defined, not fetched)

EDGAR has no prices, and several natural analyses need them: classic
Altman Z, insider-buy price context, 13F delta valuation, any return
attribution. v1 **defines the interface and refuses the dependency**:

```basic
' a price frame: { date: [...], close: [...] }  (splits/dividends caller's problem)
```

Any function that can use prices takes an optional price frame in this
shape; the user supplies it from wherever they like (a CSV export, a broker
API, a future market-data module). This keeps the library honest about its
primary-source-only boundary while leaving the seam clean.

---

## 5. The judgment layer

### 5.1 `stdlib/llm.bas` — now a standalone dependency

See `llm_design.md`. Two adapters (Anthropic; OpenAI-format, which also
reaches vLLM and Ollama — so local models are a `base_url`, not an
architecture). `ask_json` owns fence-stripping and one corrective retry,
then `unknown` — settling v1's structured-output question.

### 5.2 `stdlib/mdna.bas` — extraction, diffing, the panel

**Extraction** rides `xml.parse_html` + `xml.text` (see `xml_design.md` §6):
strip the 10-K/Q HTML to text, locate MD&A / risk factors by Item headings
("Item 7.", "Item 1A."). Contract stays *best effort, `unknown` when
sectioning fails* — and the panel can be handed the whole stripped document
when it does, since LLMs tolerate noise parsers don't.

**Deterministic pre-pass** (cheap, before any LLM):

- risk factors added / removed year-over-year — the removal of a previously
  disclosed risk, or the silent disappearance of a previously touted metric,
  is among the strongest honesty signals available
- hedge-language density shift (counted lexicon), reported as a rate
- **the §4.5 scorecard for the same periods** — so the panel receives the
  accrual gap, M-Score components, and red flags as evidence

**The panel — "make them fight," formalized.** N adversarial analysts plus a
referee, each `{name, model, stance}`:

```basic
load mdna

panel = [
    {name:"bull",     model: local1, stance: mdna.stance_bull},
    {name:"bear",     model: local2, stance: mdna.stance_bear},
    {name:"forensic", model: local3, stance: mdna.stance_forensic}
]

verdicts = mdna.panel(panel, sections, evidence)   ' frame: one row per analyst
final    = mdna.referee(frontier, verdicts)        ' adjudicated record
```

Each analyst returns a structured verdict (stance-consistent read, candor
score, evasions identified, citations into the evidence record); decoded via
`llm.ask_json` into a frame, so **disagreement is measurable** — variance on
the candor column is itself a signal (uncontroversial filings converge,
evasive ones split the panel). The referee sees only the verdicts, must
adjudicate the conflict, and can check citations against the computed
evidence — the fight happens over arithmetic, which constrains confabulation.
Economics per `llm_design.md` §5: local models for the volume seats, a
frontier model in the referee chair.

---

## 6. What this library does not do

- **No market data.** Prices enter only through the §4.7 contract.
- **No recommendations.** Columns, scores, and dossiers — never buy/sell.
  That is the human's job (and legally nobody else's). The forensic scores
  are published academic models reported with their components; the library
  editorializes nothing.
- **No general XBRL taxonomy processing.** The concept map serves the
  workflow; extending it is data work, not code work.
- **No HTML fidelity.** Narrative extraction is best-effort text; faithful
  table extraction from filing HTML is future work.
- **No pre-2025 13D/G text parsing.**

---

## 7. The monitor — watchers as the native fit

Unchanged from v1 in mechanism, richer in vocabulary: the polling loop
(`edgar.poll`) appends filings to a watched queue; watcher bodies fire
synchronously on arrival. The v2 red-flag set makes the demo sharper —
8-K 4.02, NT anything, a 13D on a watched name, a code-P cluster — and this
remains the project's flagship watcher example: no other BASIC gets to write
`watch(alerts.critical)` and mean it against a real external event stream.

---

## 8. Core-change candidates ("earn it" ledger)

1. ~~XML decoding~~ — **resolved out**: `xml_design.md` (native module over
   libxml2; the earn-it case is argued there).
2. **`sleep(seconds)`** — the monitor needs it; trivial, earned here if not
   already present.
3. **`env(name)`** — needed by `llm.bas` for key sourcing if absent;
   one-liner, broadly useful.
4. **`money` for XBRL facts** — still flagged, still not proposed.
5. **`webclient` niceties** — custom User-Agent, response status access,
   and binary/zip download for §4.6 (verify early; the zip may also simply
   be fetched by the user with curl and handed to `ingest` — the honest
   fallback that keeps the core untouched).

---

## 9. Open questions

1. **Concept-map curation and testing** — golden tests per concept against a
   diverse basket (big tech, a bank, a small-cap); banks tag differently and
   several forensic ratios are ill-defined for financials (Beneish and
   Altman were built on industrials — document per-score applicability
   rather than silently emitting numbers for banks?). Leaning: emit with a
   `sector_caveat` flag once SIC codes (present in submissions JSON) are
   surfaced.
2. **`as_filed` vs restated** as the default for forensic scores — leaning
   restated for screening, `as_filed` deltas surfaced in `flags`.
3. **13F filer identity** across multiple filer CIKs — v1 per-CIK.
4. **Panel prompt ownership** — library-default stance prompts, overridable
   per panelist (unchanged from v1).
5. **Screener freshness model** — full re-ingest vs incremental daily
   deltas; start with full re-ingest (simple, resumable), measure.
6. **Verdict schema** — exact analyst-verdict fields; malformed output is
   `llm.ask_json`'s problem, absent fields are `unknown` by dynamic-read
   semantics.

---

## 10. Roadmap

Golden-file convention throughout; network responses recorded once into
sqlite/fixture snapshots under `examples/fixtures/` — tests never hit EDGAR.

### Phase 1 — Acquisition + Fundamentals *(JSON only; no parser work)*
`edgar.bas` (identify/rate/cache gate, cik, submissions, company_facts,
document, poll) and `fundamentals.bas` (concept map v2, dedup + `as_filed`,
series/fcf/debt/margins/ratios/compare). Verified: derived FCF/debt for
three diverse real filers against hand-checked filing values.
**Dependencies: webclient, sqlite, frame — all shipped.**

### Phase 2 — Forensics *(pure composition over Phase 1)*
`forensics.bas`: accruals, Beneish, Piotroski, Altman Z″ (+Z with prices),
dilution, flags (submissions-side flags only need Phase 1). Verified against
published worked examples + hand computation + one canonical historical
manipulator. **This phase needs no new I/O at all — it is the fastest
route to something Adrian-impressive.**

### Phase 3 — Ownership *(the XML phase)*
`insiders.bas` (transactions, code-P screen, cluster/conviction) and
`ownership.bas` (13F report + delta with unit normalization; 13D/G stakes).
**Depends on `xml` module Phases 1–2.** Verified against hand-read real
filings; a delta golden from a hand-computed quarter pair.

### Phase 4 — Events + Monitor
8-K item classification, NT / amendment detection feeding
`forensics.flags`, `edgar.poll`, and the §7 watcher monitor as the worked
flagship example.

### Phase 5 — Judgment
`mdna.bas` over `llm.bas` (Phases 1–2) and `xml.parse_html`: extraction,
YoY diff pre-pass, evidence assembly from Phase 2 scorecards, panel,
referee. Extraction and diffing get golden tests; panel output asserts
shape/schema only, with one recorded end-to-end transcript kept as a worked
example, not a test.

### Phase 6 — Screener *(bulk tier)*
`screener.bas`: zip ingestion into sqlite, incremental cross-sectional
scoring, `universe`/`run`. Acceptance: a full-market Piotroski + accruals +
red-flag screen completing on real bulk data, with the `unknown` long tail
quantified per concept (the concept map's report card).

### Later / stretch
Full-text search (`efts.sec.gov`), embeddings-based risk-factor drift
(`llm.embed`, per `llm_design.md` §6), proxy-statement (DEF 14A)
compensation analysis on the judgment layer, a market-data module
satisfying §4.7, and the `stats.finance` lens
(`statistics_design.md` §9) composed over fundamentals output.

---

End of EDGAR & securities analysis design (v2).
