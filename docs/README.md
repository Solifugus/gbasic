# gBASIC documentation index

Every document in `docs/` is listed here. `tests/run_docs_gate.sh` fails if one
is missing from this page or if this page links to something that does not
exist, so a new document cannot quietly become invisible.

**Read the status column before you rely on anything.** This repository has
repeatedly been bitten by design proposals read as descriptions of shipped
behaviour — and, worse, by *stale* status lines claiming something was unbuilt
long after it shipped. Six such lines were corrected on 2026-08-15, one of
which had caused a working module to be filed as a release blocker.

| Status | Means |
|---|---|
| **Shipped** | Describes behaviour you can run today, backed by tests. |
| **Proposal** | Describes work that does **not** exist. Do not code against it. |
| **Partial** | Some of it ships; the document says which parts. |
| **Record** | History, plans, notes. Not a description of current behaviour. |

---

## Start here

| Document | Status | What it is |
|---|---|---|
| [tutorial.md](tutorial.md) | Shipped | Learn the language by writing programs. Start here if you are new. |
| [reference.md](reference.md) | Shipped | Syntax, semantics and every builtin. The document to keep open. |
| [gbasic-design.md](gbasic-design.md) | Shipped | Consolidated design of the language and core runtime — the *why*. |

## Tutorials and cookbooks

Task-oriented, with runnable examples.

| Document | Status | What it is |
|---|---|---|
| [chart_cookbook.md](chart_cookbook.md) | Shipped | 10 recipes for charts as deterministic SVG text — lines with honest gaps, date axes, money at face value, zero-anchored bars, histograms, pies that refuse to mislead, correlation heatmaps, sparklines, and the spec layer. Every code and output block verified against a real file by `tests/run_chart_cookbook.sh`. |
| [datetime_cookbook.md](datetime_cookbook.md) | Shipped | 10 recipes for dates, durations, business calendars, date expressions and scheduling — month-end arithmetic, "third Thursday", payroll rolled off holidays, mutual meeting days, convention layout, appointment slots. Every code and output block verified against a real file by `tests/run_datetime_cookbook.sh`. |
| [money_cookbook.md](money_cookbook.md) | Shipped | 9 recipes over `money` and the `finance` library — exact amounts, 178 currencies with their own decimal places, arithmetic that keeps the cents, splitting a bill so the parts add up, dated currency conversion, loan payments and amortization, project appraisal, depreciation. Every code and output block verified against a real file by `tests/run_money_cookbook.sh`. |
| [odbc_cookbook.md](odbc_cookbook.md) | Shipped | 8 recipes for databases over ODBC — SQL Server, MySQL, Oracle, DB2 and the rest through one module: bound parameters and why pasting is not a style question, `rows_affected`, the exact-number rule (`bigint`/`decimal` arrive as strings), NULL vs the empty string, transactions, telling an absent driver from a refused login, and query results straight into a frame. Every code and output block verified against a real file by `tests/run_odbc_cookbook.sh`. Exercised against the SQLite3 driver only so far. |
| [xlsx_cookbook.md](xlsx_cookbook.md) | Shipped | 12 recipes for spreadsheets: read, edit, save, evaluate formulas, check against Excel's own answers, turn sheets into queryable tables. Every code and output block is verified against a real file by `tests/run_xlsx_cookbook.sh`. |
| [edgar_tutorial.md](edgar_tutorial.md) | Shipped | Build a forensic dossier on a filer, plus recipes per library. |
| [cookbook_social_behavioral.md](cookbook_social_behavioral.md) | Shipped | Statistics by method cluster: social and behavioral sciences, through survival, meta-analysis and factor analysis. |
| [cookbook_econometrics_finance.md](cookbook_econometrics_finance.md) | Shipped | Statistics by method cluster: econometrics and finance, through event studies and causal inference. |

## Language features

| Document | Status | What it is |
|---|---|---|
| [pbi_design.md](pbi_design.md) | Shipped | Policy-Based Inheritance — gBASIC's object model (`copy`/`link`/`reset`/`exclude`, `new`). |
| [first_class_functions_design.md](first_class_functions_design.md) | Shipped | Function values, methods via `this`, `constructor`. Note: **no closures**. |
| [unicode_design.md](unicode_design.md) | Shipped | Binary-safe, UTF-8-aware strings; codepoint and byte operations. |
| [multiprocessing_design.md](multiprocessing_design.md) | Shipped | Shared-nothing actors over fork+exec; `spawn`/`send`/`receive`/`monitor`. |
| [brace_modifier_design.md](brace_modifier_design.md) | Shipped | Modifier clauses in braces — `x{USD} = 19.95`. Retires the paren spelling and, with it, the clause-recognition residual that could not be closed at token delivery. |
| [warning_model_design.md](warning_model_design.md) | Shipped | The warning channel: `on warning print/ignore/goto next/stop`, `if warning then`, and the `unused-result` diagnostic it makes affordable. |
| [error_model_design.md](error_model_design.md) | Shipped | Frame-scoped `on error goto next`/`goto label`; catch-and-return, structured raises, traces. Replaces the deleted `on error resume next`. |
| [text_design.md](text_design.md) | Partial | Layer 0 (regex as a value kind) ships. Layer 1 is `stdlib/ari.bas`. |
| [ari_spec_language.md](ari_spec_language.md) | Shipped | The ARI spec language for parsing paginated print-image reports. |
| [bitwise_design.md](bitwise_design.md) | Shipped | `band`/`bor`/`bxor`/`bnot`/`shl`/`shr`/`rotl`/`rotr`, 32-bit unsigned. |
| [crypto_design.md](crypto_design.md) | Shipped | Hashing, HMAC, AES-GCM, Ed25519, plus `stdlib/crypto.bas` (JWT, CSRF, signed cookies). |
| [mail_design.md](mail_design.md) | Shipped | Composing RFC 5322 messages (`stdlib/mail.bas`) and sending them over SMTP with TLS and auth. |
| [array_cow_design.md](array_cow_design.md) | Shipped | Why arrays are shared, refcounted and copy-on-write. |
| [source_outline_design.md](source_outline_design.md) | Shipped | `source_outline(text)` — structural outline over the reentrant parser. |
| [gbasic_clause_recognition.md](gbasic_clause_recognition.md) | Shipped | How clause recognition was ruled on and implemented. |

## Modules

| Document | Status | What it is |
|---|---|---|
| [xlsx_design.md](xlsx_design.md) | Partial | The spreadsheet engine: ZIP container, part tree, formula evaluator, recalculation, the SQL compiler — plus the corpus measurements and what was rejected. Says which parts are unbuilt. |
| [xml_design.md](xml_design.md) | Shipped | Tree parse, path navigation, and a streaming reader for large documents. |
| [sqlite_design.md](sqlite_design.md) | Shipped | `sqlite.*` — parameterized query and exec. |
| [postgres_design.md](postgres_design.md) | Shipped | `pg.*` — PostgreSQL. Its test suite is opt-in. |
| [plat-web-lowering-study.md](plat-web-lowering-study.md) | Done | PLAT-WEB-0: both design examples hand-lowered onto the existing web library. The shape is right; six runtime gaps named, mapped onto the planned phases; response-by-return resolved. |
| [plat-web-design-draft.md](plat-web-design-draft.md) | Draft | PLAT-WEB — a declarative `server` block (named hosts, routes, TLS, worker pool, zero-downtime reload) as a possible replacement for the current web layer. Revised against the runtime 2026-08-20; nothing committed. |
| [webclient_design.md](webclient_design.md) | Shipped | `webclient.*` — synchronous HTTP. |
| [webserver_design.md](webserver_design.md) | Shipped | `webserver.*` — the HTTP listener. Binds loopback unless an options record asks for an address. |
| [web_routing.md](web_routing.md) | Shipped | `stdlib/web.bas` — a route table as data: patterns, order-independent specificity, `web.dispatch`. The library layer of PLAT-WEB. |
| [llm_design.md](llm_design.md) | Shipped | `stdlib/llm.bas` — chat completion over `webclient`. |
| [gui_design.md](gui_design.md) | Partial | The GTK 3 `gui` module — an experimental proof of concept. Prefer `gi` for new work. |
| [gui_tutorial.md](gui_tutorial.md) | Shipped | Desktop applications with GTK 4: the four layers, the no-closures rule, signals, the event loop, testing without a display. |
| [gui_cookbook.md](gui_cookbook.md) | Shipped | GUI by task; every recipe executed by tests/run_gui_cookbook.sh. |
| [chart_design.md](chart_design.md) | Partial | The charting library — deterministic SVG as text. Phases 1, 2 and 4 are built (`stdlib/chart.bas`, `docs/chart_cookbook.md`); §13 records what is deferred. |
| [datetime_design.md](datetime_design.md) | Shipped | The datetime/duration redesign — precision-aware datetimes, accountant's month arithmetic, duration algebra, business calendars, `matches`/`select`/`series`, `schedule.bas`. Every planned v1 layer is built; §9 lists what is deferred by decision (timezones, business-hours arithmetic). |
| [money_design.md](money_design.md) | Shipped | The `money` type: four verified defects (no exact constructor, `*`/`/` leaving integer arithmetic, cents hardcoded, an undefined `.5` rule), and the ruled design — ISO 4217 currency on the value, per-currency exponent plus four guard digits. All five phases shipped 2026-08-29; the record of what each one found is in §8a-8f. |

## Statistics

| Document | Status | What it is |
|---|---|---|
| [statistics_design.md](statistics_design.md) | Shipped | Distributions, matrices, OLS, GLMs, clustering, time series through ARIMA/GARCH, data frames. |
| [statistics_scientist_plan.md](statistics_scientist_plan.md) | Record | The plan that took the library to "usable by working scientists", and the field expansion past it (survival, meta-analysis, factor analysis, event studies, causal inference). |

## EDGAR securities-analysis suite

| Document | Status | What it is |
|---|---|---|
| [edgar_reference.md](edgar_reference.md) | Shipped | Every public function and its return shape. |
| [edgar_design.md](edgar_design.md) | Partial | Domain primer, model rationale, applicability caveats. Names the parts deliberately not built (network 13F, 13D/G full-text search, grants/exercises). |
| [edgar_suite_development_plan.md](edgar_suite_development_plan.md) | Record | The 33 work packages and the session protocol they were built under. |
| [PROGRESS.md](PROGRESS.md) | Record | Per-work-package evidence ledger for the suite. |

## Native application platform and GUI

| Document | Status | What it is |
|---|---|---|
| [gbasic_native_app_platform_plan.md](gbasic_native_app_platform_plan.md) | Partial | The NAP plan. The platform items (NAP-0..13) ship; read the document for which. |
| [gbasic_native_app_platform_coverage.md](gbasic_native_app_platform_coverage.md) | Record | Capability survey against other toolkits. |
| [gbasic_execution_boundaries.md](gbasic_execution_boundaries.md) | **Proposal** | Execution-boundary specification for gBASIC Studio. Not implemented here. |

## The gBASIC site (sample application)

| Document | Status | What it is |
|---|---|---|
| [gbasic_site_plan.md](gbasic_site_plan.md) | Record | Plan for the Postgres-backed sample site. |
| [gbasic_site_auth_plan.md](gbasic_site_auth_plan.md) | Shipped | Auth, sessions and CSRF for that site. |
| [gbasic_site_deployment.md](gbasic_site_deployment.md) | Record | Deployment and hardening notes. |

## For AI agents writing gBASIC

Read in this order. gBASIC diverges from QBasic/VB intuition in ways that fail
*silently*, which is what these exist for.

| Document | Status | What it is |
|---|---|---|
| [ai/START-HERE.md](ai/START-HERE.md) | Shipped | The entry point. Read it first. |
| [ai/UNLEARN.md](ai/UNLEARN.md) | Shipped | What your BASIC intuition gets wrong here. |
| [ai/COOKBOOK.md](ai/COOKBOOK.md) | Shipped | One blessed idiom per task, each pointing at a suite-verified file. |
| [ai/ERRORS.md](ai/ERRORS.md) | Shipped | Diagnostic and runtime-error catalog. |

## Project record

| Document | Status | What it is |
|---|---|---|
| [project_state.md](project_state.md) | Record | Where the project stands. |
| [shipping_applications.md](shipping_applications.md) | Shipped | Turning a .bas program into a .deb a customer installs: lean builds, layout, the library-search hazard, systemd hardening. |
| [historical_development_archive.md](historical_development_archive.md) | Record | Completed phases, so retired trackers need not stay active. |
| [fake_data_design.md](fake_data_design.md) | **Proposal** | `stdlib/fake.bas` — fabricated but realistic data for tests and demos. **Nothing here is built.** The design turns on three measured facts: gBASIC's RNG is xoshiro256 and so reproducible across platforms; a record is a value, so a stateful stream cannot advance itself; hence the API is pure functions of `(seed, index)`. Split in two layers because *realistic* is relative to a purpose while *fake* is not: domain-neutral **values** (roughly what Python's Faker provides) under domain-shaped **datasets** with referential, temporal, arithmetic and intra-record consistency — the half a value generator cannot reach. Distributions, planted anomalies and structural inability to collide with real people are the parts that matter. |
| [future_library_ideas.md](future_library_ideas.md) | **Proposal** | Directions not committed to. |
| [accounting_cookbook.md](accounting_cookbook.md) | Shipped | Worked recipes for `stdlib/accounting.bas`: chart and first entries, a consultancy's month end to end, closing a period and why it refuses twice, and what is refused with the reason each matters. On the cannot-lie harness (`tests/run_accounting_cookbook.sh`). |
| [accounting_design.md](accounting_design.md) | Shipped | Double-entry accounting — chart of accounts, balanced journal entries, ledger, trial balance, the two statements, period closing. Phase 2 of the platform proposal, reordered ahead of lending on 2026-08-30 because double-entry is the substrate lending posts into. **Nothing here is built.** |
| [finance_cookbook.md](finance_cookbook.md) | Shipped | Worked recipes for `stdlib/finance.bas`: the five time-value solvers in Excel argument order, nominal/effective/periodic rates, amortization, NPV/IRR, dated flows (`xnpv`/`xirr`), day-count conventions, multiple-IRR detection, and the optional `fv`/`timing` tail. On the cannot-lie harness — every block is a real file and every output a golden (`tests/run_finance_cookbook.sh`). |
| [finance_design.md](finance_design.md) | **Proposal** | Core finance conventions — the design document `stdlib/finance.bas` never had. Rules the public API onto **Excel argument order** (decided 2026-08-29), states the one time-value equation, the sign convention, the money/number split, and the two-form API that gBASIC's lack of optional parameters forces. Phase 1 of the platform proposal; the migration is 21 in-repo call sites and its cost is stated in §2. |
| [gbasic_finance_business_platform_proposal.md](gbasic_finance_business_platform_proposal.md) | **Proposal** | A long-term program to make gBASIC a finance and business platform: 18 libraries over 7 phases, built on exact `money`, dates and `stats`. **Nothing here is built.** `stdlib/finance.bas` is the existing foundation it extends, and only the Phase 1 backlog (`rate`, generalized TVM, day counts, XNPV/XIRR) is proposed for approval — later workstreams need their own design review, and several need a domain reviewer this project does not yet have. |
| [TOKENS.md](TOKENS.md) | Record | Token inventory. |
| [gbasic_dogfood_notes.md](gbasic_dogfood_notes.md) | Record | Friction found using gBASIC on real work. See also `/DOGFOOD.md`. |
| [tedderland_dogfood_notes.md](tedderland_dogfood_notes.md) | Record | Friction found building tedderland.com in gBASIC. |
| [s390x-vm-setup.md](s390x-vm-setup.md) | Record | s390x VM setup for portability testing. |

---

Not in this directory but worth knowing:

- [`/DOGFOOD.md`](../DOGFOOD.md) — every limitation and surprise hit while
  actually using gBASIC, with the workaround. Often more useful than a design
  document, because it records what went wrong rather than what was intended.
- [`/CHANGELOG.md`](../CHANGELOG.md) — what shipped in each release.
- [`/CONTRIBUTING.md`](../CONTRIBUTING.md) — how to build and test, and the
  house rules. Note code contributions are not being merged yet, pending a CLA.
- [`/LICENSING.md`](../LICENSING.md) — which files are Apache-2.0 and which are
  AGPL-3.0, and why the line falls where it does. Enforced by the docs gate.
- [`/LICENSE`](../LICENSE), [`/LICENSE.AGPL-3.0`](../LICENSE.AGPL-3.0) and
  [`/NOTICE`](../NOTICE) — the licence texts.
