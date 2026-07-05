# EDGAR Suite — Tutorial & Cookbook

A hands-on guide to the gBASIC EDGAR / securities-analysis libraries: acquiring
SEC data, computing fundamentals and forensic scores, screening the market,
reading ownership and insider activity, and driving an LLM analyst panel — all
from plain gBASIC, entirely offline against captured fixtures if you want.

This document is in two halves:

- **Part 1 — Tutorial**: build a full forensic dossier one step at a time.
- **Part 2 — Cookbook**: short, copy-pasteable recipes per library.

The companion **[EDGAR Suite Reference](edgar_reference.md)** lists every public
function, its arguments, and its return shape.

---

## The philosophy (read this first)

Four rules run through every library. They explain most of the API decisions:

1. **Facts, not opinions.** Every number comes from SEC-published data (the XBRL
   JSON APIs) or a published, citable model computed from it. No vendor data, no
   prices (one noted exception), no guessing.
2. **`unknown` is a first-class value.** When an ingredient is missing, the
   result is `unknown` — never zero, never a guess. `unknown` propagates: a score
   built from a missing input is itself `unknown`. Your code checks for it with
   `is_unknown(x)`.
3. **Identifiers are strings, always.** CIKs (`"0000320193"`), accession numbers,
   CUSIPs — all strings, zero-padding preserved. Never do math on them.
4. **Scores are evidence, not verdicts.** Beneish, Altman, Piotroski et al. are
   published models with known limits (they were built on industrials and
   false-positive on big growth/buyback firms). The libraries compute them
   honestly and label them; interpretation is yours.

---

## Setup

Build the interpreter once:

```sh
make
```

Everything below assumes you keep your programs **inside the gBASIC checkout**,
next to the existing examples, and run them **from the repo root** with
`GBASIC_PATH` pointed at the stdlib:

```sh
GBASIC_PATH=stdlib ./gbasic examples/my_program.bas
```

Two path conventions that trip people up:

- **`load` paths are relative to your source file.** A program in `examples/`
  loads a library with `load edgar from "../stdlib/edgar.bas"`.
- **File paths you pass to functions are relative to the working directory**
  (the repo root when you run as above), e.g.
  `"examples/fixtures/edgar/companyfacts_CIK0000320193.json"`.

The quickest way to start is to copy a working example — `examples/edgar/scorecard.bas`
is the showpiece — and edit it.

### Offline by default

Everything in this guide runs with **no network**. The captured fixtures live in
`examples/fixtures/edgar/` (company tickers, one company's facts, its
submissions) and `examples/fixtures/llm/` (canned model responses). Point the
`edgar` session at that directory (`edgar.offline`) and it reads fixtures instead
of calling SEC. This is exactly how the test suite runs, so anything you golden
offline is reproducible.

To go **live**, drop the `edgar.offline` line and set a contact string SEC can
identify you by (`edgar.identify`). That is the only difference.

---

# Part 1 — Tutorial: build a forensic dossier

We'll reproduce the heart of `examples/edgar/scorecard.bas` step by step. Create
`examples/tut.bas` and follow along; run it after each step with
`GBASIC_PATH=stdlib ./gbasic examples/tut.bas`.

## Step 1 — an offline session

Everything acquisition-related goes through an **edgar session handle** — a plain
record you thread through calls (data in, updated copy out). Point it offline and
give it a scratch cache file:

```basic
program main(args)
    load edgar from "../stdlib/edgar.bas"

    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_tut_cache.db")   ' scratch sqlite cache
    e = edgar.identify(e, "offline-demo")             ' any string offline; real contact online
    e = edgar.offline(e, "examples/fixtures/edgar")   ' read fixtures, not the network

    print(edgar.cik(e, "AAPL"))                        ' -> 0000320193
end program
```

`edgar.cik` resolves a ticker to its zero-padded CIK **string**. Note it's a
string: printing it shows `0000320193`, not `320193`.

## Step 2 — fetch the two source documents

A filer's numbers live in **companyfacts** (XBRL, one big JSON record); the list
of what they've filed lives in **submissions** (a frame of parallel columns). Both
come from the session:

```basic
    cik = edgar.cik(e, "AAPL")
    facts = edgar.company_facts(e, cik)   ' the XBRL record
    subs  = edgar.submissions(e, cik)     ' filing-index frame
    print(facts["entityName"])            ' -> Apple Inc.
```

`facts` is what every fundamentals/forensics function consumes. `subs` is what the
flags/events functions consume. You rarely touch their internals directly.

## Step 3 — one fundamental trend

`fundamentals` turns the raw facts into tidy **frames** (a record whose fields are
equal-length column lists). Let's pull free cash flow:

```basic
    load fundamentals from "../stdlib/fundamentals.bas"

    fc = fundamentals.fcf(facts)   ' { end, fp, fy, value }
    ' print the fiscal-year rows
    i = 0
    while i < count(fc["end"])
        if fc["fp"][i] = "FY" then
            print(fc["end"][i] + "  FCF=" + string(fc["value"][i]))
        end if
        i = i + 1
    end while
```

Frames interleave annual (`fp = "FY"`) and quarterly rows; filter on `fp` when you
want one or the other. Every fundamentals frame carries `end`, `fp`, `fy`.

## Step 4 — one forensic score

`forensics` composes those fundamentals into published earnings-quality models.
Each returns a **scorecard frame**: one row per fiscal year, every component
column exposed next to the headline, `unknown` wherever an ingredient was missing.

```basic
    load forensics from "../stdlib/forensics.bas"

    b = forensics.beneish(facts)   ' the 8-index M-Score, per year
    n = count(b["end"])
    ' the latest year is the last row
    print("M-Score " + string(b["mscore"][n - 1]) + " flag=" + string(b["flag"][n - 1]))
```

The M-Score `flag` is `true` when M exceeds Beneish's −1.78 manipulation
threshold. Remember rule 4: a `true` here is a *signal to investigate*, not an
accusation — the model flags plenty of honest large-cap growth firms.

## Step 5 — the red-flag dossier

`forensics.flags` joins the cheap trouble signals — some from the facts (rising
accruals, M-Score over threshold, positive net income with negative free cash
flow), some from the submissions feed (late "NT" filings, restatement amendments,
8-K item 4.01 auditor changes and 4.02 non-reliance, clustered 5.02 officer
departures) — into one frame:

```basic
    fl = forensics.flags(facts, subs)   ' { kind, date, accession, period, detail }
    i = 0
    while i < count(fl["kind"])
        print(fl["kind"][i] + "  " + fl["date"][i] + "  " + fl["detail"][i])
        i = i + 1
    end while
```

## Step 6 — putting it together

That's the whole dossier: **session → facts + submissions → fundamentals trends →
forensic scorecard → flags**. `examples/edgar/scorecard.bas` does exactly this,
adds unknown-safe number formatting, and prints it as one clean screenful. Read it
top to bottom — it's the canonical worked example and a good template to fork.

A detail worth internalizing from that program: **every printed value goes through
a formatter that special-cases `unknown`** (prints `n/a`). Because any ingredient
can be missing, any derived number can be `unknown`; your presentation layer must
expect it. That one habit is the difference between an honest tool and one that
silently prints zeros.

---

# Part 2 — Cookbook

Recipes are grouped by library. The **acquisition, fundamentals, forensics, and
screener** recipes are fully runnable offline against the checked-in fixtures. For
**ownership, insiders, MD&A, and LLM**, the corresponding
`examples/*_test.bas` file is the verified, runnable template — each recipe points
to it.

Assume the session setup from Tutorial Step 1 (`e` is an offline edgar session).

## Acquisition (`edgar`)

**Resolve a ticker; fetch facts and submissions.**
```basic
cik   = edgar.cik(e, "AAPL")          ' -> "0000320193" (or unknown if unlisted)
facts = edgar.company_facts(e, cik)
subs  = edgar.submissions(e, cik)
```

**Only-new-since-last-time (polling).** `edgar.poll` is a pure function over a
submissions frame — no fetch — so it's safe to call in a loop and fully testable:
```basic
subs  = edgar.submissions(e, cik)
fresh = edgar.poll(subs, last_seen_accession)   ' rows newer than that accession
' remember the newest for next round:
last_seen_accession = subs["accession"][0]       ' submissions are newest-first
```

**Go live instead of offline.** Drop the `edgar.offline(...)` line and identify
yourself; SEC requires a real contact string and polite request spacing (the
library handles the spacing):
```basic
e = edgar.session()
e = edgar.identify(e, "Your Name <you@example.com>")
' no edgar.offline(...) -> real fetches, cached in the session's sqlite cache
```

**Count network calls** (useful in tests to prove the cache works):
`edgar.transport_count(e)`.

## Fundamentals (`fundamentals`)

**A single concept as a time series.** Concept names are the library's stable
vocabulary (`"revenue"`, `"net_income"`, `"operating_cash_flow"`, …; full list via
`fundamentals.concepts()`):
```basic
rev = fundamentals.series(facts, "revenue")   ' { end, start, value, fy, fp, form, accession, filed }
```

**Trends — FCF, margins, debt.** Each returns a frame of `end/fp/fy` plus its
columns:
```basic
fc = fundamentals.fcf(facts)      ' value
mg = fundamentals.margins(facts)  ' gross, operating, net (fractions of revenue)
db = fundamentals.debt(facts)     ' total, current, noncurrent, net
```

**Align several frames by fiscal-year end.** Build an `end -> value` map per
column, then look up (a bracket read of a missing key returns `unknown`, which is
what you want):
```basic
function by_end(frame, col)
    m = {}
    k = 0
    while k < count(frame["fp"])
        if frame["fp"][k] = "FY" then
            m[frame["end"][k]] = frame[col][k]
        end if
        k = k + 1
    end while
    return m
end function
' ... then: fcf_by = by_end(fc, "value")   ; gross_by = by_end(mg, "gross")
```

**Compare companies on the same metrics.** `fundamentals.compare(facts_list, metrics)`
takes a list of `facts` records and a list of concept keys and returns an
aligned cross-company frame.

## Forensics (`forensics`)

Every score is a per-fiscal-year scorecard frame; the **last row is the latest
year**. All propagate `unknown`.

```basic
acc = forensics.accruals(facts)    ' Sloan accrual ratio (the earnings-quality primitive)
pt  = forensics.piotroski(facts)   ' F-Score 0-9 (+ the nine booleans)
b   = forensics.beneish(facts)     ' M-Score (+ the eight indices) + flag (M > -1.78)
al  = forensics.altman(facts)      ' Z" distress score (+ ratios) + zone
di  = forensics.dilution(facts)    ' shares trend, SBC, buybacks, net
```

**Latest-year value helper** (forensic frames are FY-only, ascending):
```basic
function latest(frame, col)
    c = frame[col]
    n = count(c)
    if n = 0 then
        return unknown
    end if
    return c[n - 1]
end function
' ... forensics scorecard headline: latest(pt, "f_score"), latest(b, "mscore"), ...
```

**Altman with market equity** (the only price-dependent function). Supply a price
frame `{ date:[...], close:[...] }` whose dates line up with fiscal-year ends:
```basic
z = forensics.altman_classic(facts, { date: ["2025-09-27"], close: [250] })
```

**Score indices you already hold.** If you have the eight Beneish indices from
elsewhere, score them directly: `forensics.mscore({ dsri:.., gmi:.., ... })`.

**The composite red-flag frame** (facts + submissions):
```basic
fl = forensics.flags(facts, subs)               ' 90-day 5.02 cluster window
fl = forensics.flags_window(facts, subs, 45)    ' tighter cluster window
```

**Submissions-only events** (facts-free — this is what the monitor watches; it
classifies a filing stream without needing companyfacts):
```basic
ev = forensics.events(subs)                     ' NT / amendment / 4.01 / 4.02 / 5.02-cluster
ev = forensics.events_window(subs, 30)
```
`events` works on a **single incoming filing** too — pass a one-row submissions
frame and it tells you whether that filing is itself a flag.

## Screener — the whole market (`screener`)

The bulk tier ingests every filer's facts once into a local sqlite database, then
screening becomes local frame work. It consumes an **already-extracted directory**
of `CIK{10}.json` files (gBASIC has no unzip; `unzip companyfacts.zip -d some_dir`
first). Runnable offline against `examples/fixtures/edgar/screener_universe/`:

```basic
program main(args)
    load screener from "../stdlib/screener.bas"
    dir = "examples/fixtures/edgar/screener_universe"
    db  = "examples/tmp_screen.db"

    screener.score(db, dir)              ' compute+cache forensic scores (incremental, resumable)
    u = screener.scored(db)              ' { cik, name, latest_period, piotroski, mscore, mscore_flag, accrual_ratio, fcf }

    hits = screener.run(u, is_quality)   ' apply a predicate (see below)
    print("hits: " + string(count(hits["cik"])))

    rep = screener.unknown_report(db)    ' concept-map coverage report card
end program
```

**The predicate is a named function** (gBASIC has first-class functions but no
inline lambdas). `run` keeps rows where it returns exactly `true`; an `unknown`
excludes:
```basic
function is_quality(f)
    if is_unknown(f["piotroski"]) then
        return false
    end if
    if is_unknown(f["fcf"]) then
        return false
    end if
    if f["mscore_flag"] = true then
        return false
    end if
    return f["piotroski"] >= 7 and f["fcf"] > 0
end function
```

**Ingest is incremental and resumable.** Scoring a filer already in the database
is skipped, so a re-run continues where it left off. Use the `_limited` variants
to bound work per call (e.g. to checkpoint a multi-hour bulk run):
`screener.score_limited(db, dir, 500)`. (`screener.ingest` / `ingest_limited`
build the lighter `universe` table — cik/name/latest-period only — when you don't
need scores.)

Full worked example: `examples/screener_score_test.bas`.

## Ownership & insiders (`insiders`, `ownership`)

These parse XML filings (Form 4, 13F, 13D/G) via the `xml` module. The
offline-verifiable cores are driven directly over checked-in fixtures in the
example tests; the network conveniences (`insiders.transactions`) compose those
over an edgar session.

**Insider open-market buys, conviction, clusters** — template:
`examples/insiders_form4_test.bas`, `examples/insiders_cluster_test.bas`.
```basic
tx   = insiders.from_form4(xml.parse(body), filed_date)   ' one Form 4 -> transaction frame
buys = insiders.open_market_buys(tx)                       ' code-P only: real conviction buys
conv = insiders.conviction(buys)                           ' + prior_stake, conviction columns
cl   = insiders.cluster(buys, 5)                           ' group buys within 5 days; owners>=2 is the signal
```
Code **P** (open-market purchase with the insider's own money) is the only buy
code that means conviction; awards/exercises/tax-withholding are compensation
mechanics, and sales have a hundred innocent explanations.

**13F holdings and the quarter-over-quarter delta** — template:
`examples/ownership_13f_test.bas`.
```basic
h = ownership.report_13f(source_xml, filed_date)   ' holdings frame (value normalized to whole dollars)
d = ownership.delta(prior_h, current_h)            ' joined on cusip: new / exited / increased / decreased
```
13F data is up to ~4.5 months stale by construction (45-day deadline) — the signal
is the *diff between quarters*, not the absolute snapshot.

**13D/13G 5%-ownership stakes** — template: `examples/ownership_stakes_test.bas`.
The *choice of form* is the signal: **13G = passive, 13D = activist intent**; a
13G→13D switch means a big holder just turned hostile.
```basic
s  = ownership.stake(primary_doc_xml, filed_date)   ' one filing -> a stake record
es = ownership.stakes(list_of_stake_rows)           ' stack many into an events frame
```

## MD&A narrative + LLM panel (`mdna`, `llm`)

The judgment layer. `mdna` extracts and diffs the narrative (deterministic, no
LLM), then optionally runs an analyst panel over an LLM.

**Deterministic pre-pass** — template: `examples/mdna_sections_test.bas`,
`examples/mdna_prepass_test.bas`.
```basic
txt  = mdna.text(html)                       ' HTML -> clean text
sec  = mdna.sections(html)                   ' -> { mdna, risk_factors }
diff = mdna.risk_diff(prior_risk, cur_risk)  ' sentence-level year-over-year risk change
hs   = mdna.hedge_stats(cur_mdna)            ' hedging-language density (evasiveness proxy)
```

**The LLM client.** Build a model handle, then `ask` (one turn → text) or
`ask_json` (validated structured output). Runs offline against canned fixtures via
`llm.offline` — template: `examples/llm_adapter_test.bas`,
`examples/llm_ask_json_test.bas`.
```basic
m = llm.anthropic("claude-opus-4-8", api_key)       ' or llm.openai(...) / llm.local(base_url, model)
m = llm.offline(m, "examples/fixtures/llm")          ' read canned responses; no network
answer = llm.ask(m, "You are a terse analyst.", "Summarize the risk section.")
obj    = llm.ask_json(m, system_prompt, prompt)      ' -> record/list, or unknown if the model won't emit valid JSON
```
`ask_json` validates before parsing (so `decode` never raises) and does one
corrective retry; it returns `unknown` rather than throwing when the model can't
produce valid JSON.

**The analyst panel** — template: `examples/mdna_panel_test.bas`.
```basic
panelists = [ mdna.stance_bull(), mdna.stance_bear(), mdna.stance_forensic() ]
ev = mdna.evidence(prior_sections, cur_sections, scorecard)   ' bundle the deterministic findings
v  = mdna.panel(panelists, cur_sections, ev)                  ' one llm.ask_json per analyst -> verdict frame
dg = mdna.disagreement(v)                                     ' variance across the panel
rf = mdna.referee(frontier_model, v)                          ' a referee model reconciles them
```
A malformed analyst reply becomes an `ok=false` row — the panel continues rather
than crashing.

## The monitor — watchers over a live filing stream

The flagship example: a polling loop appends filings to a **watched** inbox, and
watcher bodies fire synchronously on arrival to raise alerts — classifying each
filing with `forensics.events` (no companyfacts needed on the hot path).

- **`examples/edgar/monitor.bas`** — the live version (network + `sleep`; run by
  hand).
- **`examples/edgar/monitor_harness_test.bas`** — the same watcher wiring driven
  by synthetic appends, fully offline. **This is the one to study and test.**
- **`examples/edgar/monitor_alerts.bas`** — the shared alerting policy both load.

The pattern in miniature:
```basic
board = { inbox: [], critical: [], warning: [] }
watch(board.inbox)
    ' on each arrival, classify the whole rolling history and route new events
    ev = monitor_alerts.classify(board.inbox)
    ' ... append to board.critical / board.warning ...
end watch
watch(board.critical)
    ' print the new critical alerts
end watch
append(board.inbox, a_filing_record)   ' fires the inbox watcher synchronously
```

---

# For testers: how the suite is verified

(This is the part most relevant if you're taking over testing.)

The whole EDGAR suite is tested **offline and deterministically** with
**golden files**: a program plus a sibling `.out` holding its expected stdout,
compared verbatim.

- **Add a positive test:** write `examples/foo_test.bas`, capture its output to
  `examples/foo_test.out`, and **add the filename to the hardcoded list in
  `tests/run_examples.sh`** (the runner does not auto-discover). Negative tests
  pair a `.bas` with a `.err` (expected stderr) and register in
  `tests/run_negative.sh`.
- **Run the suites:**
  ```sh
  ./tests/run_examples.sh    # positive goldens (rebuilds first)
  ./tests/run_negative.sh    # error/diagnostic goldens
  ```
- **Determinism rules that keep goldens stable:**
  - Drive everything from fixtures (`edgar.offline`, `llm.offline`), never the
    network.
  - Only stdout is compared for positive tests, so a stderr warning won't fail a
    golden — but don't rely on that; keep output clean.
  - Avoid printing raw huge numbers (the number formatter can vary in the last
    digits); format money in billions or round to a fixed number of decimals, as
    `scorecard.bas` does.
  - Clean up any scratch files (sqlite caches/dbs) your test creates, and add
    their paths to the runner's cleanup list.
- **Module tests skip, not fail, when a native dependency is absent** (sqlite,
  libcurl, etc.), so the suite stays green on a minimal build.

Two acceptance steps are **live and run by hand** (not in the suite): the
full-market screener run against the real multi-gigabyte `companyfacts.zip`, and
the live `monitor.bas` against the SEC stream. Everything else is offline.

---

## Where to look next

- **[EDGAR Suite Reference](edgar_reference.md)** — every function, signature, and
  return shape.
- **`docs/edgar_design.md`** — the design rationale (domain primer, why each
  model, the applicability caveats).
- **`examples/edgar/scorecard.bas`** — the canonical end-to-end program.
- **`docs/PROGRESS.md`** — the build ledger (what shipped, with evidence).
