# EDGAR Suite — PROGRESS

State tracking for `docs/edgar_suite_development_plan.md`. One entry per WP,
**newest first within its track**. Statuses:

```
todo | in-progress | blocked(<reason>) | done-claimed | verified
```

`verified` is written **only by Matthew**, after independently re-running the
decisive commands (plan §3). Each `done-claimed` entry carries a full evidence
block: commands run, verbatim output tail, files changed, deviations,
observations.

---

## Track 0 — Bootstrap

### WP-0 — done-claimed — 2026-07-02
- commands run:
    for d in src/modules examples/edgar examples/fixtures/edgar examples/fixtures/llm tools; do mkdir -p "$d"; touch "$d/.gitkeep"; done
    git status --short
- output tail (verbatim, unedited):
     M CLAUDE.md
    ?? docs/PROGRESS.md
    ?? docs/edgar_design.md
    ?? docs/edgar_suite_development_plan.md
    ?? docs/llm_design.md
    ?? docs/xml_design.md
    ?? examples/edgar/
    ?? examples/fixtures/
    ?? src/modules/
    ?? tools/
- files added/changed: docs/PROGRESS.md, CLAUDE.md, src/modules/.gitkeep,
    examples/edgar/.gitkeep, examples/fixtures/edgar/.gitkeep,
    examples/fixtures/llm/.gitkeep, tools/.gitkeep
- deviations from plan: none. (docs/ and stdlib/ already existed; examples/
    already existed — only the missing §2 subdirectories were created.)
- observations: none of the EDGAR-suite product code existed prior to this WP;
    stdlib/ held only crypto, dates, frame, matrix, stats. This is the first
    WP of the plan to run.

### WP-CORE-1 — done-claimed — 2026-07-02
- pre-existing state: `env(name)` already existed (src/eval.c), already
  registered (src/builtins.c), already tested (examples/env_builtin_test.* +
  tests/negative_env_arity/type), and already documented (reference.md
  §Environment). Per this WP's Notes ("if either builtin already exists,
  record that and do only the missing one"), `env` was left as-is and only
  `sleep` was built. `sleep` is not a lexer/parser keyword (no §5 modifier
  collision), so no `blocked` condition.
- commands run:
    make 2>&1 | tail -1
    ./tests/run_examples.sh 2>&1 | tail -1 ; grep -c '^PASS' (examples) ; grep -c '^FAIL'
    ./tests/run_negative.sh 2>&1 | tail -1 ; grep -c '^PASS' (negative) ; grep -c '^FAIL'
    bash tests/run_core.sh
    valgrind --leak-check=full ./gbasic examples/sleep_test.bas 2>&1 | grep -E 'ERROR SUMMARY|leaks are possible'
    valgrind --leak-check=full ./gbasic tests/negative_sleep_negative.bas 2>&1 | grep 'ERROR SUMMARY'
- output tail (verbatim, unedited):
    examples PASS count: 155
    examples FAIL count: 0
    sleep_test result: PASS examples/sleep_test.bas
    negative PASS count: 198
    negative FAIL count: 0
    PASS tests/negative_sleep_arity.bas
    PASS tests/negative_sleep_type.bas
    PASS tests/negative_sleep_negative.bas
      sleep(0.30): elapsed 309ms (requested 300ms)
    PASS sleep elapsed >= requested
    PASS sleep(0) returns 0
    PASS env set -> value, unset -> unknown
    core suite: 3 passed / 0 failed
    ==477762== All heap blocks were freed -- no leaks are possible
    ==477762== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
    ==477708== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
- files added/changed:
    src/eval.c (new `sleep` builtin, after the `env` block),
    src/builtins.c (registered "sleep"),
    examples/sleep_test.bas, examples/sleep_test.out,
    tests/negative_sleep_arity.bas/.err, tests/negative_sleep_type.bas/.err,
    tests/negative_sleep_negative.bas/.err,
    tests/run_core.sh (new timing/seam harness),
    tests/run_examples.sh (added sleep_test.bas),
    tests/run_negative.sh (added 3 sleep negatives),
    docs/reference.md (new §Timing entry for `sleep`)
- deviations from plan: none material. `env` was already complete (built by
  earlier unrelated work), so this WP added only `sleep` — recorded above per
  the WP Notes rather than duplicating env.
- observations:
    * `sleep` returns the requested seconds (mirrors `seed`, which returns its
      input) — chosen for consistency; recorded here since the design didn't
      specify a return value.
    * Negative and non-numeric args raise (`sleep expects a non-negative
      number` / `sleep expects a number`), matching the strict-typing
      convention of neighboring builtins.
    * The `sleep` timing assertion (elapsed >= requested) cannot be a golden
      string, so it lives in tests/run_core.sh, not run_examples.sh.
    * Pre-existing build warnings remain (`gui_optional_int_field` /
      `gui_optional_bool_field` unused) — unrelated to this WP, untouched.

---

## Track A — EDGAR numeric path (the fast path)

### WP-EDG-1 — done-claimed — 2026-07-02
- scope: CC writes the capture script; **Matthew runs it** live (his UA
  identity, his rate compliance) and checks in the resulting fixtures. This WP
  delivers the script + a recorded dry run only — no network was used.
- commands run:
    bash -n tools/edgar_capture.sh
    tools/edgar_capture.sh --print-only AAPL JPM        # (EDGAR_IDENT unset)
    EDGAR_IDENT unset + real run  -> must exit nonzero
    tools/edgar_capture.sh --print-only                 # no-args default path
    offline resolve_cik check against a synthetic company_tickers.json
- output tail (verbatim, unedited):
    bash -n: OK
    [1] ticker -> CIK map
      fetch  https://www.sec.gov/files/company_tickers.json
             -> examples/fixtures/edgar/company_tickers.json
    [2] AAPL
      fetch  https://data.sec.gov/submissions/CIK{10}.json
             -> examples/fixtures/edgar/submissions_CIK{10}.json
      fetch  https://data.sec.gov/api/xbrl/companyfacts/CIK{10}.json
             -> examples/fixtures/edgar/companyfacts_CIK{10}.json
    [3] JPM  (same two endpoints)
    edgar_capture: dry run complete — nothing fetched or written.
    --- identity gate ---
    exited nonzero (1) — good
    edgar_capture: FATAL — EDGAR_IDENT is unset.
    --- offline resolve_cik (synthetic map, no network) ---
    jq   AAPL -> CIK0000320193
    jq   JPM  -> CIK0000019617
    py3  AAPL -> CIK0000320193
- files added/changed: tools/edgar_capture.sh (new; executable)
- deviations from plan: none. The small-cap ticker is supplied by Matthew as a
    CLI argument at run time (the plan reserves that pick for him); AAPL and JPM
    are the built-in defaults. Endpoints per edgar_design.md §2:
    www.sec.gov/files/company_tickers.json, data.sec.gov/submissions/CIK{10}.json,
    data.sec.gov/api/xbrl/companyfacts/CIK{10}.json. Rate = 1 req/s (under the
    10/s ceiling). Ticker->CIK resolves via jq, python3 fallback.
- observations:
    * MANIFEST.md (URL -> file -> capture date) is written during the real run,
      not the dry run, so it is not present yet — it appears after Matthew runs
      the capture.
    * TO RUN (Matthew): `EDGAR_IDENT="<name> <email>" tools/edgar_capture.sh
      AAPL JPM <your-smallcap-ticker>` — then check in
      examples/fixtures/edgar/*.json and MANIFEST.md. Fixture presence is the
      Matthew-verified half of this WP.
    * CAPTURE EXECUTED 2026-07-02 at Matthew's explicit request (identity
      `Matthew C. Tedder matthewct@gmail.com`, small-cap = CROX). 7 files, all
      valid JSON, checked into examples/fixtures/edgar/ with MANIFEST.md:
        company_tickers.json (797 KB)
        AAPL  CIK0000320193 — submissions 164 KB, companyfacts 3.7 MB (Apple Inc.)
        JPM   CIK0000019617 — submissions 4.5 MB, companyfacts 7.7 MB (JPMORGAN CHASE & CO)
        CROX  CIK0001334036 — submissions 163 KB, companyfacts 3.5 MB (Crocs, Inc.)
      `verified` remains Matthew's call, but the fixtures are now present so
      WP-EDG-2 is unblocked.

### WP-EDG-2 — done-claimed — 2026-07-02
- (was briefly blocked; RESOLVED by Matthew choosing the explicit-handle API —
  see resolution + evidence at the end of this entry. Diagnostic history kept.)
- The design (edgar_design.md §4.1) specifies a STATELESS-CALL public API that
  relies on hidden, persistent module state:
      edgar.identify("...")        ' sets identity once
      cik  = edgar.cik("AAPL")     ' uses that identity implicitly
      subs = edgar.submissions(cik)
  edgar also needs to remember: offline dir (edgar.offline(dir)), cache db path,
  and a transport call counter (required by the cache-hit test).
- gBASIC .bas libraries CANNOT hold mutable module/global state. Verified by
  probe (temp files, not committed):
    * library top-level assignments never execute on load — only function defs
      register. (PROBE C: a top-level print() in the library body never fired.)
    * functions are references, not closures — a loaded/top-level function
      cannot see a program-global variable (`undefined variable`). (PROBE A/B)
    * records pass BY VALUE — a function mutating a record parameter does not
      affect the caller's copy. (PROBE D: r.x=99 inside; caller still 1.)
    * `this` in methods is read-only (existing negative_this_read_only test), so
      object-with-methods gives no mutable state either.
    * the ONLY working persistence idiom is an explicit handle threaded in and
      returned out: `e = lib.session(); e = lib.identify(e, s); lib.use(e)`.
      (PROBE E: works.)
- Confirmed a .bas library CAN `load sqlite`/`webclient` and call them qualified
  (PROBE: returned 42), and that `library edgar` + plain fn names yields the
  qualified `edgar.cik(...)` call shape callers expect — so the ONLY blocker is
  the hidden-state assumption, nothing else.
- PRECISE QUESTION FOR MATTHEW — how should edgar hold identity / offline-dir /
  cache-path / transport-counter, given .bas libs are stateless?
    (1) Explicit session handle (pure .bas, recommended): change §4.1 signatures
        to thread a handle — `e = edgar.session()`, `e = edgar.identify(e, s)`,
        `edgar.cik(e, ticker)`, `edgar.submissions(e, cik)`. Idiomatic (data in,
        data out, like frames). Changes the documented public API + every
        downstream Track A/C caller.
    (2) Compiled module (edgar in src/eval.c behind `edgar.` dispatch, like
        sqlite/pg/webclient which DO hold state): preserves the exact stateless
        §4.1 API but moves edgar out of stdlib/edgar.bas into C (contradicts §2
        repo layout) and breaks the "compositions in gBASIC" rule for the family.
    (3) On-disk config in the sqlite cache db: keeps the §4.1 API but leaks
        identity/offline across programs sharing the cache and makes offline a
        persistent disk setting — fragile for tests. Not recommended.
- RESOLUTION: Matthew chose option (1) explicit session handle (2026-07-02).
  Design doc edgar_design.md §4.1 updated to the handle API. Built
  stdlib/edgar.bas with: session()/identify()/offline()/cache() (config in the
  returned handle); the single fetch gate (cache-first sqlite, then transport);
  transport = fixture read (offline) or webclient.request with UA header + 100ms
  spacing (online); sqlite cache (immutable + TTL-index columns; default
  ~/.gbasic/edgar.db, overridable); a transport counter stored in the cache DB
  (edgar.transport_count(e), the cache-hit test hook); cik(e,ticker) ->
  10-digit padded CIK string or unknown; submissions(e,cik) -> filing-index
  frame (form/filed/accession/period/primary_document/items).
- commands run:
    ./tests/run_examples.sh   (make clean+build, then goldens)
    ./tests/run_negative.sh
    (offline-only against examples/fixtures/edgar; NO network)
- output tail (verbatim, unedited):
    examples PASS: 156  FAIL: 0
    PASS examples/edgar_offline_test.bas
    negative PASS: 200  FAIL: 0
    PASS tests/negative_edgar_unset_identify.bas
    PASS tests/negative_edgar_offline_miss.bas
  positive golden (examples/edgar_offline_test.out) proves the acquisition core:
    aapl 0000320193 / jpm 0000019617 / crox 0001334036 / bogus_unknown true
    cols form,filed,accession,period,primary_document,items
    transport_after_fetches 2 / transport_after_cachehit 2 / cachehit_no_transport true
- Done-when checklist:
    * suite green over fixtures — YES (156 positive / 200 negative)
    * unset-identify raises — YES (negative_edgar_unset_identify)
    * offline-miss raises — YES (negative_edgar_offline_miss)
    * cache-hit touches no transport (transport counter) — YES
      (transport stays 2 across the repeated submissions call)
    * valgrind — N/A (no new C code; edgar.bas is pure gBASIC)
- files added/changed:
    stdlib/edgar.bas (new),
    examples/edgar_offline_test.bas/.out (new positive golden),
    tests/negative_edgar_unset_identify.bas/.err (new),
    tests/negative_edgar_offline_miss.bas/.err (new),
    tests/run_examples.sh (registered example + tmp-db cleanup),
    tests/run_negative.sh (registered 2 negatives + tmp-db cleanup trap),
    docs/edgar_design.md (§4.1 rewritten to the handle API)
- deviations from plan: public API changed from stateless calls to an explicit
  session handle (edgar.session()/identify(e,..)/cik(e,..)/submissions(e,..)) —
  authorized by Matthew after the blocked question above. edgar.poll signature
  in §4.1 shown as pure (poll(subs,last_seen)); actual poll is WP-EDG-3.
- observations (out of scope; noted, not acted on):
    * gBASIC quirk: an `on error resume next` ... `on error stop` block causes
      the FOLLOWING assignment to be dropped ("undefined variable"). Worked
      around by probing dir existence with a file reference instead. Worth a
      core bug ticket.
    * `exists()` accepts only FILE references (directory references raise
      "expects a file reference"); a file reference to a directory PATH does
      report existence correctly — used that.
    * `contains()` is array-only (no string-substring form); string not-equal is
      `!=` (`<>` is a parse error). Endpoint detection therefore splits the URL
      and uses array contains() on the path segments.
    * The two negative .err goldens embed edgar.bas-internal line numbers
      (143/150) because gBASIC attributes a library `error`'s line to the ENTRY
      file's path. So edgar.bas line layout is now a regression anchor for those
      two goldens — regenerate the .err files if those `error` lines move.

### WP-EDG-3 — done-claimed — 2026-07-02
- Completed the acquisition surface on top of WP-EDG-2, all offline (no network):
    * edgar.company_facts(e, cik) -> decoded companyfacts record
      ({cik, entityName, facts}); TTL-cached like the other index/facts endpoints.
    * edgar.document(e, cik, accession, filename) -> filing document body;
      immutable -> cached forever. URL built as
      .../Archives/edgar/data/{cik-no-zeros}/{accession-no-dashes}/{filename}.
    * edgar.poll(subs, last_seen) -> new-filings frame. PURE function over a
      submissions frame (no fetch, offline-testable): returns filings newer than
      the last_seen accession (those ahead of it in EDGAR's newest-first order);
      unknown/absent last_seen => all new.
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
- output tail (verbatim, unedited):
    examples PASS: 157  FAIL: 0
    PASS examples/edgar_offline_test.bas
    PASS examples/edgar_facts_poll_test.bas
    negative PASS: 201  FAIL: 0
    PASS tests/negative_edgar_unset_identify.bas
    PASS tests/negative_edgar_offline_miss.bas
    PASS tests/negative_edgar_document_miss.bas
  poll golden (examples/edgar_facts_poll_test.out), last_seen = the 6th-newest
  CROX filing (subs.accession[5]):
    entity CROCS, INC. / has_facts true
    new_count 5 / newest_matches true / excludes_seen true
    unknown_all_new true / newest_none_new true
- Done-when checklist:
    * suite green (counts) — YES (157 positive / 201 negative)
    * poll golden from a fixture with a chosen last_seen split — YES (index-5
      split yields exactly the 5 newer filings; boundary cases covered)
- files added/changed:
    stdlib/edgar.bas (added company_facts/document/poll/_take_cols; extended
      _url_to_fixture for Archives document URLs),
    examples/edgar_facts_poll_test.bas/.out (new positive golden),
    tests/negative_edgar_document_miss.bas/.err (new),
    tests/run_examples.sh (registered example + tmp-db cleanup),
    tests/run_negative.sh (registered negative + tmp-db cleanup)
- deviations from plan:
    * poll signature is poll(subs, last_seen) (pure over a submissions frame),
      not poll(watchlist, last_seen). The WP's own "pure function over
      submissions, offline-testable" clause requires the submissions frame as
      input (a multi-CIK watchlist poll would have to fetch, i.e. not pure), so
      this is the only reading consistent with the WP text. Matches the §4.1
      update from WP-EDG-2. `new` is a reserved word (PBI constructor) so
      callers must not name the result `new` (the §4.1 example is illustrative).
    * edgar.document POSITIVE path is not golden-tested: WP-EDG-1 captured only
      company_tickers/submissions/companyfacts fixtures, no filing documents.
      Its URL construction and offline seam are verified via the document
      offline-miss negative test (URL + expected fixture name asserted). A
      positive document golden awaits a captured document fixture (note for a
      later WP / capture pass).
- observations:
    * Adding functions to edgar.bas shifted the `error` line numbers, so the two
      pre-existing edgar negative .err goldens were regenerated: identity is now
      edgar.bas:147, offline-miss (submissions AND document) is edgar.bas:154.
      These remain the regression anchor noted in WP-EDG-2.
### WP-EDG-4 — done-claimed — 2026-07-02
- Built stdlib/fundamentals.bas: the concept map (24 concepts covering the §4.2
  v2 list, each a concept -> ordered us-gaap/dei tag fallback chain) and
  series(facts, concept) (deduped fact frame) + series_as_filed(facts, concept)
  (originals kept). Tag chains verified against the 3 companyfacts fixtures.
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
- output tail (verbatim, unedited) — examples/fundamentals_series_test.out:
    revenue AAPL rows=64 fy_end=2025-09-27 fy_val=416161000000
    revenue JPM rows=53 fy_end=2025-12-31 fy_val=182447000000
    revenue CROX rows=88 fy_end=2025-12-31 fy_val=4040647000
    net_income AAPL rows=168 fy_end=2025-09-27 fy_val=112010000000
    net_income JPM rows=114 fy_end=2025-12-31 fy_val=57048000000
    net_income CROX rows=146 fy_end=2025-12-31 fy_val=-81198000
    total_assets AAPL rows=122 fy_end=2025-09-27 fy_val=359241000000
    total_assets JPM rows=120 fy_end=2025-12-31 fy_val=4424900000000
    total_assets CROX rows=106 fy_end=2025-12-31 fy_val=4174750000
    dedup rows_deduped=168 rows_as_filed=334
    dedup fy2007_val=3495000000 accn=0001193125-10-012091
    jpm operating_income unknown=true
    unmapped concept unknown=true
  suite totals: examples PASS: 158 FAIL: 0 ; negative PASS: 201 FAIL: 0
- Done-when checklist:
    * >=3 concepts x 3 filers (9 series) green — YES (revenue/net_income/
      total_assets x AAPL/JPM/CROX)
    * dedup test, period in 10-K and 10-Q, latest-accession wins — YES. AAPL
      net_income FY2007 (end 2007-09-29) is filed twice; the later filing
      (2010-01-25, accn ..10-012091, val 3495000000, a restatement of the
      original 3496000000) wins. as_filed=334 collapses to deduped=168.
    * unmapped-concept -> unknown — YES (not_a_real_concept). PLUS the notes'
      "bank lacks an industrial tag" case: JPM operating_income -> unknown.
- HAND-CHECK ANCHORS (Matthew, vs the 10-Ks): the 9 latest-FY dollar values
  above. NOTE CROX net_income latest-FY reads -81198000 (a LOSS) — please verify
  against the Crocs 10-K; if wrong it signals a concept-map issue for CROX, not
  a code bug.
- files added/changed:
    stdlib/fundamentals.bas (new),
    examples/fundamentals_series_test.bas/.out (new golden),
    tests/run_examples.sh (registered example)
- deviations from plan:
    * series_as_filed is a SEPARATE function, not series(facts, concept,
      as_filed): gBASIC user functions are arity-strict (no optional args).
    * dedup key is (start, end, fp), not the design's literal (end, fp): `start`
      separates duration facts (a quarter vs a year-to-date at the same end/fp);
      instant balance-sheet facts have start="".
    * "latest accession wins" is implemented as latest `filed` date (accession
      strings are NOT globally ordered — companyfacts mixes filing agents, e.g.
      AAPL facts filed under agent 0001193125), accession as the tiebreaker.
    * the frame carries two extra columns beyond §3's illustrative
      {end,value,fy,fp,form,accession}: `start` and `filed` (needed for correct
      dedup and for as_filed restatement-delta work).
- observations:
    * fact-object fields are read with bracket access (f["end"]) because `end`
      is a reserved word; the series frame's period column is likewise
      series["end"] (dot access would be a parse error). Downstream WP-EDG-5/6
      consumers must use bracket access for reserved-word columns.
### WP-EDG-5 — done-claimed — 2026-07-02
- Added the four derived metrics to stdlib/fundamentals.bas, composing over
  series and aligning by period key (end, fp) — exact across duration (flow) and
  instant (balance-sheet) facts since a fiscal year-end shares that key. All
  outputs are fact frames; a missing ingredient propagates as `unknown` for that
  period (never a guess).
    * fcf(facts)     -> per-period operating_cash_flow - capex
    * debt(facts)    -> total / current / noncurrent / net (net = total - cash)
    * margins(facts) -> gross / operating / net (each / revenue)
    * ratios(facts)  -> interest_coverage, current_ratio, net_debt_ebitda, fcf_conversion
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
    (cross-check) jq FCF anchor from the AAPL companyfacts fixture
- output tail (verbatim, unedited) — examples/fundamentals_derived_test.out:
    fcf AAPL 2025-09-27: ocf=111482000000 capex=12715000000 fcf=98767000000
    margins AAPL 2025-09-27: gross=0.4691 op=0.3197 net=0.2692
    debt AAPL 2025-09-27: total=90678000000 noncurrent=78328000000 net=54744000000
    ratios AAPL 2025-09-27: coverage=unknown current_ratio=0.8933 nd_ebitda=0.3782 fcf_conv=0.8818
    fcf JPM 2025-12-31: unknown
    margins JPM 2025-12-31: gross=unknown op=unknown net=0.3127
  jq cross-check: jq ocf=111482000000 capex=12715000000 fcf=98767000000  (exact match)
  suite totals: examples PASS: 159 FAIL: 0 ; negative PASS: 201 FAIL: 0
- Done-when checklist:
    * goldens green (counts) — YES (159 positive)
    * HAND-CHECK ANCHOR (Matthew, vs the actual AAPL 10-K, FY ending 2025-09-27):
        operating cash flow = 111,482,000,000
        capital expenditure =  12,715,000,000
        FCF (ocf - capex)   =  98,767,000,000
      (library value verified against the raw fixture via jq — exact match.)
- files added/changed:
    stdlib/fundamentals.bas (added fcf/debt/margins/ratios + alignment helpers),
    examples/fundamentals_derived_test.bas/.out (new golden),
    tests/run_examples.sh (registered example)
- deviations from plan:
    * period alignment is by (end, fp) (drops `start`) so instant balance-sheet
      facts align with flow facts at the same fiscal period end.
    * debt: an absent current portion of long-term debt is treated as 0 for the
      `total` (so total = noncurrent when no current portion is filed) but is
      reported as `unknown` in its own `current` column.
    * each metric uses a sensible driver series for its period skeleton
      (ocf->fcf, revenue->margins, long_term_debt->debt, net_income->ratios).
- observations:
    * CORRECTNESS NOTE (not a bug): AAPL interest_coverage reads `unknown` for
      recent FYs because Apple stopped filing a separate InterestExpense line
      after FY2023 — verified in the fixture. NA propagation is working; the
      value is honestly absent.
    * gBASIC QUIRK worth a core ticket: round(unknown, n) returns 0 (and
      is_unknown() of the result is false) — i.e. round() MASKS unknown as zero.
      Any display/formatting of possibly-unknown values must guard with
      is_unknown() BEFORE round() (the golden's fmt/fmt4 helpers do this). This
      could silently corrupt NA data in arithmetic-then-round pipelines.
    * JPM (bank) correctly yields fcf=unknown (no capex) and gross/operating
      margins=unknown (no industrial tags), while net margin computes (0.3127).
### WP-EDG-6 — done-claimed — 2026-07-02
- Added fundamentals.compare (peer frame, one row per company, latest fiscal-year
  value per metric, unknown where absent) and the worked example
  examples/edgar/fundamentals_demo.bas — the whole Track A numeric pipeline on one
  page (edgar offline -> company_facts -> fundamentals.compare -> printed table).
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
- output tail (verbatim, unedited) — examples/edgar/fundamentals_demo.out:
    Peer comparison — latest fiscal year (offline fixtures)
    Company                  Revenue$B   NetInc$B      FCF$B  NetMargin
    Apple Inc.                  416.16     112.01      98.77      26.9%
    JPMORGAN CHASE & CO         182.45      57.05        n/a      31.3%
    CROCS, INC.                   4.04      -0.08       0.66        -2%
  suite totals: examples PASS: 160 FAIL: 0 ; negative PASS: 201 FAIL: 0
- Done-when checklist:
    * suite green — YES (160 positive / 201 negative)
    * demo output golden recorded — YES (examples/edgar/fundamentals_demo.out).
      Note JPM FCF = n/a: a bank files no capex, so fcf propagates unknown into
      the peer table — the NA policy visible end-to-end.
- files added/changed:
    stdlib/fundamentals.bas (added compare + _metric_latest + _latest_fy),
    examples/edgar/fundamentals_demo.bas/.out (new worked example + golden),
    tests/run_examples.sh (registered demo + tmp-db cleanup)
- deviations from plan:
    * compare(facts_list, metrics), not compare(ciks, metrics): it takes a list
      of already-decoded companyfacts records (pure over facts, matching every
      other fundamentals function; keeps fundamentals decoupled from edgar). The
      demo shows the intended ergonomics — edgar fetches per ticker, then the
      facts list is handed to compare.
    * metric keys are base concepts (revenue, net_income, ...) OR named derived
      scalars: fcf, net_debt, gross_margin, operating_margin, net_margin,
      current_ratio (the design's "margins" is split into the three named margin
      scalars so each is a single comparable cell).
- observations:
    * NAME COLLISION: fundamentals.compare shares a name with the core builtin
      `compare`, so loading fundamentals prints a one-line stderr warning
      ("unqualified calls use the built-in"). QUALIFIED calls (fundamentals.
      compare) resolve to the library function correctly — the demo output
      proves it — and the warning is stderr so it does not affect goldens. If
      the warning noise is unwanted, renaming (e.g. to `peers`) would silence it,
      but that deviates from the design's name — flagged for Matthew.
- MILESTONE: Track A EDGAR-numeric path (WP-EDG-1..6) is now complete. The
  fast-path forensics track (WP-FOR-1..4 -> WP-DEMO-1, the Adrian demo) is
  unblocked; WP-FOR-1 depends on WP-EDG-5 (done).
### WP-FOR-1 — done-claimed — 2026-07-02
- Built stdlib/forensics.bas: accruals (Sloan ratio) + piotroski (F-Score),
  composing over fundamentals.series with per-fiscal-year YoY alignment. Both
  return scorecard frames (one row per FY): period columns end + prior_end,
  components exposed, headline (accrual_ratio / f_score), unknown on any missing
  ingredient. This establishes the scorecard-frame shape for WP-FOR-2..4.
    * accruals(facts): (net_income - operating_cash_flow) / avg total assets,
      avg = (assets_t + assets_{t-1})/2.
    * piotroski(facts): the nine binary tests (f_roa, f_cfo, f_droa, f_accrual,
      f_dlever, f_dliquid, f_shares, f_dmargin, f_dturn) + f_score (sum 0-9,
      unknown if any component unknown).
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
- output tail (verbatim, unedited) — examples/forensics_scores_test.out:
    accruals AAPL 2025-09-27 prior=2024-09-28
      net_income=112010000000 operating_cash_flow=111482000000 avg_total_assets=362110500000
      accrual_ratio=0.001458
    piotroski AAPL 2025-09-27 prior=2024-09-28
      f_roa=1 f_cfo=1 f_droa=1 f_accrual=0
      f_dlever=1 f_dliquid=1 f_shares=1
      f_dmargin=1 f_dturn=1
      f_score=8
      ingredients cur 2025-09-27: ni=112010000000 cfo=111482000000 ta=359241000000 ltd=78328000000
        ca=147957000000 cl=165631000000 shares=14773260000 gp=195201000000 rev=416161000000
      ingredients prior 2024-09-28: ni=93736000000 cfo=118254000000 ta=364980000000 ltd=85750000000
        ca=152987000000 cl=176392000000 shares=15116786000 gp=180683000000 rev=391035000000
    piotroski JPM 2025-12-31: f_cfo=0 f_dmargin=unknown f_dturn=0 f_score=unknown
  suite totals: examples PASS: 161 FAIL: 0 ; negative PASS: 201 FAIL: 0
- Done-when checklist:
    * goldens green — YES (161 positive)
    * HAND-CHECK ANCHOR (Matthew, vs the AAPL 10-K, FY ending 2025-09-27 vs
      2024-09-28) — the nine Piotroski components with ingredients:
        f_roa=1     ROA>0            : ni 112010M / ta 359241M > 0
        f_cfo=1     CFO>0            : cfo 111482M > 0
        f_droa=1    dROA>0           : 112010/359241 (.3118) > 93736/364980 (.2568)
        f_accrual=0 CFO>NI           : 111482M > 112010M is FALSE
        f_dlever=1  leverage down    : 78328/359241 (.2181) < 85750/364980 (.2349)
        f_dliquid=1 curr ratio up    : 147957/165631 (.8933) > 152987/176392 (.8673)
        f_shares=1  no new shares    : 14,773,260,000 <= 15,116,786,000 (buybacks)
        f_dmargin=1 gross margin up  : 195201/416161 (.4691) > 180683/391035 (.4621)
        f_dturn=1   turnover up      : 416161/359241 (1.159) > 391035/364980 (1.071)
        f_score = 8 (all but f_accrual)
      accruals anchor: (112010M - 111482M) / 362110.5M = 0.001458.
- files added/changed:
    stdlib/forensics.bas (new),
    examples/forensics_scores_test.bas/.out (new golden),
    tests/run_examples.sh (registered example)
- deviations from plan (standard-form choices, per the WP Notes):
    * ROA uses YEAR-END total assets, not Piotroski's original beginning-of-year
      denominator (a common variant; the sign and YoY-direction tests are robust
      to the choice — verified above).
    * f_score is `unknown` if ANY of the nine components is unknown (strict NA:
      a partial F-Score is not an F-Score) — e.g. JPM (a bank, no gross_profit)
      -> f_dmargin unknown -> f_score unknown, while computable components (f_cfo,
      f_dturn, ...) still report.
    * f_shares uses whichever shares_outstanding tag the concept map resolves
      (CommonStockSharesOutstanding first); "no new shares" = shares_t <=
      shares_{t-1}, the published definition.
    * forensics private helpers _get/_sub were renamed _fget/_fsub to avoid
      shadowing fundamentals' identically-named private helpers (forensics loads
      fundamentals) — eliminated the override warnings.
- observations:
    * A benign stderr warning remains: loading fundamentals warns that its public
      `compare` shares a name with the core builtin (see WP-EDG-6). Inherited by
      any consumer of fundamentals (incl. forensics); qualified calls are correct
      and goldens are unaffected.
    * All nine AAPL components were re-derived by hand from the ingredients and
      match — the scorecard math is sound.
- MILESTONE: first two forensic scores shipped; scorecard-frame conventions
  established for WP-FOR-2 (Beneish) / WP-FOR-3 (Altman+dilution) / WP-FOR-4
  (flags).
### WP-FOR-2 — done-claimed — 2026-07-04
- UNBLOCKED: Matthew approved the reference on 2026-07-04. Approved primary
  reference = the StableBread Boeing FY2023 published worked example (raw two-year
  statements -> 8 indices -> M-Score), with the WallStreetMojo composite example as
  a secondary cross-check. Coefficients cross-checked against the 1999 paper and
  hard-coded (4.679 for TATA — NOT the 4.697 typo that appears in MyAccountingCourse,
  exactly the trap the WP Notes warn about).
- Built forensics.beneish(facts) in stdlib/forensics.bas:
    * eight indices DSRI, GMI, AQI, SGI, DEPI, SGAI, LVGI, TATA per fiscal-year
      pair, each a year-over-year ratio built from the §4.2 concept map via the
      WP-FOR-1 helpers (_fy_index/_annual_ends/_fget/_ratio/_fsub/_avg, plus new
      _fadd/_over); composite mscore + flag (M > -1.78) columns; scorecard-frame
      shape (end, prior_end, one column per index, headline, flag).
    * mscore(indices) — public composite from a record of the 8 indices, weights
      Beneish (1999); beneish() computes each index then calls mscore, DRY.
    * unknown propagation per component: a missing ingredient makes that index --
      and therefore mscore/flag -- unknown (the NA helpers already short-circuit;
      _zscore returns unknown if any index is unknown).
- Concept-map addition (fundamentals.bas): added ppe_net ->
  ["PropertyPlantAndEquipmentNet"] — net PP&E is required by AQI and DEPI and was
  the only Beneish ingredient the §4.2 map lacked. Additive; existing
  fundamentals/forensics goldens unchanged.
- Fixture: examples/fixtures/edgar/beneish_boeing_fixture.json — a minimal
  companyfacts-shaped record carrying the APPROVED Boeing FY2022/FY2023 raw inputs
  (built by a one-off python step from the StableBread numbers; $M magnitudes, and
  the indices are scale-invariant so units are immaterial).
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/forensics_beneish_test.out:
    == Boeing FY2023 (published reference) ==
    period 2022-12-31 -> 2023-12-31
    DSRI=0.901 GMI=0.534 AQI=1.004 SGI=1.168
    DEPI=1.063 SGAI=1.057 LVGI=1.008 TATA=-0.06
    MSCORE=-2.95 flag(manipulator)=false
    == WallStreetMojo composite cross-check ==
    MSCORE=-2.53
    == unknown propagation (no PP&E) ==
    DSRI known? true (PPE-free index still computes)
    AQI unknown? true DEPI unknown? true
    MSCORE unknown? true flag unknown? true
    mscore(one unknown index) unknown? true
  suite totals: examples PASS: 180 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * golden against a published worked example (approved reference) — YES. Boeing
      FY2023: all eight indices match StableBread to 3 dp (DSRI 0.901, GMI 0.534,
      AQI 1.004, SGI 1.168, DEPI 1.063, SGAI 1.057, LVGI 1.008, TATA -0.060) and
      MSCORE -2.95 (StableBread -2.951), flag false (-2.95 < -1.78, not a
      manipulator). Secondary: WallStreetMojo indices -> MSCORE -2.53 (matches).
      I independently hand-recomputed both M-Scores from the inputs before coding.
    * suite counts recorded — examples 180/0, negative 223/0.
    * (WP build) unknown propagation per component — YES (drop PP&E -> AQI/DEPI ->
      mscore/flag all unknown; other indices still compute; mscore short-circuits).
- files added/changed:
    stdlib/forensics.bas (beneish + mscore + _fadd/_over/_beneish_coeffs),
    stdlib/fundamentals.bas (ppe_net concept),
    examples/fixtures/edgar/beneish_boeing_fixture.json (new),
    examples/forensics_beneish_test.bas/.out (new golden),
    tests/run_examples.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * GMI uses gross margin = (revenue - cost_of_revenue)/revenue (COGS-based, as
      in the reference); net_income maps to NetIncomeLoss (the reference's "income
      before extraordinary items" for Boeing FY2023 = the net loss -2242).
    * threshold is the WP's -1.78 (Beneish's primary probit cutoff); the more
      conservative -2.22 cutoff also exists in the literature but the WP specifies
      -1.78.
    * THIS UNBLOCKS the rest of the plan: WP-FOR-4 (flags) now has its M-Score
      dependency, and through it Track D (WP-EVT-1/2) and WP-SCR-2.
### WP-FOR-3 — done-claimed — 2026-07-02
- Added to stdlib/forensics.bas: altman(facts) (Z" distress, book equity),
  altman_classic(facts, prices) (classic Z, market equity from a §4.7 price
  frame), and dilution(facts) (shares trend, SBC, buybacks, net). Scorecard
  frames per fiscal year with component ratios + zone; NA propagation throughout.
- commands run:
    ./tests/run_examples.sh
    ./tests/run_negative.sh
- output tail (verbatim, unedited) — examples/forensics_distress_test.out:
    altman Z" AAPL 2025-09-27
      x1_wc=-0.0492 x2_re=-0.0397 x3_ebit=0.3704 x4_bookeq=0.2583
      zscore=2.3078 zone=grey
      ingredients 2025-09-27: ca=147957000000 cl=165631000000 ta=359241000000
        re=-14264000000 ebit=133050000000 book_equity=73733000000 total_liabilities=285508000000
    altman classic Z AAPL 2025-09-27 (synthetic close=250)
      x4_mkt=12.9359 x5_sales=1.1584 zscore=10.0276 zone=safe
    dilution AAPL 2025-09-27 prior=2024-09-28
      shares=14773260000 prior_shares=15116786000 shares_change=-343526000
      sbc=12863000000 buybacks=90711000000 net=77848000000
  suite totals: examples PASS: 162 FAIL: 0 ; negative PASS: 201 FAIL: 0
- Done-when checklist:
    * Z" golden vs a HAND-COMPUTED example (component ratios + ingredients) — YES.
      HAND-CHECK (Matthew, AAPL FY end 2025-09-27), Z" = 6.56*X1 + 3.26*X2 +
      6.72*X3 + 1.05*X4:
        X1 = (ca 147957M - cl 165631M) / ta 359241M = -0.0492
        X2 = re -14264M / ta 359241M                = -0.0397
        X3 = ebit 133050M / ta 359241M              =  0.3704
        X4 = book_equity 73733M / tot_liab 285508M  =  0.2583
        Z" = -0.3228 - 0.1294 + 2.4891 + 0.2712     =  2.3078  -> zone grey
    * synthetic price-frame test exercises classic Z — YES (close=250 at FY end:
      x4_mkt=12.9359, x5=1.1584, Z=10.0276, safe).
    * dilution golden on AAPL (large buybacks) — YES: buybacks 90711M vastly
      exceed SBC 12863M, net +77848M, shares fell 343.526M YoY (the real
      buyback-heavy signal shape).
- files added/changed:
    stdlib/forensics.bas (added altman/altman_classic/dilution + helpers),
    examples/forensics_distress_test.bas/.out (new golden),
    tests/run_examples.sh (registered example)
- deviations from plan:
    * altman(facts) [Z"] and altman_classic(facts, prices) [classic Z] are two
      function names, not one overloaded `altman` — gBASIC has no arity overload.
    * price lookup is an EXACT date match against the price frame's `date`
      column (no nearest-trading-day search); the synthetic test uses the FY-end
      date. A real caller aligns their price frame's dates to fiscal year ends.
    * dilution `net` = buybacks - SBC (dollars): positive => buybacks exceed
      share-based comp (real return); near-zero/negative => buybacks merely mop
      up issuance.
- observations:
    * SECTOR CAVEAT illustrated: AAPL scores Z"=2.31 (GREY) despite being
      obviously non-distressed, because X1 (working capital) and X2 (retained
      earnings) are NEGATIVE — AAPL runs negative working capital and an
      accumulated deficit from years of buybacks. The Z-family was built on
      industrials; this is exactly the applicability caveat in edgar_design.md
      §9.1. The math is correct; the model's assumptions are the caveat.
### WP-FOR-4 — done-claimed — 2026-07-04
- Built forensics.flags(facts, subs) + forensics.flags_window(facts, subs,
  window_days) in stdlib/forensics.bas — the composite red-flag dossier of
  edgar_design.md §4.5. Two-arg flags() defaults the 5.02 cluster window to 90
  days. One frame, five columns per §4.5 ("kind, date, accession/period, detail"):
    kind, date, accession (unknown for facts-side rows), period, detail.
  Eight detectors, emitted in a FIXED order (submissions rows in feed order,
  facts rows fiscal-year ascending) so the frame is deterministic WITHOUT a sort:
    * nt_filing        — form starts "NT " (late-filing notice)
    * amendment        — form is 10-K/A or 10-Q/A (restatement territory; routine
                         /A forms like 8-K/A, SC 13D/A are deliberately excluded)
    * auditor_change   — 8-K with item 4.01
    * non_reliance     — 8-K with item 4.02 (the fire alarm)
    * officer_exodus   — 8-K item 5.02 clustered: >=2 such filings within
                         window_days of each other (per-filing |civil-day| diff)
    * rising_accruals  — accrual ratio (forensics.accruals) up YoY and positive
    * mscore_flag      — forensics.beneish flag true (M > -1.78)
    * positive_ni_negative_fcf — NI>0 while FCF (ocf - capex) < 0 in a fiscal year
  Helpers added: _civil_days (Howard Hinnant days_from_civil on "YYYY-MM-DD" —
  O(1), no dependency on dates.bas's day-walk), _col (missing-column-safe frame
  read), _row (past-end-safe cell read), _item_hit (comma-split 8-K item match;
  no item code is a prefix of another so trimmed starts_with is safe), _is_8k.
- Fixtures (both new): examples/fixtures/edgar/flags_synthetic_subs.json (a
  submissions frame: NT 10-K, 10-K/A, 8-K 4.01, 8-K 4.02, THREE clustered 8-K
  5.02s, PLUS benign filings (plain 10-K, 8-K 2.02) and one ISOLATED 5.02 dated
  2023-01-05 that must NOT cluster) and examples/fixtures/edgar/flags_synthetic_facts.json
  (a companyfacts-shaped record, 3 fiscal years engineered so the 2023 pair fires
  rising_accruals + mscore_flag + positive_ni_negative_fcf, and the 2022 beneish
  pair stays unknown so exactly one M-Score row fires). Both built by a one-off
  python generator (scratchpad), NOT captured (no network).
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/forensics_flags_test.out:
    == synthetic red-flag dossier (default 90-day cluster window) ==
    nt_filing | 2024-02-15 | acc=0000000000-24-000001 | period=2023-12-31 | late filing: NT 10-K
    amendment | 2024-06-01 | acc=0000000000-24-000010 | period=2022-12-31 | amended financials: 10-K/A
    auditor_change | 2024-03-10 | acc=0000000000-24-000005 | period= | 8-K 4.01 auditor change
    non_reliance | 2024-04-20 | acc=0000000000-24-000007 | period= | 8-K 4.02 non-reliance on prior financials
    officer_exodus | 2024-06-15 | acc=0000000000-24-000013 | period= | 5.02 officer change clustered (3 within 90d)
    officer_exodus | 2024-05-20 | acc=0000000000-24-000012 | period= | 5.02 officer change clustered (3 within 90d)
    officer_exodus | 2024-05-01 | acc=0000000000-24-000011 | period= | 5.02 officer change clustered (3 within 90d)
    rising_accruals | 2023-12-31 | acc=- | period=2023-12-31 | accrual ratio 0.027 -> 0.156
    mscore_flag | 2023-12-31 | acc=- | period=2023-12-31 | M-Score -0.38 > -1.78
    positive_ni_negative_fcf | 2023-12-31 | acc=- | period=2023-12-31 | positive net income, negative free cash flow
    rows: 10
    == 10-day window ==
    officer_exodus rows: 0
    == real filer (AAPL) over real submissions ==
    submissions rows scanned: 1000
    total flag rows: 10
      nt_filing: 0
      amendment: 0
      auditor_change: 0
      non_reliance: 0
      officer_exodus: 7
      rising_accruals: 1
      mscore_flag: 2
      positive_ni_negative_fcf: 0
  suite totals: examples PASS: 181 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * synthetic-fixture golden covers ALL flag kinds — YES. All eight detectors
      fire in the 90-day run (10 rows: 1 nt, 1 amendment, 1 auditor_change, 1
      non_reliance, 3 officer_exodus, 1 rising_accruals, 1 mscore_flag, 1 fcf).
    * window parameter demonstrated — YES: flags_window(...,10) dissolves the May
      5.02 cluster (officer_exodus 0), while the isolated 2023-01-05 5.02 never
      clusters at either window (confirming the |civil-day| diff, not mere count).
    * real-filer run produces a (possibly-empty) frame without error; counts
      recorded — YES: Apple over its REAL companyfacts + REAL submissions (1000
      filings scanned, projected to a frame exactly as edgar.submissions does),
      0.63s, no error. Real output: officer_exodus 7, rising_accruals 1,
      mscore_flag 2, all else 0 — the model's own signal, recorded as evidence,
      NOT a verdict (Beneish is known to false-positive on large growth+buyback
      firms; the dossier reports, it does not judge).
- files added/changed:
    stdlib/forensics.bas (flags + flags_window + _civil_days/_col/_row/_item_hit/_is_8k),
    examples/fixtures/edgar/flags_synthetic_subs.json (new),
    examples/fixtures/edgar/flags_synthetic_facts.json (new),
    examples/forensics_flags_test.bas/.out (new golden),
    tests/run_examples.sh (registered example).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * window is exposed as a 3rd param on flags_window(); the 2-arg flags() the WP
      signature specifies delegates with a 90-day default (gBASIC has no default
      params). "clustered 5.02s (window parameter)" thus satisfied without changing
      the documented flags(facts, subs) surface.
    * amendment detector is intentionally restricted to 10-K/A & 10-Q/A (the
      restatement forms §4.5 names); flagging every /A would fire on routine
      8-K/A and 13D/A amendments.
    * facts-side rows carry accession=unknown and set date=period=fiscal-year-end;
      submissions rows carry the filing's accession and filing date, period=reportDate.
    * THIS UNBLOCKS: Track D (WP-EVT-1 events surface, WP-EVT-2 monitor) and
      WP-SCR-2 (cross-sectional screener scoring), plus WP-DEMO-1 (Adrian demo).
### WP-DEMO-1 — done-claimed — 2026-07-05
- Built examples/edgar/scorecard.bas — "the Adrian demo": ticker in, forensic
  dossier out, composed over the public surfaces (edgar + fundamentals +
  forensics). One screenful, three sections:
    * fundamentals trends — last 4 fiscal years of FCF, gross/operating/net
      margins, and net debt (aligned table, FY rows only via a by_end() map).
    * the §4.5 forensic scorecard (latest FY) — Piotroski F-Score, Beneish
      M-Score + flag, Altman Z" + zone, Sloan accrual ratio, dilution net.
    * composite red flags — forensics.flags(facts, subs) as a per-kind tally,
      with an honest "evidence not verdict / Beneish-Altman false-positive on
      big growth+buyback firms" caveat.
  Every value formatted through unknown-safe helpers (money_b / pct / num2 /
  num3 / show) so a missing ingredient prints "n/a", never a guess or a crash.
- OFFLINE by default / ONLINE when EDGAR_IDENT is set: uses edgar.offline against
  examples/fixtures/edgar (the existing company_tickers.json + companyfacts_
  CIK0000320193.json + submissions_CIK0000320193.json captures); if EDGAR_IDENT
  is exported it identifies with it and hits SEC live instead. A temp sqlite cache
  (examples/tmp_scorecard_cache.db) is used and self-deleted so the run is
  deterministic and leaves nothing behind.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/edgar/scorecard.out:
    ================================================================
      FORENSIC DOSSIER — AAPL  (Apple Inc.)
      CIK 0000320193
    ================================================================

    -- fundamentals (fiscal year) ----------------------------------
      end          FCF       gross    oper     net      net debt
      2022-09-24  111.44B   43.3%  30.3%  25.3%  86.44B
      2023-09-30  99.58B   44.1%  29.8%  25.3%  75.14B
      2024-09-28  108.81B   46.2%  31.5%  24%  66.72B
      2025-09-27  98.77B   46.9%  32%  26.9%  54.74B

    -- forensic scorecard (latest fiscal year) ---------------------
      Piotroski F-Score   8 / 9        (fundamental health, higher better)
      Beneish M-Score     -2.29         (manipulation; flag=false, threshold -1.78)
      Altman Z"           2.31         (distress; zone=grey)
      Sloan accrual ratio 0.001        (earnings quality; lower/negative better)
      Dilution net        77.85B      (buybacks minus stock-based comp)

    -- red flags (composite; signals, not verdicts) ----------------
      officer_exodus x7
      rising_accruals x1
      mscore_flag x2
      total: 10 flag rows

      note: forensic scores are published models over reported facts;
      Beneish/Altman were built on industrials and are known to
      false-positive on large growth+buyback firms. Read as evidence.
  suite totals: examples PASS: 185 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done when checklist:
    * offline golden of full output recorded — YES (examples/edgar/scorecard.out,
      28 lines; deterministic against the fixed fixtures, cache self-cleaned).
    * program length + readability matter (showpiece) — YES: ~185 lines with
      section-header comments; unknown-safe formatters; one aligned trend table
      then a labelled scorecard then a tallied flag list. The AAPL dossier reads
      as a coherent one-screen story: strong margins (43->47%), falling net debt
      (86->55B), F=8/9, clean latest-FY M-Score (-2.29, flag false), Z"=2.31 grey,
      near-zero accruals, huge buyback-driven dilution net (+77.85B) — and the
      flag scan honestly surfaces 10 rows (incl. 2 earlier-year M-flags: a known
      big-growth-firm false positive, called out in the note).
- files added/changed:
    examples/edgar/scorecard.bas (new demo),
    examples/edgar/scorecard.out (new golden),
    tests/run_examples.sh (registered example + tmp_scorecard_cache.db cleanup).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * `program main(args)` — referencing `args` when none are passed raises
      "undefined variable: args" in this build, so the ticker is a top-of-program
      constant (change it, or export EDGAR_IDENT and go live) rather than argv[0].
    * loads live INSIDE `program main` (module-level `load` left edgar.session
      unresolved — "invalid function call"); the pure formatting/frame helpers
      stay top-level since they touch no library.
    * scorecard shows the LATEST-FY M-Score (clean) while flags scans ALL years —
      hence latest flag=false but mscore_flag x2 in the tally; both are correct
      and the difference is the point (a scorecard is a snapshot, flags are a
      history sweep).

---

## Track B — XML module

### WP-XML-1 — done-claimed — 2026-07-03
- Built the native XML module over libxml2 (2.15.2): src/modules/xml.c with
  xml.parse(text[, keep_space]) and xml.parse_file(path). §2 node records
  (name/qname/ns/attrs/children, coalesced text, whitespace-dropped default +
  keep_space), §7 security defaults, §8 structured errors with line/column.
- commands run:
    make                              (HAVE_LIBXML2=1 via pkg-config libxml-2.0)
    ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full ./gbasic examples/xml_parse_test.bas
    valgrind --leak-check=full ./gbasic tests/negative_xml_malformed.bas
    make clean && make LIBXML2_AVAILABLE=0   (degradation check)
- output tail (verbatim, unedited):
    examples PASS: 163  FAIL: 0
    PASS examples/xml_parse_test.bas
    negative PASS: 205  FAIL: 0
    PASS tests/negative_xml_not_loaded.bas
    PASS tests/negative_xml_malformed.bas
    PASS tests/negative_xml_parse_arity.bas
    PASS tests/negative_xml_parse_type.bas
    (valgrind, parse) All heap blocks were freed -- no leaks are possible
    (valgrind, parse) ERROR SUMMARY: 0 errors from 0 contexts
    (valgrind, malformed error path) ERROR SUMMARY: 0 errors from 0 contexts
    (libxml2 OFF) load xml -> "XML support is not available in this build"
    (libxml2 OFF) xml.parse (unloaded) -> "library not loaded: xml"
  parse golden (examples/xml_parse_test.out) covers:
    root order id=A1 qty=3 / 2 items / item0 Widget / item1 Gadget
    ns local=root qname=a:root uri=urn:example:x  (namespaces)
    entity [5 < 10 & 3 > 1  raw <b> ]              (entities decoded + CDATA)
    ws_default 2 / ws_keep 5                        (whitespace drop vs keep)
    plain_ns_nothing true / plain_children 0
  malformed golden asserts line/col:
    xml: Opening and ending tag mismatch: unclosed line 1 and root (line 1, column 24)
- Done-when checklist:
    * parse goldens green incl. namespaces, CDATA, entities, not-well-formed
      error with line/col asserted — YES.
    * valgrind-clean — YES (all heap freed; 0 errors on happy, error, and
      depth-cap paths).
    * build-without-libxml2 degrades to the standard clean error — YES (forced
      LIBXML2_AVAILABLE=0: load-time "not available in this build").
- files added/changed:
    src/modules/xml.c (new module),
    src/eval.c (xml_library_loaded flag; `load xml` handler; `#include
      "modules/xml.c"`; xml dispatch in the qualified-call chain),
    Makefile (libxml-2.0 detection -> HAVE_LIBXML2; eval.o deps on
      src/modules/xml.c),
    examples/xml_parse_test.bas/.out (new golden),
    tests/negative_xml_{not_loaded,malformed,parse_arity,parse_type}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered)
- deviations from plan:
    * src/modules/xml.c is a TRANSLATION-UNIT INCLUDE (`#include`d into eval.c),
      NOT a separate .o. Reason: eval.c's Value type + constructors are
      file-static; a standalone object would require exposing the entire Value
      API in a header (a large refactor of eval.c's encapsulation). The include
      honors plan §2's file location while reusing the static API, matching how
      every other optional module already lives inside eval.c. FLAG for Matthew:
      if a true separate compilation unit is required, that is a bigger
      eval.c-API-exposure task — say so and I will scope it.
    * security options = NONET | NOCDATA; NOENT is deliberately NOT set (only the
      five predefined entities + char refs expand; DTD-declared entities are left
      unexpanded — billion-laughs / XXE closed). DTD loading off by default.
    * the depth cap (design default 256) is enforced by libxml2's OWN default
      max-depth (also 256), which fires during parse as a structured error
      ("Excessive depth in document: 256 ...", source xml, with line/col) — not a
      crash. A redundant walk-level XML_MAX_DEPTH=256 backstop is also present.
    * parse_file accepts a string path OR a file reference.
- observations:
    * libxml2's default error printer was writing to stderr; suppressed via a
      no-op structured-error handler (xmlSetStructuredErrorFunc) so only the
      gBASIC structured error surfaces.
    * CLAUDE.md's "HAVE_*" list (Build & run section) does not yet mention
      HAVE_LIBXML2 — a one-line doc sync for a later pass (left out to stay in
      the WP boundary).
### WP-XML-2 — done-claimed — 2026-07-03
- Built the four navigation helpers (xml_design.md §3) in src/modules/xml.c:
  xml.find(node, path), xml.find_all(node, path), xml.text(node),
  xml.attr(node, name[, default]). Mini-path = slash-separated local names or
  `*`, matched against element children, descending; find = first match,
  find_all = all matches at the final step. Pure over the §2 node record (work on
  hand-built records too). §8 absence: find/attr -> unknown, text -> "", none raise.
- FIXTURE: a real AAPL Form 4 was captured (examples/fixtures/edgar/form4_sample.xml,
  7692 bytes, well-formed ownershipDocument) using the authorized identity, and a
  --form4 capture mode was added to tools/edgar_capture.sh (recorded in MANIFEST).
- commands run:
    make ; ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full ./gbasic examples/xml_form4_test.bas
- output tail (verbatim, unedited) — examples/xml_form4_test.out:
    doc_type=4
    symbol=AAPL
    owner_cik=0001780525
    owner_name=Newstead Jennifer
    nonderiv_txn_count=2
      txn0 security=Common Stock code=M shares=30104 price=[]
      txn1 security=Common Stock code=F shares=16238 price=[296.42]
    wildcard_symbol=0000320193
    attr_id=42 attr_kind=sale
    attr_missing_unknown=true
    attr_default=none
    find_miss_unknown=true
    find_all_miss_empty=0
    text_of_unknown=[]
  suite totals: examples PASS: 164 FAIL: 0 ; negative PASS: 208 FAIL: 0
  valgrind: All heap blocks were freed -- no leaks are possible; 0 errors.
- Done-when checklist:
    * goldens green incl. the REAL Form 4 field-check — YES. HAND-CHECK
      (Matthew, vs form4_sample.xml / the filing): reporting owner CIK
      0001780525 (Newstead Jennifer); nonderivative txn0 code M, 30104 shares
      (Common Stock); txn1 code F, 16238 shares @ 296.42.
    * absence semantics (unknown / "") — YES (find miss -> unknown, find_all
      miss -> empty, text(unknown) -> "", attr miss -> unknown / default).
- files added/changed:
    src/modules/xml.c (added find/find_all/text/attr + helpers + dispatch),
    examples/xml_form4_test.bas/.out (new golden),
    tests/negative_xml_{find_arity,find_path_type,attr_arity}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered),
    tools/edgar_capture.sh (--form4 capture mode),
    examples/fixtures/edgar/form4_sample.xml (new fixture) + MANIFEST.md row
- deviations from plan:
    * helpers are in C, NOT stdlib .bas (the WP's "prefer .bas" preference). The
      compiled `xml` module owns the `xml.` qualifier — `load xml` is intercepted
      before any stdlib file resolves, so xml.find/text/attr MUST live in the
      compiled module to be `xml.`-qualified. They are still pure record ops (no
      libxml2), so they also work on hand-built records as the design intends.
    * the Form 4 fixture was captured by CC under the authorized identity
      (Matthew previously authorized EDGAR fetching and said "continue with
      WP-XML-2" knowing it needed the fixture), rather than Matthew running the
      capture. The --form4 mode makes it repeatable; MANIFEST records it. If you
      prefer to re-capture yourself:
        EDGAR_IDENT="..." tools/edgar_capture.sh --form4 320193 000114036126025622 form4.xml
- observations:
    * `*` wildcard returns the FIRST child element (find semantics): issuer/* ->
      issuerCik 0000320193 in the golden. Exact qualified-name matching is via
      the `qname` field in user code (design §3), not the mini-path.
### WP-XML-3 — done-claimed — 2026-07-03
- Built xml.encode(node[, pretty]) in src/modules/xml.c: record tree -> XML
  string. Escapes text (& < >) and attribute values (+ "); writes `qname` as
  given (namespace prefixes preserved); attribute values stringified via
  canonical conversion; entry validation raises on a malformed node record;
  pretty = indented.
- commands run:
    make ; ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full ./gbasic examples/xml_encode_test.bas
- output tail (verbatim, unedited) — examples/xml_encode_test.out:
    encode <order id="A1"><item>Widget</item><item>Gadget</item></order>
    escaped <n note="a &quot;b&quot; &amp; c">5 &lt; 10 &amp; 3 &gt; 1</n>
    handbuilt <x n="5" s="a&lt;b&amp;c">hello &amp; &lt;world&gt;</x>
    rt_attrs_nested true
    rt_prefix_ns true
    rt_default_ns true
    rt_empty true
    rt_entities true
    (pretty) <order id="A1">\n  <item>Widget</item>\n  <item>Gadget</item>\n</order>
  suite totals: examples PASS: 165 FAIL: 0 ; negative PASS: 210 FAIL: 0
  valgrind: All heap blocks were freed -- no leaks are possible; 0 errors
  (happy + malformed error path).
- Done-when checklist:
    * parse->encode->parse round-trip equality (structural via serialize) — YES,
      5 shapes: attrs+nested, prefixed ns, default ns, empty element, entities.
    * escaping goldens (& < quotes in attrs) — YES (text 5 &lt; 10 &amp; 3 &gt; 1;
      attr a &quot;b&quot; &amp; c; hand-built a&lt;b&amp;c / &lt;world&gt;).
    * valgrind-clean — YES.
- files added/changed:
    src/modules/xml.c (xml.encode + writer/escape helpers; PLUS a WP-XML-1
      parser fix, see below),
    examples/xml_encode_test.bas/.out (new golden),
    tests/negative_xml_encode_{arity,malformed}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered)
- deviations / cross-WP fix:
    * REQUIRED WP-XML-1 PARSER FIX: namespace DECLARATIONS (xmlns / xmlns:prefix)
      live in libxml2's nsDef list, not properties, so WP-XML-1's parser had
      dropped them. The design (§2) keeps xmlns in `attrs` precisely so a parsed
      tree round-trips. Fixed xml_element_to_record to emit nsDef entries into
      `attrs` (xmlns:p="uri" / xmlns="uri"). Existing WP-XML-1/2 goldens
      (xml_parse_test, xml_form4_test) are UNCHANGED (re-diffed, MATCH) because
      they don't print the attrs of a namespaced element. Without this fix
      prefixed/default-namespace round-trips fail.
    * bug found + fixed: builtin_string_value CONSUMES its argument (returns
      strings as-is, frees other kinds); the first cut double-freed the record's
      attr value. Fixed by stringifying value_copy(*attr) — valgrind-clean.
- observations:
    * pretty printing indents element children; text-only elements stay inline;
      whitespace-only text between elements is dropped in pretty (re-parse drops
      it too, so pretty output still round-trips).
### WP-XML-4 — done-claimed — 2026-07-03
- Built the streaming reader (xml_design.md §4): xml.reader(path) / xml.read(r) /
  xml.close(r) over libxml2's xmlTextReader. The reader is a new opaque value
  kind VALUE_XML_READER, refcounted like the sqlite/pg connection handles, so it
  closes on scope cleanup; xml.close is explicit + idempotent; any use after
  close is a structured error. Event records: {kind: element|end|text, name,
  qname, ns, attrs (element), text (text), depth, line}. skip_to/subtree are WP-XML-5.
- commands run:
    make ; ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full ./gbasic examples/xml_reader_test.bas
    valgrind on a reader that is NEVER explicitly closed (scope-cleanup path)
- output tail (verbatim, unedited) — examples/xml_reader_test.out:
    element catalog depth=0 line=9
    element book id=b1 depth=1 line=9
    element title depth=2 line=9
    text [XML] depth=3 line=9
    end title depth=2 line=9
    end book depth=1 line=9
    element book id=b2 depth=1 line=9
    element title depth=2 line=9
    text [Streaming] depth=3 line=9
    end title depth=2 line=9
    end book depth=1 line=9
    end catalog depth=0 line=9
    total_events=12
    double_close_ok=true
  suite totals: examples PASS: 166 FAIL: 0 ; negative PASS: 214 FAIL: 0
  valgrind (reader golden): All heap blocks were freed -- no leaks are possible; 0 errors
  valgrind (reader never closed -> scope cleanup frees it): 0 errors, 0 lost
- Done-when checklist:
    * event-stream golden (kinds, names, depths, lines) — YES (12 events above).
    * error tests green — YES: use-after-close ("xml.read on a closed reader"),
      read-on-non-reader ("xml.read expects an xml reader"), reader-on-missing-
      file ("could not open ... as XML"), and DEPTH-CAP ("xml: Excessive depth in
      document: 256 ... (line 1, column 774)").
    * valgrind-clean (the decisive value — readers are leak-prone) — YES, incl.
      the no-explicit-close scope-cleanup path.
- files added/changed:
    src/eval.c (VALUE_XML_READER: enum, union member, forward typedef,
      XmlReaderValue struct, value_xml_reader ctor, xml_reader_release,
      value_copy retain, value_free release, and the type-name/condition/
      display/equality/string/encode/serialize switch sites; +
      #include <libxml/xmlreader.h>),
    src/modules/xml.c (xml.reader/read/close + event builder + reader-arg guard),
    examples/xml_reader_test.bas/.out (new golden),
    tests/negative_xml_{read_type,read_closed,reader_missing,reader_depth}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered + tmp-file cleanup)
- deviations / notes:
    * `line` is libxml2's parser READ-POSITION (xmlTextReaderGetParserLineNumber),
      not a per-element line: a small buffered document reports the last line
      (9) for every event; a large streamed document advances. depth/kind/name
      are the accurate per-element signals. Faithful to what the reader exposes.
    * the depth cap is enforced by libxml2's OWN reader limit (256): xml.read
      surfaces it as a structured xml error via the global last-error. A
      redundant XML_READER_MAX_DEPTH=256 backstop is also present.
    * whitespace-only text nodes are skipped by the reader (data-XML default,
      matching the tree parser); comments/PIs/doctype are skipped.
    * the new value kind was added at ~10 switch sites in eval.c mirroring
      VALUE_SQLITE_CONNECTION; full example suite (incl. sqlite/pg/actor handle
      tests) stays green, so the enum addition did not disturb the others.
### WP-XML-5 — done-claimed — 2026-07-03
- Built skip_to + subtree (xml_design.md §4 windowing) on top of the WP-XML-4
  reader:
    * xml.skip_to(r, name) — advances the cursor to the next start-element whose
      LOCAL name equals `name` (namespace-agnostic, so it matches <infoTable> in
      the default-namespaced 13F doc); returns true when positioned on one, false
      at end-of-document. The current node is checked BEFORE reading forward, so
      `while skip_to: subtree` handles adjacent siblings without skipping one.
    * xml.subtree(r) — cursor MUST be on a start-element (else raises); materializes
      that element into a §2 node record via xmlTextReaderExpand (memory bounded by
      the element, not the file), copies it into a Value, then xmlTextReaderNext
      advances past the subtree. Reuses xml_element_to_record, so subtree trees are
      byte-identical in shape to xml.parse trees.
- Fixture (real, captured live via the authorized identity; NOT in tests' network
  path — read from disk): examples/fixtures/edgar/f13_infotable_sample.xml —
  13F-HR information table, CIK 1596355, accession 0001596355-26-000003, 17
  holdings, 8077 bytes. Captured with a new `--doc CIK ACCN FILE DEST` mode added
  to tools/edgar_capture.sh; recorded in the EDGAR MANIFEST.
- commands run:
    make ; ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full --errors-for-leak-kinds=all ./gbasic examples/xml_window_13f_test.bas
    valgrind on a NO-explicit-close windowing variant (scope-cleanup path)
    valgrind on the subtree-non-element error path
- output tail (verbatim, unedited) — examples/xml_window_13f_test.out:
    count=17
    total_value=180807891
    row1: Accenture Plc | G1151C101 | 3272150 | 26295
    row5: Berkshire Hathaway B | 084670702 | 56153766 | 112220
    row17: Vanguard S&P 500 Value ETF | 921932703 | 274670 | 1253
  suite totals: examples PASS: 167 FAIL: 0 ; negative PASS: 218 FAIL: 0
  valgrind (windowing golden): 0 errors, no leaks
  valgrind (no-close scope-cleanup path, n=17): 0 errors, 0 lost
  valgrind (subtree-non-element error path): 0 valgrind errors (program exits 1 on the raise)
- Done-when checklist:
    * design's 13F windowing loop as a test produces a holdings frame — YES
      (the loop is xml_design.md §4 verbatim: reader → while skip_to("infoTable")
      → subtree → find/text/number → append).
    * field-checked against hand-read values (2–3 rows for Matthew) — YES, the
      three rows above + count=17 + total_value=180807891 were independently
      derived with Python ElementTree over the same fixture and match exactly.
      Hand-check anchors (row / issuer / cusip / value / sshPrnamt):
        1  / Accenture Plc                / G1151C101 / 3272150  / 26295
        5  / Berkshire Hathaway B         / 084670702 / 56153766 / 112220
        17 / Vanguard S&P 500 Value ETF   / 921932703 / 274670   / 1253
        (all 17 sum to value 180,807,891)
    * subtree on non-start-element raises — YES (negative test; also skip_to/
      subtree on a closed reader, and skip_to non-string name).
    * valgrind-clean — YES, incl. the scope-cleanup and error paths.
- files added/changed:
    src/modules/xml.c (xml_eval_skip_to, xml_eval_subtree, dispatch entries),
    tools/edgar_capture.sh (--doc general single-document capture mode),
    examples/fixtures/edgar/f13_infotable_sample.xml (new real fixture) + MANIFEST.md,
    examples/xml_window_13f_test.bas/.out (new golden),
    tests/negative_xml_{subtree_non_element,subtree_closed,skip_to_type,skip_to_closed}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered + tmp-file cleanup)
- deviations / notes:
    * skip_to matches on LOCAL name (xmlTextReaderConstLocalName), deliberately
      namespace-agnostic — the 13F table declares a default namespace, so a
      qname/URI-sensitive match would find nothing. This mirrors find/find_all,
      which also navigate by local name.
    * subtree caps depth via the same XML_MAX_DEPTH=256 as the tree parser (it
      routes through xml_element_to_record); the reader's own read-side cap is
      unchanged. No new leak surface beyond the transient Expand DOM, which the
      reader owns and frees on the following Next — confirmed by valgrind.
    * open question §10.2 (should skip_to accept a path like "table/row"?) left as
      designed: bare local name only, per §4.
### WP-XML-6 — done-claimed — 2026-07-03
- Proves the streaming claim (xml_design.md §11 Phase 2 note): a >=100 MB
  document windowed via skip_to("infoTable")/subtree keeps peak resident memory
  bounded by the element, not the file.
- Build:
    * tools/xml_bigfile_gen.sh — synthesizes a namespaced <informationTable> with
      hundreds of thousands of <infoTable> records (same shape as the real 13F
      fixture), sized past a byte target. Output is NOT checked in (generated on
      demand, deleted by the harness).
    * tests/run_xml_bigfile.sh — MANUAL-TIER harness (NOT in make test / not in
      run_examples.sh). Generates the file, streams it with the §4 windowing loop
      in a background gbasic, and samples /proc/<pid>/status VmHWM in a tight loop
      (gBASIC can't read /proc itself — size-0 files yield 0 lines — so sampling
      is external). Asserts peak VmHWM < BOUND_KB (default 64 MiB).
- how gBASIC reading /proc was ruled out: read_lines on a `(file)=` ref to
  /proc/self/status returns linecount=0 (procfs reports st_size 0); hence the
  external sampler.
- commands run:
    ./tests/run_xml_bigfile.sh
- output tail (verbatim, unedited):
    xml_bigfile_gen: wrote 367800 records, ~104857615 bytes
    source file size: 104857632 bytes (100 MiB)
    records=367800
    checksum=6763860390000
    gbasic exit code:        0
    VmHWM before (first obs): 4048 kB
    VmHWM peak (during run):  10840 kB
    peak ceiling (BOUND_KB):  65536 kB
    source file:              104857632 bytes
    PASS: peak VmHWM 10840 kB < 65536 kB ceiling while streaming a 104857632-byte file
          (peak is ~10% of the source size — memory is bounded by the element, not the file)
    real  0m3.452s
- Done-when checklist:
    * harness output tail recorded (file size, VmHWM before/after/peak) — YES,
      above: file 104,857,632 bytes; VmHWM before 4048 kB, peak 10840 kB.
    * marked manual-tier (not in make test), invocation documented — YES: header
      of tests/run_xml_bigfile.sh documents `./tests/run_xml_bigfile.sh` and the
      BOUND_KB / TARGET_BYTES overrides; it is NOT registered in run_examples.sh.
- correctness anchor: checksum=6763860390000 = sum over n=1..367800 of value=100*n
  = 50 * 367800 * 367801 = 6,763,860,390,000 (exact), confirming all 367,800
  records were genuinely streamed and their <value> read — not short-circuited.
- deviations / notes:
    * peak 10840 kB includes the ~4048 kB interpreter baseline; the incremental
      streaming cost is ~6.8 MB and FLAT (does not scale with the 100 MB file).
      A load-everything reader would show VmHWM > 100 MB (>102400 kB); 10840 kB
      is decisive. Runtime ~3.4 s — acceptable for a manual-tier test.
    * no new C code in this WP (skip_to/subtree are WP-XML-5, already valgrind-
      clean), so there is no new native surface to valgrind here.
    * the generator's byte target is approximate (character count as it grows);
      the harness reports the actual `wc -c` size used in the assertion.
### WP-XML-7 — done-claimed — 2026-07-03
- Built lenient HTML (xml_design.md §6): xml.parse_html(text) over libxml2's HTML
  parser (htmlReadMemory), emitting the SAME §2 node tree as xml.parse — so
  find/find_all/text work on it unchanged. The HTML parser repairs rather than
  rejects, so tag soup never raises; only a NULL doc / rootless result does.
  Security parity with the XML path: HTML_PARSE_NONET (no network), plus RECOVER
  + NOERROR/NOWARNING + NODEFDTD.
- Fixture (real, captured live via the authorized identity; read from disk, no
  network in the test): examples/fixtures/edgar/tenk_10ka_sample.htm — a real
  Form 10-K/A inline-XBRL HTML document (EDGAR's modern annual-report format),
  CIK 2070900 QuantumSphere Acquisition Corp, accession 0001829126-26-007147,
  ~52 KB. Captured with the tools/edgar_capture.sh --doc mode; in the MANIFEST.
- SENTINELS: Matthew delegated the choice ("you choose"). I picked three strings
  read from the 10-K cover page, each present in the extracted xml.text:
    1. "SECURITIES AND EXCHANGE COMMISSION"
    2. "For the fiscal year ended March 31, 2026"
    3. "Emerging growth company"
- commands run:
    make ; ./tests/run_examples.sh ; ./tests/run_negative.sh
    valgrind --leak-check=full --errors-for-leak-kinds=all ./gbasic examples/xml_parse_html_test.bas
    valgrind on the parse_html type-error path
- output tail (verbatim, unedited) — examples/xml_parse_html_test.out:
    root=html
    sentinel1=found
    sentinel2=found
    sentinel3=found
    soup_root=html
    soup_text=TitlePara onebolditalic
  suite totals: examples PASS: 168 FAIL: 0 ; negative PASS: 220 FAIL: 0
  valgrind (parse_html golden): 0 errors, no leaks
  valgrind (parse_html type-error path): 0 valgrind errors (program exits 1 on the raise)
- Done-when checklist:
    * parse of the real 10-K succeeds — YES (root=html; 8342 chars of visible text
      extracted from the inline-XBRL document).
    * xml.text contains 3 sentinel strings (evidence lists them) — YES, the three
      above; all three `find`-hit (sentinelN=found).
    * tag-soup fixture (unclosed tags) parses without error — YES: the inline soup
      "<html><body><h1>Title<p>Para one<b>bold<i>italic</body>" (unclosed h1/p/b/i,
      no </html>) parses to root=html and yields text "TitlePara onebolditalic".
    * valgrind-clean — YES, incl. the type-error path.
- files added/changed:
    src/modules/xml.c (#include <libxml/HTMLparser.h>, XML_HTML_OPTS,
      xml_parse_html_memory, parse_html dispatch),
    examples/fixtures/edgar/tenk_10ka_sample.htm (new real fixture) + MANIFEST.md,
    examples/xml_parse_html_test.bas/.out (new golden),
    tests/negative_xml_parse_html_{arity,type}.bas/.err,
    tests/run_examples.sh + tests/run_negative.sh (registered)
- deviations / notes:
    * xml.text is document order with NO layout (the §6 scope note: parser, not a
      browser), so adjacent block text concatenates without spaces — e.g. the
      cover reads "UNITED STATESSECURITIES AND EXCHANGE COMMISSION" and the soup
      yields "TitlePara onebolditalic". Sentinels were chosen to be robust to this
      (each is a within-block contiguous phrase).
    * the fixture is a 10-K/A (amendment) — the smallest real annual-report HTML I
      could capture (~52 KB); it is a genuine Form 10-K-family inline-XBRL document
      and carries the full 10-K cover/Item structure. Flagged here for honesty;
      the parse/extract path is identical for a plain 10-K.
    * encoding is auto-detected (htmlReadMemory encoding=NULL) from BOM/<meta>, as
      a real 10-K requires; NONET preserves the no-network guarantee.

---

## Track C — Ownership

### WP-OWN-1 — done-claimed — 2026-07-03
- Built stdlib/insiders.bas (edgar_design.md §1 Form 4, §4.3) over the xml module
  and frame:
    * from_form4(doc, filed) — parse one Form 4 (parsed-xml record) into a
      transaction FRAME, one row per NON-DERIVATIVE transaction, all §4.3 columns:
      code, acquired (A/D), shares, price, value, owner, is_officer, is_director,
      officer_title, post_shares, filed, date. Footnoted/absent numeric cells
      (e.g. an RSU-settlement price) are `unknown`, never guessed; value = shares
      * price (unknown if either is). `filed` is supplied by the caller — it is a
      submissions/acceptance date, NOT present in the Form 4 XML (which carries
      only the owner signature date).
    * concat(a, b) — row-wise combine (multiple Form 4s per filer).
    * open_market_buys(tx) — frame.filter to code == "P" (the one voluntary buy
      with the insider's own money, §1).
    * transactions(e, cik, since) — acquisition-backed convenience (submissions ->
      form=="4" & filed>=since -> edgar.document -> from_form4 -> concat). This is
      the network entry point; the offline golden drives the pure core directly,
      so transactions() is documented-but-not-exercised in tests (no network).
  Scope: NON-DERIVATIVE table only in v1; derivative transactions (option
  grants/exercises) are a separate surface, deferred.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
- output tail (verbatim, unedited) — examples/insiders_form4_test.out:
    == real fixture ==
    nrows=2
    codes=M,F
    acquired=A,D
    owner=Newstead Jennifer
    officer_title=SVP, GC and Secretary
    is_officer=true is_director=false
    row0 M/A: shares=30104 price=unknown value=unknown post_shares=57784 date=2026-06-15 filed=2026-06-17
    row1 F/D: shares=16238 price=296.42 post_shares=41546 date=2026-06-15
    row1 value == shares*price ? true
    row0 price is_unknown ? true
    open_market_buys(real) nrows=0
    == synthetic P ==
    owner=Buyer Jane is_director=true is_officer=false
    code=P acquired=A shares=1000 price=150.5 value=150500
    == concat + screen ==
    concat nrows=3
    buys nrows=1
    buy owner=Buyer Jane code=P value=150500 date=2026-05-10
  suite totals: examples PASS: 169 FAIL: 0 ; negative unchanged PASS: 220 FAIL: 0
- Done-when checklist:
    * field-check golden vs hand-read fixture values — YES. Hand-read from
      examples/fixtures/edgar/form4_sample.xml (Apple Form 4, owner Jennifer
      Newstead):
        row0 non-deriv txn: code M, A/D=A, shares 30104, price footnoted->unknown,
          post_shares 57784, date 2026-06-15.
        row1 non-deriv txn: code F, A/D=D, shares 16238, price 296.42,
          post_shares 41546, date 2026-06-15; value == 16238*296.42 (exact-boolean
          check "true" — display shows %g "4.81327e+06", so the check asserts the
          underlying product, not the rounded display).
        relationship: isOfficer true, no isDirector -> false, officerTitle
          "SVP, GC and Secretary".
    * code-P filter test — YES, both directions: open_market_buys over the real
      fixture (no P) = 0 rows (columns preserved); over the synthetic P Form 4
      (+ concat with the real frame, 3 rows total) = exactly the 1 P buy
      (owner Buyer Jane, value 150500, date 2026-05-10).
    * A/D column asserted — YES (acquired=A,D on the real rows; A on the synth).
    * officer_title column asserted — YES ("SVP, GC and Secretary").
- files added/changed:
    stdlib/insiders.bas (new library),
    examples/insiders_form4_test.bas/.out (new golden),
    tests/run_examples.sh (registered)
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface for this WP.
    * load convention followed: insiders.bas uses `load frame from "frame.bas"` /
      `load edgar from "edgar.bas"` (relative to the lib dir) and bare `load xml`
      (native module); the test uses `load insiders from "../stdlib/insiders.bas"`
      + `load xml`, matching the other edgar/fundamentals goldens so it resolves
      under the bare `./gbasic` the runner uses.
    * transactions() strips a leading `xslF345X0N/` render-path prefix from
      submissions.primary_document to reach the raw Form 4 XML (the same wrinkle
      tools/edgar_capture.sh notes); untested offline, flagged above.
    * value display uses %g (the known number-display precision gap) — the golden
      carries an exact-equality boolean so the field-check is precise regardless.
### WP-OWN-2 — done-claimed — 2026-07-03
- Added the two §4.3 convenience compositions to stdlib/insiders.bas:
    * conviction(buys) — adds prior_stake (= post_shares - shares) and conviction
      (= value / prior_stake, per WP-OWN-2) columns to a buys frame. A zero prior
      stake (a first-ever purchase, post_shares == shares) OR any unknown
      ingredient yields `unknown` — never a divide-by-zero.
    * cluster(buys, window) — groups buys into clusters by date proximity (each
      buy within `window` days of its cluster's FIRST buy; input sorted by date
      internally via frame.sort_by, ISO dates sort chronologically) and summarizes
      each: start..end span, distinct owners (count(unique(owner))), buy count,
      total value (unknown if any member value is unknown). owners >= 2 is the
      multi-insider signal. Day gaps use dates.days_between.
  Added `load dates from "dates.bas"` to the library.
- Synthetic fixtures (real Form 4s rarely contain clusters): a 5-row code-P buys
  frame built as a column-literal in the test — Bob has post==shares (zero prior
  stake), Dave has footnoted/unknown price+value, three buys sit >7 days after the
  opening Alice+Bob pair.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
- output tail (verbatim, unedited) — examples/insiders_cluster_test.out:
    == conviction ==
    Alice value=10000 prior_stake=4000 conviction=2.5
    Bob value=10000 prior_stake=0 conviction=unknown
    Alice value=20000 prior_stake=6000 conviction=3.33333
    Alice value=1500 prior_stake=1000 conviction=1.5
    Dave value=unknown prior_stake=500 conviction=unknown
    alice0 conviction == 10000/4000 ? true
    bob zero-prior conviction is_unknown ? true
    dave unknown-value conviction is_unknown ? true
    == cluster (window=7) ==
    num_clusters=3
    cluster 2026-05-01..2026-05-03 owners=2 buys=2 value=20000
    cluster 2026-05-20..2026-05-21 owners=1 buys=2 value=21500
    cluster 2026-06-15..2026-06-15 owners=1 buys=1 value=unknown
  suite totals: examples PASS: 170 FAIL: 0 ; negative unchanged PASS: 220 FAIL: 0
- Done-when checklist:
    * synthetic goldens green — YES (above). conviction hand-checks: Alice0
      10000/4000=2.5 (exact-boolean true), Alice2 20000/6000=3.33333, Alice3
      1500/1000=1.5. cluster window=7: cluster1 = Alice+Bob within 2 days ->
      owners=2 (the multi-insider signal), value 20000; cluster2 = two Alice buys
      1 day apart -> owners=1; cluster3 = Dave alone -> owners=1, value unknown.
    * division-by-zero / zero-prior-stake yields unknown (tested) — YES: Bob
      (post==shares -> prior_stake 0 -> conviction unknown); also Dave
      (unknown value -> conviction unknown), both asserted via is_unknown.
- files added/changed:
    stdlib/insiders.bas (conviction, cluster, + load dates),
    examples/insiders_cluster_test.bas/.out (new golden),
    tests/run_examples.sh (registered)
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * conviction uses the WP's literal "value / prior stake" (dollars per prior
      share); prior stake is post_shares - shares (in shares). Noted for honesty:
      the ratio mixes units, but it is the specified formula and the zero-prior
      edge is what the done-when tests.
    * cluster's `end` column is set via bracket assignment (res["end"]=...) since
      `end` is a reserved word; column reads use ["end"] likewise.
### WP-OWN-3 — done-claimed — 2026-07-03
- Built stdlib/ownership.bas (edgar_design.md §1 13F, §4.4):
    * report_13f(source, filed) — streams a 13F info-table (path/file ref) via the
      xml reader's skip_to("infoTable")/subtree windowing (constant memory,
      WP-XML-5) into a holdings frame {issuer, cusip, value, shares}. `value` is
      normalized to whole dollars by filing date.
    * _normalize_value — SEC Form 13F units: whole dollars for filings on/after
      2023-01-03, THOUSANDS before (×1000). ISO dates compare lexicographically.
    * delta(prior, current) — quarter diff on cusip over the UNION of both
      quarters: columns cusip, issuer, status (new|exited|changed|unchanged),
      prior_shares, shares, delta_shares, prior_value, value, delta_value. `new` ->
      prior 0; `exited` -> current 0 (negative deltas).
- Fixtures (two quarters of ONE filer, both real, captured live; read from disk):
    * f13_infotable_2026q1_sample.xml — CIK 1596355, accession 0001596355-26-000002,
      period 2026-03-31 filed 2026-04-08, 19 holdings (the PRIOR quarter).
    * f13_infotable_sample.xml — accession ...000003, period 2026-06-30 filed
      2026-07-02, 17 holdings (the current quarter; from WP-XML-5).
  Both in the MANIFEST; captured via tools/edgar_capture.sh --doc.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
- output tail (verbatim, unedited) — examples/ownership_13f_test.out:
    == report field-check ==
    q1 count=19 total_value=165332436
    q1 row0: Accenture Plc | G1151C101 | 5230890 | 26380
    q2 count=17 total_value=180807891
    q2 row0: Accenture Plc | G1151C101 | 3272150 | 26295
    == normalization ==
    2019-filed value0 == 2026 value0 * 1000 ? true
    == real delta q1->q2 ==
    counts new=0 exited=2 changed=16 unchanged=1
    exited: Exxon Mobil | 30231G102 | prior_shares=1205 delta_shares=-1205
    exited: Millrose Properties Inc. | 601137102 | prior_shares=27975 delta_shares=-27975
    changed Accenture: prior_shares=26380 shares=26295 delta_shares=-85 delta_value=-1958740
    == synthetic delta ==
    AAA changed prior_sh=100 sh=150 dsh=50 dval=500
    CCC unchanged prior_sh=300 sh=300 dsh=0 dval=0
    DDD new prior_sh=0 sh=400 dsh=400 dval=4000
    BBB exited prior_sh=200 sh=0 dsh=-200 dval=-2000
  suite totals: examples PASS: 171 FAIL: 0 ; negative unchanged PASS: 220 FAIL: 0
- Done-when checklist:
    * report field-check golden — YES. Hand-read (Python ElementTree over the same
      fixtures): Q1 19 holdings total 165,332,436, row0 Accenture G1151C101
      5,230,890 / 26,380; Q2 17 holdings total 180,807,891, row0 Accenture
      3,272,150 / 26,295. Value-unit normalization asserted (2019-filed = ×1000).
    * delta golden from a hand-computed pair, evidence lists one new, one exited,
      one changed — YES, the SYNTHETIC pair (the real consecutive quarters have no
      new position): AAA changed (100->150, dsh 50, dval 500), BBB EXITED
      (dsh -200, dval -2000), CCC unchanged, DDD NEW (dsh 400, dval 4000). The
      real q1->q2 delta is also shown as extra field-check: 2 exited (Exxon Mobil
      30231G102, Millrose 601137102), 16 changed, 1 unchanged; Accenture changed
      -85 shares / -1,958,740 value (all independently reproduced in Python).
- files added/changed:
    stdlib/ownership.bas (new library),
    examples/fixtures/edgar/f13_infotable_2026q1_sample.xml (new real fixture) + MANIFEST.md,
    examples/ownership_13f_test.bas/.out (new golden),
    tools/edgar_capture.sh (reused --doc mode),
    tests/run_examples.sh (registered)
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * public shape: shipped report_13f(source, filed) is the offline-verifiable
      core; the design's report_13f(cik, quarter) NETWORK form is DEFERRED — a 13F
      filing's info-table filename varies per filer and needs the filing index,
      which edgar.bas does not yet expose. Documented in the library header rather
      than shipping a guess.
    * delta composes the cusip UNION explicitly: frame.join is an INNER join, so it
      alone can't surface new/exited (the non-matching rows). Noted in code.
    * the two real quarters had NO new position, so the one-new/one-exited/one-
      changed done-when evidence is the synthetic pair; the real pair supplies the
      exited+changed field-checks.
### WP-OWN-4 — done-claimed — 2026-07-04
- Extended stdlib/ownership.bas with the 13D/13G stakes surface (edgar_design.md
  §1 13D/G, §4.4):
    * stake(source, filed) — parse ONE structured-era primary_doc.xml (an
      <edgarSubmission>) into an event record: filer, form ("13D"|"13G"), percent
      (number or "unknown"), filed, amended (true|false), issuer_cik, issuer_name,
      cusip, event_date. Identifiers (issuer_cik, cusip) stay strings. `filed` is
      supplied by the caller (not carried in the XML), exactly as report_13f.
    * _text_any(node, paths) — first candidate slash-path with non-empty text.
      Needed because 13D and 13G are TWO schemas that diverge: issuerCik (13G) vs
      issuerCIK (13D); classPercent (13G) vs percentOfClass (13D);
      coverPageHeaderReportingPersonDetails (13G) vs
      reportingPersons/reportingPersonInfo (13D); eventDateRequiresFilingThisStatement
      (13G) vs dateOfEvent (13D); issuerCusip (13G, singular) vs
      issuerCusips/issuerCusipNumber (both). All matching is local-name, so the
      schedule13g vs schedule13D namespaces don't interfere.
    * stakes(rows) — stack a list of stake() records into an events frame (columns
      as above); built manually so ownership.bas still depends only on the xml
      module. A holder filing 13G then 13D shows as two rows, form flipping.
    * OUT-OF-SCOPE guard: a non-structured (pre-2025 text) document has no
      headerData/submissionType — stake() RAISES ("not a structured 13D/13G
      filing ...") rather than silently returning empty; a submissionType that
      isn't a 13D/13G also raises.
- Fixtures — two REAL structured-era filings on the SAME subject (Trinity Biotech
  plc, issuer CIK 0000888721), captured live via tools/edgar_capture.sh --doc,
  read from disk (no network in tests):
    * sc13g_trinity_novus_sample.xml — SCHEDULE 13G by Novus Diagnostics Ltd.
      (CIK 2114878), accession 0001213900-26-024764, filed 2026-03-06,
      classPercent 5.99, unamended (the PASSIVE holder).
    * sc13d_trinity_perceptive_sample.xml — SCHEDULE 13D/A by Perceptive Advisors
      LLC (CIK 1224962), accession 0001193125-26-204043, filed 2026-05-04,
      amendment 8, percentOfClass 9.9 (the ACTIVIST on the same issuer).
    * sc13_non_structured_sample.xml — synthetic non-structured stand-in for the
      negative test. Real ones in the MANIFEST.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/ownership_stakes_test.out:
    == real 13G field-check (Novus / Trinity Biotech) ==
    form=13G filer=Novus Diagnostics Ltd. percent=5.99 amended=false
    issuer_name=TRINITY BIOTECH PLC issuer_cik=0000888721 cusip=896438306
    event_date=12/23/2025 filed=2026-03-06
    == real 13D field-check (Perceptive / Trinity Biotech) ==
    form=13D filer=Perceptive Advisors LLC percent=9.9 amended=true
    issuer_name=Trinity Biotech plc issuer_cik=0000888721 cusip=896438504
    event_date=04/30/2026 filed=2026-05-04
    == events frame (same subject, two holders) ==
    rows=2 forms=13G,13D
    filers=Novus Diagnostics Ltd. | Perceptive Advisors LLC
    percents=5.99 | 9.9
    == synthetic flip (same filer) ==
    2026-01-15 13G 6.5% Rivendell Capital LP
    2026-04-02 13D 8.2% Rivendell Capital LP
    flip visible: 13G->13D ? true
- negative stderr (verbatim) — tests/negative_ownership_stake_unstructured.err:
    runtime error at tests/negative_ownership_stake_unstructured.bas:222:13: ownership.stake: not a structured 13D/13G filing (no headerData/submissionType; pre-2025 text filings are out of scope)
  suite totals: examples PASS: 172 FAIL: 0 ; negative PASS: 221 FAIL: 0
- Done-when checklist:
    * field-check golden — YES. Both real filings field-checked against the raw XML
      inspected at capture: 13G percent 5.99 / cusip 896438306 / issuer_cik
      0000888721 / event 12/23/2025 / amended false; 13D percent 9.9 / cusip
      896438504 / amended true (amendment 8) / event 04/30/2026. The two schemas'
      divergent tags all resolved through _text_any.
    * a 13G-then-13D synthetic sequence shows both rows (the flip visible) — YES.
      Same filer "Rivendell Capital LP": 13G at 6.5% (2026-01-15) then 13D at 8.2%
      (2026-04-02); events frame form column = ["13G","13D"], "flip visible ? true".
    * pre-2025 text filings encoded as a tested ERROR, not silence — YES
      (negative_ownership_stake_unstructured raises on a document lacking
      headerData/submissionType).
- files added/changed:
    stdlib/ownership.bas (stake/_text_any/stakes added; dropped the earlier
      load-frame — stakes now builds columns manually, so no new dependency and no
      builtin-name-collision warning),
    examples/fixtures/edgar/sc13g_trinity_novus_sample.xml,
    examples/fixtures/edgar/sc13d_trinity_perceptive_sample.xml,
    examples/fixtures/edgar/sc13_non_structured_sample.xml (+ MANIFEST.md rows for
      the two real ones),
    examples/ownership_stakes_test.bas/.out (new golden),
    tests/negative_ownership_stake_unstructured.bas/.err (new negative),
    tests/run_examples.sh + tests/run_negative.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface. The xml module (parse_file/find/
      text) is used unchanged.
    * public shape: shipped stake(source, filed) + stakes(rows) is the
      offline-verifiable core; the design's stakes(cik) NETWORK form (enumerate
      every 13D/G naming cik as subject) is DEFERRED — it needs EDGAR full-text
      enumeration edgar.bas does not yet expose. Documented in the library header
      rather than guessed. Same honest split as WP-OWN-3's report_13f.
    * multi-person reporting groups: only the FIRST reporting person is surfaced as
      the representative filer/percent in v1 (open question, mirroring 13F per-CIK
      identity). Noted in code.
    * the two real filings are the passive/activist pair on ONE subject but by
      DIFFERENT holders, so they are not themselves a same-holder flip; the flip is
      shown synthetically, exactly as the done-when specifies.

---

## Track D — Events + monitor

### WP-EVT-1 — done-claimed — 2026-07-04
- Finding: WP-FOR-4's flags(facts, subs) already contained the full NT / /A /
  4.01 / 4.02 / 5.02-cluster detection, but ONLY reachable through flags(), which
  requires companyfacts. The §7 monitor classifies incoming filings by item code
  on a live stream with NO companyfacts in hand — so the design DOES demand the
  thin exposure the WP anticipated: a facts-free events surface. Built it as a
  behavior-preserving refactor (no logic change to flags' output).
- Refactor in stdlib/forensics.bas:
    * extracted the five submissions-side detectors into _submission_flags(subs,
      window_days) — a shared internal returning the {kind,date,accession,period,
      detail} frame (submissions rows in feed order; 5.02 cluster by |civil-day|
      diff within window_days).
    * flags_window(facts, subs, window_days) now initialises its columns from
      _submission_flags(...) then appends the facts-side detectors (rising
      accruals / M-Score / positive-NI-negative-FCF) — output BYTE-IDENTICAL to
      before (verified: examples/forensics_flags_test.out diff is empty).
    * NEW public surface: events(subs) = _submission_flags(subs, 90);
      events_window(subs, window_days). Same kinds/columns/ordering/window as the
      submissions rows of flags(), no facts argument.
- commands run:
    GBASIC_PATH=stdlib ./gbasic examples/forensics_flags_test.bas | diff - examples/forensics_flags_test.out  (empty -> flags golden unchanged)
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/forensics_events_test.out:
    == events(subs) — submissions-side only, no companyfacts ==
    nt_filing | 2024-02-15 | acc=0000000000-24-000001 | late filing: NT 10-K
    amendment | 2024-06-01 | acc=0000000000-24-000010 | amended financials: 10-K/A
    auditor_change | 2024-03-10 | acc=0000000000-24-000005 | 8-K 4.01 auditor change
    non_reliance | 2024-04-20 | acc=0000000000-24-000007 | 8-K 4.02 non-reliance on prior financials
    officer_exodus | 2024-06-15 | acc=0000000000-24-000013 | 5.02 officer change clustered (3 within 90d)
    officer_exodus | 2024-05-20 | acc=0000000000-24-000012 | 5.02 officer change clustered (3 within 90d)
    officer_exodus | 2024-05-01 | acc=0000000000-24-000011 | 5.02 officer change clustered (3 within 90d)
    rows: 7
    == events_window(subs, 10) ==
    rows: 4 (cluster dissolved)
    == monitor: classify one incoming 8-K (the fire alarm) ==
    kind: non_reliance | detail: 8-K 4.02 non-reliance on prior financials
  suite totals: examples PASS: 182 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done when checklist:
    * existing suites still green, counts grown never shrunk — YES: examples
      181 -> 182 (added forensics_events_test), negative 223 unchanged; flags
      golden byte-identical (empty diff).
    * PROGRESS records the outcome — this entry. The design DID demand a thin
      exposure (facts-free events()); delivered without touching flags' behaviour.
- files added/changed:
    stdlib/forensics.bas (extracted _submission_flags; added events/events_window),
    examples/forensics_events_test.bas/.out (new golden),
    tests/run_examples.sh (registered example).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * events_window exposes the cluster window as a 3rd-arg peer of flags_window;
      the two-arg events() defaults 90 days (gBASIC has no default params).
    * events() takes a submissions frame, so it works on a single incoming filing
      (a one-row frame) exactly as on a full history — the shape WP-EVT-2's
      monitor needs to classify each polled filing.
    * UNBLOCKS WP-EVT-2 (the watcher monitor): it can now `forensics.events(...)`
      per edgar.poll batch, no companyfacts fetch on the hot path.
### WP-EVT-2 — done-claimed — 2026-07-05
- Built the flagship watcher monitor (edgar_design.md §7) as three files:
    * examples/edgar/monitor_alerts.bas — a PURE shared library (frame/classify/
      severity/line/key) loaded by BOTH the live program and the harness so the
      two cannot drift; the only classification is forensics.events (facts-free,
      the WP-EVT-1 surface — no companyfacts on the hot path).
    * examples/edgar/monitor.bas — the LIVE demo: edgar.session/identify, a
      watched board {inbox, critical, warning}, three watchers, and a
      while-true poll loop (edgar.submissions -> edgar.poll -> append arrivals
      oldest-first -> sleep(poll_seconds)). Hits the SEC network + sleeps; run by
      hand, NOT in the suite. Parse-verified via `--ast` (exit 0, no execution).
    * examples/edgar/monitor_harness_test.bas — the OFFLINE harness (tested):
      IDENTICAL watcher wiring, but arrivals are synthetic append()s to the
      watched inbox — no network, no sleep.
- Watcher design (gbasic-design §9): a watched inbox (the poll queue) + two
  watched alert channels. On each arrival the inbox watcher re-classifies the
  WHOLE rolling history (monitor_alerts.classify -> forensics.events) and routes
  any event whose (accession|kind) key is new to its channel; the channel
  watchers print. Channels are CURSOR-based (crit_seen/warn_seen) so output is
  exact regardless of how watcher drains coalesce. Re-classifying the full
  history (not just the new row) is what makes CLUSTERING visible: a lone 5.02
  raises nothing; the 2nd 5.02 forms the cluster and both members alert.
- commands run:
    GBASIC_PATH=stdlib ./gbasic --ast examples/edgar/monitor.bas   (parse-check, exit 0)
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/edgar/monitor_harness_test.out:
    == EDGAR monitor: synthetic filing stream ==
    [CRITICAL] non_reliance 2024-04-20 acc=0000000000-24-000007 :: 8-K 4.02 non-reliance on prior financials
    [CRITICAL] nt_filing 2024-05-10 acc=0000000000-24-000001 :: late filing: NT 10-K
    [WARNING]  officer_exodus 2024-05-01 acc=0000000000-24-000011 :: 5.02 officer change clustered (2 within 90d)
    [WARNING]  officer_exodus 2024-05-20 acc=0000000000-24-000012 :: 5.02 officer change clustered (2 within 90d)
    == totals ==
    critical: 2 warning: 2
  suite totals: examples PASS: 183 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done when checklist:
    * harness golden green — YES (examples 182 -> 183, added edgar/monitor_harness_test).
      The trace proves the flagship behaviour: benign 10-K raises nothing; an 8-K
      4.02 fires CRITICAL on arrival; NT 10-K fires CRITICAL; the FIRST 5.02
      raises nothing (no cluster yet); the SECOND 5.02 forms the cluster and BOTH
      members alert WARNING — all facts-free, synchronous, on append.
    * the live program is demo-run by Matthew, not tested — YES: monitor.bas is
      excluded from the suite (network + sleep); only parse-verified here.
- files added/changed:
    examples/edgar/monitor_alerts.bas (new shared library),
    examples/edgar/monitor.bas (new live demo),
    examples/edgar/monitor_harness_test.bas/.out (new golden),
    tests/run_examples.sh (registered the harness).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * LEXER GOTCHA hit + worked around: `f(x) = y` in a condition lexes as a
      modifier `(...)=` clause (MOD_LPAREN), so `if monitor_alerts.severity(...) =
      "critical"` fails to parse; bind the call to a temp first, then compare.
    * The live loop appends poll results OLDEST-first so the inbox is chronological
      (poll returns newest-first) — the 5.02 cluster arithmetic depends on order.
    * Only monitor.bas's inbox watcher differs in trigger source (edgar.poll vs
      synthetic append); the classification + routing + channel watchers are
      copied verbatim between the two, and the pure policy is shared via
      monitor_alerts.bas — so "same watcher logic" is structural, not just claimed.
    * Track D (Events + Monitor) is now COMPLETE.

---

## Track E — LLM client

### WP-LLM-1 — done-claimed — 2026-07-04
- Built stdlib/llm.bas — chat-completion client, pure gBASIC over webclient
  (llm_design.md §1–§4):
    * Constructors -> plain copyable model-handle records (§2):
      anthropic(model, key) [ns https://api.anthropic.com],
      openai(model, key) [https://api.openai.com],
      local(base_url, model) [openai wire format, keyless — Ollama/vLLM].
      Handle fields: format, base_url, model, key, keyless, env_var, temperature
      (default 0), max_tokens (1024), timeout (60), retries (3), plus the
      injection seams offline_dir/transport/sleep_fn.
    * Two adapters (§1) as pure functions:
      _build_body (request record -> wire JSON; anthropic carries `system` as a
      top-level field, openai as a leading system message; blank system omitted;
      fixed field order so encode() output is deterministic) and
      _extract (wire -> common response { text, usage:{input,output},
      stop_reason, model, raw }; anthropic input_tokens/output_tokens +
      content[].text concat, openai prompt_tokens/completion_tokens +
      choices[0].message.content). Missing pieces degrade to `unknown` (NA policy)
      via _field/_at guards — bracket reads return unknown but indexing INTO
      unknown raises, so every optional nested read is guarded.
    * chat(m, system, messages) and ask(m, system, prompt) -> string (§3).
    * Key sourcing (§2): key arg, else env(env_var), else error at first call;
      local() is keyless (no auth header).
    * Failure policy (§4): _send retries 429/5xx with exponential backoff
      (1,2,4… capped 30), honoring an integer Retry-After when present; budget
      m.retries (default 3); non-retryable status or exhausted budget RAISES
      (source llm). Transport AND the backoff sleep are INJECTABLE
      (with_transport/with_sleep) so the loop is tested with no network and no
      real waiting. The transport is passed req.attempt so an injected transport
      can vary by attempt while staying stateless.
    * llm.offline(dir) — fixture transport mirroring the edgar seam; returns
      {dir}/{format}_response.json (raises on a missing fixture).
- Fixtures (checked-in, offline): examples/fixtures/llm/anthropic_response.json,
  examples/fixtures/llm/openai_response.json (request/response adapter pairs;
  the "request built" half is the golden of _build_body's output).
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/llm_adapter_test.out:
    == request built (anthropic) ==
    {"model":"claude-sonnet-4-6","max_tokens":1024,"temperature":0,"system":"You are a forensic analyst.","messages":[{"role":"user","content":"Summarize the accruals."}]}
    == request built (openai) ==
    {"model":"gpt-4o","temperature":0,"max_tokens":1024,"messages":[{"role":"system","content":"You are a forensic analyst."},{"role":"user","content":"Summarize the accruals."}]}
    == response parsed (anthropic) ==
    text=Receivables grew faster than sales; watch the accrual.
    usage in=42 out=17
    stop=end_turn model=claude-sonnet-4-6
    == response parsed (openai) ==
    text=Revenue rose but gross margin compressed.
    usage in=31 out=9
    stop=stop model=gpt-4o
    == ask (90% case -> string) ==
    Receivables grew faster than sales; watch the accrual.
- output (verbatim) — examples/llm_retry_test.out (injected flaky transport +
  injected printing sleep; 500 -> exp 1s, 503+Retry-After:5 -> 5s, then 200):
    backoff 1s
    backoff 5s
    result text: done
    usage in=3 out=2
- negative stderr (verbatim):
    tests/negative_llm_retry_exhausted.err:
      runtime error at tests/negative_llm_retry_exhausted.bas:316:21: llm: request failed (HTTP 500) after 3 retries
    tests/negative_llm_offline_miss.err:
      runtime error at tests/negative_llm_offline_miss.bas:217:13: llm: offline fixture missing (expected examples/fixtures/llm_empty/openai_response.json)
  suite totals: examples PASS: 174 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * adapter goldens green for both formats (request built + response parsed from
      checked-in fixture pairs) — YES (llm_adapter_test: anthropic + openai wire
      bodies built to the golden; responses parsed from the two checked-in
      fixtures).
    * usage extraction asserted — YES (anthropic in=42/out=17 from
      input_tokens/output_tokens; openai in=31/out=9 from
      prompt_tokens/completion_tokens).
    * retry test proves 3 backoffs then raise — YES. The positive llm_retry_test
      proves the backoff loop (exponential 1s, then Retry-After 5s honored, then a
      200 success); the negative negative_llm_retry_exhausted proves the budget:
      an always-500 transport backs off 3 times (injected no-wait sleep) then
      raises "after 3 retries".
- files added/changed:
    stdlib/llm.bas (new library),
    examples/fixtures/llm/anthropic_response.json + openai_response.json (new),
    examples/llm_adapter_test.bas/.out, examples/llm_retry_test.bas/.out (goldens),
    tests/negative_llm_retry_exhausted.bas/.err,
    tests/negative_llm_offline_miss.bas/.err,
    examples/llm/smoke_ask.bas (MANUAL live script — needs a key + network, NOT
      registered in run_examples.sh),
    tests/run_examples.sh + tests/run_negative.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface. No core change was needed (env
      already exists; first-class function-refs in record fields are callable, so
      transport/sleep injection is a plain library concern).
    * "3 backoffs then raise" is split across the positive (proves the backoff
      sequence + eventual success, incl. Retry-After honoring) and the negative
      (proves 3 backoffs then the raise). A single golden can't show both a live
      backoff trace on stdout AND the raise, so the pair covers it cleanly.
    * ask_json / structured-output sloppiness handling is WP-LLM-2 (not built).
    * gBASIC constraints rediscovered and worked around: record literals require
      IDENT keys (hyphenated header names like "Retry-After" must be bracket-
      assigned); static field access on a missing field RAISES while bracket reads
      return unknown; indexing INTO unknown raises (hence the _field/_at guards);
      strings aren't index-able (digit check uses mid(s,i,1), 0-based).
### WP-LLM-2 — done-claimed — 2026-07-04
- Extended stdlib/llm.bas with structured output (llm_design.md §3, §7.1):
    * ask_json(m, system, prompt) -> record/list, or unknown. Strips a leading
      ```/```json fence, validates, decodes; on a decode failure issues exactly
      ONE corrective retry ("Your previous reply was not valid JSON. Reply with
      ONLY the JSON …"), then returns unknown.
    * §7.1 open question RESOLVED as the WP directs: the corrective retry is its
      OWN single-shot budget — a fresh chat() with the correction appended — and
      does NOT consume m.retries (which stays the transport-level 429/5xx budget).
    * _strip_fences — removes a start-anchored ```json … ``` fence (the common
      local-model sloppiness); unfenced text passes through.
- WHY A HAND-WRITTEN VALIDATOR (design deviation, forced by the language): gBASIC
  `decode` RAISES on malformed JSON, and I proved empirically that
  `on error resume next` UNWINDS to the top program frame — so a library function
  cannot catch a decode raise and still return cleanly to its caller (the
  caller's assignment is skipped; `x = ask_json(bad)` leaves x unbound). Catching
  only works when the handler sits in the caller's own frame, which would force
  every ask_json caller to wrap the call — unacceptable. Resolution: a
  non-raising recursive-descent JSON validator (_json_valid + _scan_value/
  _scan_object/_scan_array/_scan_string/_scan_number/_scan_lit + _ch/_is_digit/
  _is_hex/_skip_ws), and decode is called ONLY on a validated string, so it never
  raises and ask_json needs no error handling at all.
    * Validator SAFETY cross-checked against decode over a 35-string battery
      (clean/fenced/nested/garbage/edge): every string the validator ACCEPTS,
      decode also accepts — zero "valid-but-decode-raises" cases (the crash
      case). It is at worst STRICTER than decode on three exotic inputs
      (`01`, `1.`, `.5` — gBASIC decode is lenient there); those just yield a
      harmless retry/unknown, never a crash. validator-accepts ⊆ decode-accepts.
- Offline test design: ask_json makes a SECOND chat() for the corrective retry,
  whose request body carries the correction text, so an injected transport
  distinguishes first-vs-retry STATELESSLY via find(req.body, "not valid JSON").
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/llm_ask_json_test.out:
    clean:  verdict=buy score=9
    fenced: verdict=sell score=2
    retry:  ok=true (parsed on the corrective retry)
    garbage: is_unknown=true
    list:   count=3 first=10
  suite totals: examples PASS: 175 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist (tests green; counts recorded):
    * clean JSON — YES (verdict=buy score=9).
    * fenced JSON — YES (```json …``` stripped -> verdict=sell score=2).
    * garbage-then-clean (retry succeeds) — YES (first reply prose+`{oops`,
      corrective retry returns `{"ok":true}` -> ok=true).
    * garbage-then-garbage (unknown) — YES (both replies prose -> is_unknown=true).
    * (extra) a JSON list is a valid result too (count=3, first=10).
- files added/changed:
    stdlib/llm.bas (ask_json + _strip_fences + _parse_or_unknown + the
      non-raising JSON validator),
    examples/llm_ask_json_test.bas/.out (new golden),
    tests/run_examples.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * the validator replaces the design's implied "attempt decode; on failure…"
      try/catch, which gBASIC cannot express for a value-returning library
      function (see the WHY note above). Behaviour matches the design contract
      (clean/fenced parse; one corrective retry; unknown after) — only the
      failure-detection MECHANISM differs, and it is provably crash-safe.
    * ask_json uses NO on-error, so there is no resume-next mode leak into callers.

---

## Track F — MD&A judgment

### WP-MDA-1 — done-claimed — 2026-07-04
- Built stdlib/mdna.bas — 10-K/Q section extraction over the xml module
  (edgar_design.md §5.2 extraction):
    * text(html_text) — the whole-document fallback accessor: xml.parse_html +
      xml.text, with non-breaking spaces (U+00A0) normalized to ordinary spaces.
      This is what the panel (WP-MDA-3) receives when a section can't be isolated.
    * sections(html_text) -> { mdna, risk_factors }. Each field is the section
      text or `unknown` when its header can't be located (best effort, per the
      §5.2 contract). MD&A = Item 7 (ends at Item 7A or 8); Risk Factors = Item 1A
      (ends at Item 1B or 2); nearest following end-header wins; runs to EOF if no
      end-header follows.
    * _section / _find_from helpers.
- HEADING HEURISTIC (the crux): "Item 7." also appears in the table of contents
  and in in-body cross-references ("...as described in Part II - Item 7. ..."),
  so first/last/largest-span matching is wrong (verified against the fixture:
  largest-span picked a cross-reference sentence). The reliable signal is CASE —
  the actual section headers print in UPPERCASE ("ITEM 7.", "ITEM 1A.") while the
  TOC and cross-references use title case ("Item 7."). Confirmed across BOTH
  fixture years that each uppercase "ITEM <n>." occurs EXACTLY ONCE (the real
  header). mdna matches the uppercase form case-sensitively. A filer that does not
  uppercase its headers yields `unknown` (honest best-effort) + the whole-doc
  fallback for the panel; documented in the library header.
- Fixtures — two REAL consecutive-year 10-Ks of ONE filer (Crocs Inc., CIK
  1334036), captured live via tools/edgar_capture.sh --doc, read from disk:
    * tenk_crox_2025_sample.htm — accession 0001334036-26-000006, FY2025
      (period 2025-12-31, filed 2026-02-12), ~2.2 MB.
    * tenk_crox_2024_sample.htm — accession 0001334036-25-000009, FY2024
      (period 2024-12-31, filed 2025-02-13), ~2.3 MB — the PRIOR year, also serving
      WP-MDA-2's YoY diff.
  Both in the MANIFEST. (The pre-existing tenk_10ka_sample.htm from WP-XML-7 is a
  10-K/A amendment with NO Item 7/1A sections — unusable for extraction, hence the
  new captures.) xml.parse_html + xml.text on 2.2 MB runs in ~0.07s.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/mdna_sections_test.out:
    == tenk_crox_2025_sample.htm ==
    mdna [66675 chars]: ITEM 7. Management’s Discussion and Analysis of Financial Condition and Results of
    risk [118498 chars]: ITEM 1A. Risk FactorsYou should carefully consider the following risk factors and
    == tenk_crox_2024_sample.htm ==
    mdna [66879 chars]: ITEM 7. Management’s Discussion and Analysis of Financial Condition and Results of
    risk [111011 chars]: ITEM 1A. Risk FactorsYou should carefully consider the following risk factors and
    == headerless synthetic ==
    mdna unknown? true
    risk unknown? true
    whole-doc fallback text len=51
  suite totals: examples PASS: 176 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * for each fixture year, evidence lists the first ~10 words of each extracted
      section for Matthew's eyeball check — YES (see output; MD&A = "ITEM 7.
      Management's Discussion and Analysis of Financial Condition and Results of…",
      Risk Factors = "ITEM 1A. Risk FactorsYou should carefully consider the
      following risk factors and…" for both years; section lengths 66–67 KB MD&A,
      111–118 KB risk).
    * failure-path test (headerless synthetic HTML -> unknown) — YES (both sections
      unknown; whole-doc fallback still returns the 51-char stripped text).
- files added/changed:
    stdlib/mdna.bas (new library),
    examples/fixtures/edgar/tenk_crox_2025_sample.htm + tenk_crox_2024_sample.htm
      (new real fixtures) + MANIFEST.md,
    examples/mdna_sections_test.bas/.out (new golden),
    tools/edgar_capture.sh (reused --doc mode),
    tests/run_examples.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface. Rides the existing xml module
      (parse_html/text) unchanged.
    * the uppercase-header heuristic is filer-dependent by nature; it is the
      strongest single signal that isolates real headers from TOC/cross-refs, and
      the contract is explicitly best-effort with an `unknown` + whole-doc fallback
      when it doesn't fit a filer. Both Crocs years validate it cleanly.
    * "first ~10 words" rendered via a whitespace-word helper (headers are glued to
      body text after HTML stripping, e.g. "Risk FactorsYou", so the token
      "FactorsYou" is expected and harmless for the eyeball check).
### WP-MDA-2 — done-claimed — 2026-07-04
- Extended stdlib/mdna.bas with the deterministic pre-pass (edgar_design.md §5.2):
    * risk_diff(prior_risk, current_risk) — SENTENCE-granularity YoY diff (choice
      recorded below). Returns { added, removed, added_count, removed_count,
      common_count, prior_count, current_count }; lists/counts are `unknown` when
      either section is missing (NA policy). Sentences split on ". ", whitespace-
      normalized, sub-25-char fragments dropped, de-duplicated.
    * hedge_stats(text) -> { hedge, total, rate } and hedge_rate(text) -> rate,
      over an EDITABLE hedge_lexicon() data list (~55 lowercase whole words).
      _tokenize lowercases + replaces a fixed separator set with spaces + splits.
    * evidence(prior, current, scorecard) — bundles the risk diff, MD&A hedge
      density for both years + the shift, and the SUPPLIED §4.5 forensics
      scorecard (embedded opaquely; mdna does NOT load forensics.bas).
- GRANULARITY CHOICE (recorded per the WP): SENTENCE-level. Item-level (one
  risk-factor caption per unit) is ideal but 10-K captions are a formatting
  convention that does not survive HTML->text stripping, so there is no robust
  delimiter; sentences split deterministically and carry the add/remove signal.
- PERFORMANCE (the real work of this WP): the first cut ran 32s. Two gBASIC traps:
    (1) `append(arr, x)` returns a COPY of the whole array each call, so building a
        10k-word list via append is O(n^2). Fixed by streaming counts.
    (2) far bigger: indexing `arr[i]` inside a `while i < count(arr)` loop
        DEEP-COPIES the whole array every iteration — O(n^2). A 12k-token hedge
        loop took 13s that way; the same loop as `for each w in toks` runs in
        0.15s. Converted every hot loop (hedge, sentences, tokenize, diff,
        norm_ws) to for-each. Full pre-pass over both real 10-Ks now ~0.3-0.5s.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/mdna_prepass_test.out:
    == real risk_diff FY2024->FY2025 ==
    prior_sentences=416 current_sentences=434
    added=137 removed=119 common=297
    == real hedge density (MD&A) ==
    FY2024 hedge=149/10708 per10k=139
    FY2025 hedge=170/10607 per10k=160
    shift per10k=21
    == synthetic risk_diff ==
    added=2 removed=1 common=3
    ADDED: New import tariffs may materially increase our costs
    ADDED: Ongoing litigation risk has increased this year
    REMOVED: Our brand remains concentrated in clogs footwear
    == hedge_rate spot check ==
    hedgy per10k=5000
    plain per10k=0
    == evidence record ==
    risk_added=137 risk_removed=119
    hedge_shift per10k=21
    scorecard carried: m_score=-1.78 red_flags=2
  suite totals: examples PASS: 177 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * diff golden on the two fixture years — YES (FY2024->FY2025: 416->434
      sentences, 137 added / 119 removed / 297 common — reproducible).
    * plus a synthetic pair with known adds/removes — YES (added=2 [tariffs,
      litigation], removed=1 [brand concentrated in clogs], common=3; exact
      sentences asserted).
    * hedge-rate golden — YES (real MD&A 139->160 per-10k, shift +21; spot check
      hedgy 5000 vs plain 0 per-10k).
- files added/changed:
    stdlib/mdna.bas (risk_diff / hedge_stats / hedge_rate / hedge_lexicon /
      evidence + tokenizer/sentence helpers),
    examples/mdna_prepass_test.bas/.out (new golden),
    tests/run_examples.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * the forensics scorecard is a SUPPLIED opaque record (kept mdna decoupled
      from forensics.bas, matching the design's "supplied scorecard"); the test
      passes a representative stub.
    * hedge rates reported in the golden as integer per-10k-words (round(rate*1e4))
      to keep the float display stable; the API returns the raw rate.
### WP-MDA-3 — done-claimed — 2026-07-04
- Extended stdlib/mdna.bas with the panel/referee (edgar_design.md §5.2, §9.6;
  llm_design.md §3, §5). mdna now `load llm from "llm.bas"`.
    * stance_bull / stance_bear / stance_forensic / stance_referee — library
      DEFAULT system prompts (persona only; overridable — a panelist's `stance`
      field is any string). The verdict schema is requested in the USER prompt
      (panel-controlled), so overriding a stance never changes the output contract.
    * panel(panelists, sections, evidence) -> verdict FRAME, one row per analyst:
      columns name, candor, stance_read, evasions, citations, ok. Each panelist is
      { name, model, stance }; panel calls llm.ask_json(model, stance, prompt) and
      reads verdict fields DYNAMICALLY (bracket) so absent fields degrade to
      `unknown` (§9.6). A panelist whose model won't return JSON (ask_json ->
      unknown) becomes an ok=false row of unknowns and the panel CONTINUES.
    * disagreement(verdicts) -> population variance of the numeric candor column
      (unknowns skipped; `unknown` when < 2 valid) — the "make them fight" signal.
    * referee(frontier_model, verdicts) -> { verdict, candor, rationale, dissent,
      ok }; sees only the rendered verdicts; ok=false/unknowns when its reply won't
      parse.
    * _analyst_prompt / _referee_prompt render the MD&A excerpt + evidence as text
      the model can cite; the frontier seat is a config choice (llm_design §5).
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/mdna_panel_test.out (fixture models = llm handles
  with injected transports returning canned verdict JSON; no network):
    == panel verdict frame ==
    rows=4
    bull: ok=true candor=80 evasions=0 read=Brand momentum looks genuine
    bear: ok=true candor=35 evasions=2 read=Downplays tariff and margin pressure
    forensic: ok=true candor=50 evasions=1 read=Numbers and narrative mostly agree
    muddled: ok=false candor=unknown
    == disagreement (candor variance, unknowns skipped) ==
    variance=350
    == referee ==
    ok=true verdict=lean negative candor=45
    rationale=Forensic flags unaddressed; bear case credible
    dissent=candor spread 35 to 80
  suite totals: examples PASS: 178 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * shape/schema tests only over fixture responses — YES (mdna_panel_test drives
      panel/referee with injected-transport llm handles; asserts the verdict-frame
      shape, candor column, evasion counts, referee fields).
    * including one malformed-panelist fixture -> unknown row, panel continues —
      YES (the "muddled" seat returns prose; ask_json -> unknown; row is ok=false /
      candor=unknown and the other three verdicts and the referee are unaffected).
    * one manually-recorded real transcript saved to
      examples/edgar/panel_transcript.md as a worked example, not a test — YES
      (Crocs FY2024->FY2025 walkthrough: local analyst seats + frontier referee,
      the evidence payload, three verdict JSONs, candor variance, and the referee's
      adjudication; clearly labelled illustrative + a "how to reproduce" section,
      since this offline environment has no live model access).
- files added/changed:
    stdlib/mdna.bas (stances + panel + disagreement + referee + prompt helpers;
      added `load llm`),
    examples/mdna_panel_test.bas/.out (new golden),
    examples/edgar/panel_transcript.md (worked example, not a test),
    tests/run_examples.sh (registered).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * Track F is now complete (WP-MDA-1..3). mdna depends on llm (panel over
      ask_json) and on the xml module (extraction); it does NOT depend on
      forensics.bas — the scorecard is supplied opaquely.
    * the transcript's numbers are illustrative (assembled by hand offline); the
      TESTED behaviour is the fixture-driven shape. Matthew can drop a real
      recorded transcript over it after a live run.

---

## Track G — Screener

### WP-SCR-1 — done-claimed — 2026-07-04
- Built stdlib/screener.bas — the whole-market bulk tier (edgar_design.md §4.6,
  §8.5):
    * ingest(db_path, facts_dir) — bulk-load a directory of CIK{10}.json facts into
      a sqlite `universe` table (cik PK, name, latest_period, fact_count,
      ingested_at). Resumable: skips filers already present.
    * ingest_limited(db_path, facts_dir, cap) — ingest at most `cap` NOT-yet-ingested
      filers this call (cap<=0 = unlimited); returns the count ingested this call.
      The interruption hook the test uses.
    * universe(db_path) -> { cik, name, latest_period } frame, ordered by cik.
    * helpers: _cik_from_filename (CIK{10}.json -> "0000320193", stays a string;
      "" for non-CIK files like .gitkeep), _latest_period (max us-gaap fact `end`
      via for-each — not while+arr[i], which is O(n^2)), _summarize, _txt.
- ZIP DECISION (edgar_design.md §8.5, recorded): gBASIC has no DEFLATE / binary-safe
  unzip, so ingest does NOT read the .zip. It consumes an ALREADY-EXTRACTED
  directory (`unzip companyfacts.zip -d dir`; the honest fallback that keeps the
  core untouched — NO new C). No `blocked` — the fallback is sufficient.
- Fixture + builder:
    * tools/screener_sample_build.sh — reads the three full companyfacts_CIK*.json
      captures, truncates each to <=6 concepts x last-4 data points, writes
      examples/fixtures/edgar/companyfacts_sample/CIK{10}.json (the equivalent of
      an extracted, truncated companyfacts.zip). Deterministic, offline, re-runnable.
    * examples/fixtures/edgar/companyfacts_sample/ — AAPL/JPM/CROX, ~3-4 KB each.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim) — examples/screener_ingest_test.out:
    == idempotency ==
    ingest-once  processed=3 rows=3
    ingest-again processed=0 rows=3
    twice == once ? true
    == resumability (cap=1 interruptions) ==
    cap#1: processed=1 rows=1
    cap#2: processed=1 rows=2
    finish: processed=1 rows=3
    no duplication (rows == once) ? true
    == universe ==
    0000019617 | JPMORGAN CHASE & CO | 2026-03-31
    0000320193 | Apple Inc. | 2026-03-28
    0001334036 | CROCS, INC. | 2026-03-31
  suite totals: examples PASS: 179 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done-when checklist:
    * ingest-twice row counts equal ingest-once (asserted) — YES (once=3 rows,
      again processed 0 new, rows still 3; twice==once true).
    * universe golden over the sample — YES (3 filers, cik/name/latest_period;
      AAPL end 2026-03-28, JPM & CROX 2026-03-31).
    * (build) resumability tested by interrupting via a row-count cap injection —
      YES (cap=1 -> 1 row, cap=1 again -> 2 rows, finish -> 3 rows, no duplication).
- files added/changed:
    stdlib/screener.bas (new library),
    tools/screener_sample_build.sh (new fixture builder),
    examples/fixtures/edgar/companyfacts_sample/CIK000000{19617,320193}.json +
      CIK0001334036.json (new truncated sample fixtures),
    examples/screener_ingest_test.bas/.out (new golden),
    tests/run_examples.sh (registered + tmp_screener_{a,b}.db added to cleanup).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * zip handling: consumes an extracted directory (§8.5 fallback) rather than the
      .zip directly — recorded above, NOT blocked.
    * latest_period is the max us-gaap fact `end`; a full walk per filer is the
      cost of ingest (a batch job by design). A corrupt/non-JSON facts file would
      make `decode` raise (gBASIC can't catch-and-continue in a returning function
      — see the WP-LLM-2 note); the real captures are valid, so ingest is clean
      here. Noted as a known at-scale edge.
### WP-SCR-2 — done-claimed — 2026-07-05
- Built the cross-sectional scoring tier (edgar_design.md §4.6) in stdlib/screener.bas
  (now also loads fundamentals + forensics):
    * score_limited(db_path, facts_dir, cap) / score(db_path, facts_dir) —
      INCREMENTAL: compute each filer's LATEST-FY headline scores (Piotroski
      f_score, Beneish mscore + flag, Sloan accrual_ratio, latest-FY FCF) from its
      CIK{10}.json and cache into a sqlite `scores` table keyed by cik. A filer
      already scored is skipped, so re-runs resume and never recompute; cap bounds
      new work per call (the interruption hook). A missing ingredient stays
      `unknown` end-to-end: `unknown` is not a bindable sqlite parameter, so it is
      stored as SQL NULL (_bind: unknown->nothing) and restored on read
      (_unwrap: nothing->unknown; _unflag for the boolean flag 1/0/NULL).
    * scored(db_path) — the scored-universe frame {cik, name, latest_period,
      piotroski, mscore, mscore_flag, accrual_ratio, fcf}, cik-ordered.
    * run(u, fn) — the market screen: applies a predicate FUNCTION VALUE to each
      per-filer row-record and keeps rows where it returns exactly `true` (an
      `unknown`/`false` verdict excludes — an unscored filer is never a hit).
    * unknown_report(db_path) — the concept-map report card: per concept, how many
      scored filers LACK it (the `unknown` long tail), from a per-filer
      unknown-concepts list recorded at score time. { filers, concept[], 
      unknown_count[], rate[] } in concept-map order, every known concept present.
- Fixtures (new): examples/fixtures/edgar/screener_universe/CIK000000000{1,2,3}.json
  — a SYNTHETIC 3-filer universe with distinct, engineered profiles (HEALTHY /
  MANIPULATOR / WEAK) and deliberately uneven concept coverage (only HEALTHY
  carries retained_earnings). Built by a one-off python generator (scratchpad),
  NOT captured. The WP-SCR-1 companyfacts_sample fixtures are TRUNCATED (score to
  all-unknown), so a purpose-built multi-year universe was required to exercise
  scoring — recorded as the fixture decision.
- commands run:
    ./tests/run_examples.sh   (make clean && make first)
    ./tests/run_negative.sh
- output (verbatim, unedited) — examples/screener_score_test.out:
    == incremental scoring (cap=1 interruptions) ==
    step1=1 step2=1 finish=1 rescore=0
    == scored universe ==
    0000000001 | HEALTHY CO (screener fixture) | F=9 M=-2.7 flag=false accr=-0.067 fcf=200
    0000000002 | MANIPULATOR CO (screener fixture) | F=7 M=-0.38 flag=true accr=0.156 fcf=-60
    0000000003 | WEAK CO (screener fixture) | F=2 M=-2.62 flag=false accr=-0.049 fcf=-90
    == screen: quality (F>=7, no M-flag, FCF>0) ==
    HIT 0000000001 | HEALTHY CO (screener fixture)
    hits: 1 of 3
    == unknown concept report (3 filers) ==
    operating_income : 3/3 (1)
    interest_expense : 3/3 (1)
    cash : 3/3 (1)
    total_liabilities : 3/3 (1)
    long_term_debt_current : 3/3 (1)
    retained_earnings : 2/3 (0.67)
    book_equity : 3/3 (1)
    share_based_comp : 3/3 (1)
    stock_repurchased : 3/3 (1)
    dividends_paid : 3/3 (1)
  suite totals: examples PASS: 184 FAIL: 0 ; negative PASS: 223 FAIL: 0
- Done when checklist:
    * screen golden over the sample universe — YES. Scoring is resumable (cap=1
      steps: 1,1,1; a 4th full pass rescores 0 = idempotent). The scores separate
      the profiles as designed (HEALTHY F=9/M=-2.7/FCF+200; MANIPULATOR flag=true;
      WEAK F=2/FCF-90), and run() with a quality predicate selects ONLY the
      healthy filer (manipulator excluded by the M-flag, weak by low F / negative
      FCF): 1 of 3.
    * unknown-tail report golden — YES: 9 concepts fully absent from the fixtures
      (rate 1.0) and retained_earnings at 2/3 (0.67) — the partial-coverage row
      proving the tally is gradated, "the concept map telling the truth."
    * Full-market acceptance against real bulk data — DEFERRED to a Matthew-run
      milestone (per the WP), to be recorded here when done.
- files added/changed:
    stdlib/screener.bas (scoring + scored + run + unknown_report + helpers),
    examples/fixtures/edgar/screener_universe/CIK000000000{1,2,3}.json (new),
    examples/screener_score_test.bas/.out (new golden),
    tests/run_examples.sh (registered example + tmp_screener_scores.db cleanup).
- deviations / notes:
    * pure gBASIC (no C) — no valgrind surface.
    * The §4.6 sketch shows run(u, fn) where u = screener.universe(); WP-SCR-1's
      universe() returns only cik/name/latest_period, so scored() is added as the
      enriched frame and run() is a generic frame-filter over it — the design's
      intent (predicate over per-filer score records) without changing WP-SCR-1.
    * nt_count from the §4.6 example is NOT a bulk-tier score: companyfacts.zip
      carries no submissions, so submissions-side flags (NT/4.02/5.02) are out of
      screener scope by construction. Scores here are facts-only; a caller who
      wants submissions signals joins forensics.events per CIK separately.
    * name-collision fixed: screener's latest-FY helper was renamed _fy_last to
      avoid overriding fundamentals' internal _latest_fy (the interpreter warned).
    * WP-SCR-1's companyfacts_sample stays the ingest fixture; scoring uses the new
      screener_universe/ fixtures.
