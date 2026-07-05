# EDGAR Suite — Reference

Public API for the gBASIC EDGAR / securities-analysis libraries. For a narrative
introduction and runnable recipes, see the
**[Tutorial & Cookbook](edgar_tutorial.md)**.

Each library is loaded by a path relative to your source file, e.g. from a
program in `examples/`:

```basic
load edgar        from "../stdlib/edgar.bas"
load fundamentals from "../stdlib/fundamentals.bas"
load forensics    from "../stdlib/forensics.bas"
load screener     from "../stdlib/screener.bas"
load insiders     from "../stdlib/insiders.bas"
load ownership    from "../stdlib/ownership.bas"
load mdna         from "../stdlib/mdna.bas"
load llm          from "../stdlib/llm.bas"
```

---

## Conventions & common shapes

**Frame.** A record whose fields are equal-length column lists (column-major
table). `count(frame["end"])` is the row count; `frame["col"][i]` is one cell.
Iterate by index or with `for each`.

**Fundamentals frame.** Always carries `end` (period end, `"YYYY-MM-DD"`), `fp`
(fiscal period: `"FY"` or a quarter), `fy` (fiscal year number), plus the
metric columns. Rows are ascending by `end` and interleave annual and quarterly
periods — filter `fp = "FY"` for annual trends.

**Scorecard frame** (forensics). One row per fiscal year, ascending; the **last
row is the latest year**. Carries `end` and usually `prior_end` (the year compared
against), one column per model component, and the headline column. Any component
or the headline is `unknown` when an ingredient is missing.

**`unknown` / NA policy.** Missing data is `unknown`, never guessed or zeroed.
`unknown` propagates through arithmetic and scoring. Test with `is_unknown(x)`.
Bracket-reading an absent record key (`rec["missing"]`) returns `unknown`;
dot-reading an absent field (`rec.missing`) raises — prefer bracket reads for
possibly-absent keys.

**Identifiers are strings.** CIK is a 10-digit zero-padded string
(`"0000320193"`); accession numbers and CUSIPs are strings. Never do arithmetic
on them.

**Session handle** (`edgar`). An opaque record carrying identity, cache path, and
offline directory. Setters take it and return an updated copy
(`e = edgar.identify(e, ...)`).

**Model handle** (`llm`). A record carrying wire format, endpoint, model name, key
sourcing, tunables (`temperature` 0, `max_tokens` 1024, `timeout` 60, `retries`
3), and injection seams. Setters return an updated copy.

---

## `edgar` — acquisition

The single gate to SEC data: identity-enforced, cache-first, with an offline
fixture seam. All fetches require an identity to be set (even offline).

| Function | Returns | Notes |
| --- | --- | --- |
| `session()` | session handle | Fresh handle; no identity, default cache path. |
| `identify(e, contact)` | handle | Set the contact string (required for any fetch). Live: use a real `"Name <email>"`. |
| `offline(e, dir)` | handle | Read fixtures from `dir` instead of the network. Fixture names mirror `tools/edgar_capture.sh`. |
| `cache(e, path)` | handle | Set the sqlite cache DB path (document bodies + transport counter). |
| `cik(e, ticker)` | CIK string, or `unknown` | Ticker → 10-digit CIK. Case-insensitive ticker. |
| `company_facts(e, cik)` | facts record | The whole companyfacts XBRL document (`{cik, entityName, facts}`). |
| `submissions(e, cik)` | frame | Filing index: `form, filed, accession, period, primary_document, items` (newest-first). |
| `document(e, cik, accession, filename)` | string | One filed document's body (cached forever — immutable). |
| `poll(subs, last_seen)` | frame | **Pure.** Rows of `subs` newer than the `last_seen` accession (those ahead of it in newest-first order). `unknown`/absent ⇒ all rows. |
| `transport_count(e)` | number | How many real network fetches have occurred (cache misses). |

**Offline fixture names** (in the `offline` dir): `company_tickers.json`,
`submissions_CIK{10}.json`, `companyfacts_CIK{10}.json`,
`doc_{cik}_{filename}`.

---

## `fundamentals` — 10-K/10-Q numerics

Turns companyfacts into tidy frames via a concept map (stable metric names →
prioritized XBRL tag chains). Missing concept ⇒ `unknown`.

| Function | Returns | Notes |
| --- | --- | --- |
| `concepts()` | list of strings | Every concept key the library knows. |
| `series(facts, concept)` | frame `{end, start, value, fy, fp, form, accession, filed}` | One concept, deduped to latest-filed per period. `unknown` if the concept is unmapped or absent. |
| `series_as_filed(facts, concept)` | frame | Same, but originals kept (no dedup) — for as-filed vs restated analysis. |
| `fcf(facts)` | frame `{end, fp, fy, value}` | Operating cash flow − capex. |
| `debt(facts)` | frame `{end, fp, fy, total, current, noncurrent, net}` | Net = total − cash. |
| `margins(facts)` | frame `{end, fp, fy, gross, operating, net}` | Fractions of revenue. |
| `ratios(facts)` | frame | Interest coverage, current ratio, net debt/EBITDA, FCF conversion. |
| `compare(facts_list, metrics)` | frame | Cross-company comparison aligned on `metrics` (a list of concept keys). |

**Key concepts** (non-exhaustive): `revenue`, `cost_of_revenue`, `gross_profit`,
`sga`, `dep_amort`, `operating_income`, `net_income`, `operating_cash_flow`,
`capex`, `cash`, `receivables`, `current_assets`, `current_liabilities`,
`ppe_net`, `total_assets`, `total_liabilities`, `long_term_debt`,
`retained_earnings`, `book_equity`, `shares_outstanding`, `share_based_comp`,
`stock_repurchased`, `dividends_paid`. Full list: `fundamentals.concepts()`.

---

## `forensics` — the honesty arithmetic

Published earnings-quality / distress / manipulation models, each a per-fiscal-year
scorecard frame with components exposed and `unknown` propagated. **The last row
is the latest year.**

| Function | Returns (frame columns) | Notes |
| --- | --- | --- |
| `accruals(facts)` | `end, prior_end, net_income, operating_cash_flow, avg_total_assets, accrual_ratio` | Sloan accrual ratio = (NI − CFO) / avg total assets. The earnings-quality primitive. |
| `piotroski(facts)` | `end, prior_end, f_roa, f_cfo, f_droa, f_accrual, f_dlever, f_dliquid, f_shares, f_dmargin, f_dturn, f_score` | F-Score 0–9 (sum of the nine booleans). `f_score` is `unknown` if any test is. |
| `beneish(facts)` | `end, prior_end, dsri, gmi, aqi, sgi, depi, sgai, lvgi, tata, mscore, flag` | 8-index M-Score. `flag = mscore > -1.78` (manipulation threshold). |
| `mscore(indices)` | number, or `unknown` | Composite from a record of the eight indices `{dsri, gmi, aqi, sgi, depi, sgai, lvgi, tata}`. |
| `altman(facts)` | `end, x1_working_capital, x2_retained_earnings, x3_ebit, x4_book_equity, zscore, zone` | Z″ variant (book equity; no market data). `zone` ∈ `distress`/`grey`/`safe`. |
| `altman_classic(facts, prices)` | `end, x1..x4_market_equity, x5_sales, zscore, zone` | Classic Z (market equity). `prices` = `{date:[...], close:[...]}` aligned to FY ends. |
| `dilution(facts)` | `end, prior_end, shares, prior_shares, shares_change, sbc, buybacks, net` | `net = buybacks − SBC`: positive = real capital return. |
| `flags(facts, subs)` | `kind, date, accession, period, detail` | Composite red-flag dossier (facts + submissions). 90-day 5.02 cluster window. |
| `flags_window(facts, subs, window_days)` | same | `flags` with an explicit officer-exodus cluster window. |
| `events(subs)` | `kind, date, accession, period, detail` | **Facts-free** submissions-side subset of `flags` (for the monitor / single filings). 90-day window. |
| `events_window(subs, window_days)` | same | `events` with an explicit cluster window. |

**Flag / event `kind` values:** `nt_filing` (late), `amendment` (10-K/A, 10-Q/A),
`auditor_change` (8-K 4.01), `non_reliance` (8-K 4.02), `officer_exodus`
(clustered 8-K 5.02), `rising_accruals`, `mscore_flag`,
`positive_ni_negative_fcf`. Submissions rows carry `accession`; facts rows carry
`period` (the fiscal-year end) with `accession = unknown`.

**Applicability caveat.** Beneish and Altman were built on industrials and are
ill-defined for financials; they false-positive on large growth/buyback firms.
Treat outputs as evidence, not verdicts.

---

## `screener` — the whole-market bulk tier

Ingests an extracted directory of `CIK{10}.json` companyfacts files into a local
sqlite DB, scores them cross-sectionally (incremental, resumable), and screens
with a predicate. `db_path` is a sqlite file path; `facts_dir` is a directory of
`CIK{10}.json` files (`unzip companyfacts.zip -d facts_dir` first — the library
does not unzip).

| Function | Returns | Notes |
| --- | --- | --- |
| `ingest(db_path, facts_dir)` | number ingested | Light universe (cik/name/latest-period/fact-count). Resumable (skips known CIKs). |
| `ingest_limited(db_path, facts_dir, cap)` | number ingested | Ingest at most `cap` new filers this call (`cap <= 0` = no limit). |
| `universe(db_path)` | frame `{cik, name, latest_period}` | The ingested universe, cik-ordered. |
| `score(db_path, facts_dir)` | number scored | Compute+cache latest-FY forensic headlines per filer. Resumable. |
| `score_limited(db_path, facts_dir, cap)` | number scored | Score at most `cap` new filers this call. |
| `scored(db_path)` | frame `{cik, name, latest_period, piotroski, mscore, mscore_flag, accrual_ratio, fcf}` | The scored universe (NULLs restored to `unknown`). |
| `run(u, fn)` | frame | Keep rows of frame `u` where `fn(row_record)` returns exactly `true`. `fn` is a named function value. |
| `unknown_report(db_path)` | `{filers, concept:[...], unknown_count:[...], rate:[...]}` | Per-concept coverage report card (the `unknown` long tail), concept-map order. |

**Note.** Scores here are facts-only. Submissions-side signals (NT / 4.02 /
5.02-cluster) are not in the bulk tier — `companyfacts.zip` carries no
submissions; join `forensics.events` per CIK separately if you need them.

---

## `insiders` — Form 4

Insider transactions over the `xml` module. Codes matter: **P** = open-market
purchase (the conviction signal); A/M/F/S are compensation mechanics or sales.

| Function | Returns | Notes |
| --- | --- | --- |
| `from_form4(doc, filed)` | transaction frame | Parse one Form 4 (`doc` = `xml.parse(body)`; `filed` = `"YYYY-MM-DD"`). |
| `concat(a, b)` | frame | Stack two transaction frames (for combining many Form 4s). |
| `open_market_buys(tx)` | frame | Filter a transaction frame to code-P buys only. |
| `conviction(buys)` | frame + `prior_stake, conviction` | Buy value ÷ prior stake (post_shares − shares). First-ever buy or missing input ⇒ `unknown` (no divide-by-zero). |
| `cluster(buys, window)` | cluster frame | Group buys within `window` days of each cluster's first buy; summarizes distinct `owners`, count, total value, date span. `owners >= 2` is the multi-insider signal. |
| `transactions(e, cik, since)` | transaction frame | **Network.** Every Form 4 for `cik` filed on/after `since`, combined. Composes the above over an edgar session. |

---

## `ownership` — 13F holdings and 13D/G stakes

Over the `xml` module. 13F value is normalized to **whole dollars** (the SEC's
2023 amendment switched `<value>` from thousands to whole dollars; the library
hides the seam by filing date).

| Function | Returns | Notes |
| --- | --- | --- |
| `report_13f(source, filed)` | holdings frame | Stream a 13F information table (issuer, cusip, value, shares) into a frame. Value in whole dollars. |
| `delta(prior, current)` | change frame | Join two quarters on cusip: new / exited / increased / decreased positions. **The signal is the diff** — 13F data is ~4.5 months stale. |
| `stake(source, filed)` | stake record | Parse one structured 13D/13G primary doc: `{filer, form, percent, filed, amended, issuer_cik, issuer_name, cusip, event_date}`. |
| `stakes(rows)` | events frame | Stack many stake records into a frame. |

**The 13D/G signal:** crossing 5% forces disclosure, and the *form choice* is the
message — **13G = passive, 13D = activist**; a 13G→13D switch means a large holder
just turned hostile. Structured XML only (SEC's late-2024 mandate); earlier text
filings are out of scope.

---

## `mdna` — MD&A extraction, diffing, and the analyst panel

Deterministic narrative processing (no LLM) plus an optional LLM panel. Extraction
uses the HTML parser; the panel uses `llm`.

| Function | Returns | Notes |
| --- | --- | --- |
| `text(html_text)` | string | HTML → clean plain text (entities/nbsp normalized). |
| `sections(html_text)` | `{mdna, risk_factors}` | Extract Item 7 (MD&A) and Item 1A (risk factors) by case-sensitive uppercase item headers. |
| `risk_diff(prior_risk, current_risk)` | diff | Sentence-level year-over-year change in the risk section. |
| `hedge_lexicon()` | list | The hedging/uncertainty word list used by the stats. |
| `hedge_stats(text)` | stats record | Hedging-language counts (an evasiveness proxy). |
| `hedge_rate(text)` | number | Hedging density (hedge words per unit text). |
| `evidence(prior, current, scorecard)` | evidence record | Bundle the deterministic findings (risk diff, hedge stats, forensic scorecard) for the panel. |
| `stance_bull()` / `stance_bear()` / `stance_forensic()` / `stance_referee()` | stance record | Prebuilt analyst stances (name + system prompt). |
| `panel(panelists, sections, evidence)` | verdict frame | One `llm.ask_json` per panelist over the sections+evidence. A malformed reply ⇒ an `ok=false` row (the panel continues). |
| `disagreement(verdicts)` | number | Spread across the panel (variance of the candor/verdict column). |
| `referee(frontier_model, verdicts)` | referee record | A referee model reconciles the panel into one call. |

---

## `llm` — chat-completion client

One client over Anthropic, OpenAI, and local (Ollama/vLLM) wire formats. Transport
and backoff sleep are injectable, so the retry logic is testable offline.

| Function | Returns | Notes |
| --- | --- | --- |
| `anthropic(model, key)` | model handle | Anthropic wire format; key sourced from arg or `ANTHROPIC_API_KEY`. |
| `openai(model, key)` | model handle | OpenAI wire format; `OPENAI_API_KEY`. |
| `local(base_url, model)` | model handle | OpenAI format at your `base_url`, no key. |
| `offline(m, dir)` | handle | Read canned responses `{dir}/{format}_response.json` instead of the network. |
| `with_transport(m, fn)` | handle | Inject a transport function (unit-test the retry loop with a failing transport). |
| `with_sleep(m, fn)` | handle | Inject the backoff sleep (no real waiting in tests). |
| `chat(m, system, messages)` | `{text, usage:{input,output}, stop_reason, model, raw}` | Full form: system prompt + explicit message list `[{role, content}, ...]`. |
| `ask(m, system, prompt)` | string, or `unknown` | The 90% case: one user turn → the assistant's text. |
| `ask_json(m, system, prompt)` | record / list, or `unknown` | Validated structured output. Validates before `decode` (never raises); one corrective retry; `unknown` if the model won't emit valid JSON. |

**Handle tunables** (set as record fields on the handle): `temperature` (0),
`max_tokens` (1024), `timeout` (60s), `retries` (3). The `_send` retry loop honors
429/5xx with exponential backoff and `Retry-After`.

---

## See also

- **[Tutorial & Cookbook](edgar_tutorial.md)** — narrative + recipes.
- **`docs/edgar_design.md`** — rationale, domain primer, applicability caveats.
- **`examples/edgar/scorecard.bas`** — canonical end-to-end program.
- **`examples/*_test.bas`** — a verified, runnable template per feature.
