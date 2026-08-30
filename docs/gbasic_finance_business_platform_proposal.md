# gBASIC Finance and Business Platform

**Status:** Proposal  
**Audience:** gBASIC maintainers, contributors, domain reviewers, and application developers  
**Primary implementation language:** gBASIC standard-library code, with C primitives only where measurement demonstrates a need  
**Location:** `docs/gbasic_finance_business_platform_proposal.md`

## Summary

This proposal defines a long-term program of work to make gBASIC an exceptional language and application platform for finance and business. The goal is broader than adding spreadsheet-style financial functions. gBASIC should make financial and operational programs readable, exact, convention-aware, auditable, reproducible, and difficult to misuse.

The existing `stdlib/finance.bas` becomes the universal foundation for time value of money, interest rates, cash flows, financial schedules, day-count conventions, and depreciation. Specialized libraries build on that foundation for lending, deposits, accounting, fixed income, portfolios, derivatives, risk, insurance, treasury, payments, corporate finance, sales, forecasting, optimization, and operations.

This work should compose with capabilities already present in gBASIC: exact currency-bearing `money`, dates and durations, `unknown`, records and arrays, statistics and econometrics, frames, databases, spreadsheets, charts, web services, cryptography, persistence, EDGAR analysis, and native GUI support.

## Motivation

Finance and business software is often written in a mixture of spreadsheets, SQL, scripting languages, proprietary systems, and undocumented formulas. This creates recurring problems:

- amounts are confused with dimensionless numbers;
- percentages, rates, spreads, and basis points are mixed accidentally;
- annual and periodic rates are applied inconsistently;
- date, calendar, accrual, settlement, and rounding conventions remain implicit;
- calculations return plausible numbers even when given incompatible inputs;
- changing regulatory rules are embedded without effective dates or sources;
- spreadsheets obscure data lineage and make testing difficult;
- numerical solvers return an answer without exposing convergence or ambiguity;
- reports cannot be traced cleanly back to inputs and calculation versions;
- operational workflows are separated artificially from the financial calculations they drive.

gBASIC is unusually well positioned to address these problems. It combines approachable syntax with typed values, exact money, business dates, databases, spreadsheets, statistics, and application-building facilities. A finance professional, auditor, analyst, or operations manager should be able to read a gBASIC program and recognize both the business rule and the conventions under which it operates.

## Vision

A gBASIC financial program should read like an explicit statement of business intent:

```basic
' PROPOSED. `lending.*`, `finance.nominal_rate` and `finance.months` do not
' exist yet; this is the shape being argued for, not working code.
principal {USD}= "250000.00"

loan = lending.loan({
    principal: principal,
    rate: finance.nominal_rate(0.0625, 12),
    term: finance.months(360),
    payment_timing: "end",
    day_count: "actual/365",
    rounding: "half-even"
})

quote = lending.quote(loan)
print quote.payment
print quote.apr
```

Money is built with the currency modifier — `principal {USD}= "250000.00"` —
which is the only way to get an exact amount, so it happens on its own line
before the record rather than inside it.

Today the same calculation is one call, and it already enforces the first
design principle below: `finance.pmt` **raises** if the principal is a plain
number rather than money.

```basic
load finance
principal {USD}= "250000.00"
print finance.pmt(principal, 0.0625 / 12, 360)     ' -1539.29, and still money
```

What the proposal adds is not the arithmetic. It is that the rate says whether
it is nominal or periodic, the term says what a period is, and the answer
carries the conventions it was computed under.

The result should be usable directly while retaining enough explanation for review:

```basic
print quote.value
print quote.inputs
print quote.conventions
print quote.warnings
print quote.as_of
```

This combination of readability and auditability should be the distinguishing feature of the platform.

## Design principles

### 1. Money remains money

Amounts use gBASIC's exact, currency-bearing `money` type. Rates and ratios are dimensionless values. Functions must reject incompatible currencies unless an explicit exchange rate or conversion policy is supplied.

### 2. Conventions are explicit

Financial answers depend on compounding frequency, payment timing, day count, business-day adjustment, holiday calendar, settlement policy, and rounding. APIs must not guess when reasonable conventions produce materially different answers.

### 3. Safe defaults are documented defaults

A default may be provided when a convention is truly dominant and harmless, but it must be documented and visible in an explanatory result. Silence must not conceal a consequential choice.

### 4. Exact where exact; numerical where necessary

Cash amounts and ledger balances remain exact. Root-finding, optimization, statistical estimation, and market models may use floating-point arithmetic, but must report convergence, tolerances, failure, and ambiguity.

### 5. Wrong-looking inputs must not produce right-looking answers

The library should reject impossible schedules, mismatched currencies, invalid rates, non-monotonic dated cash flows, unsupported conventions, and nonconvergent calculations. `unknown` is appropriate for unavailable analytical results; `error` is appropriate for a violated API contract.

### 6. Calculation and policy are separate

Universal mathematics belongs in stable libraries. Laws, tax tables, accounting mandates, disclosure rules, file standards, and regulatory thresholds belong in versioned jurisdiction-specific packages with sources and effective dates.

### 7. Results can explain themselves

Important calculations should offer rich result records containing the answer, inputs, conventions, intermediate schedules, warnings, solver information, formula version, and as-of date. Simple scalar convenience functions may coexist with explanatory variants.

### 8. Compositions stay in gBASIC

Following the existing standard-library architecture, business and financial compositions should be written in gBASIC. New C primitives are justified only by missing low-level capabilities or measured performance constraints.

### 9. Interoperability is part of correctness

Where a de facto standard exists, results should be compared against multiple independent implementations such as Excel, LibreOffice, NumPy Financial, QuantLib, SciPy, statsmodels, actuarial references, published examples, or authoritative regulatory worksheets. Compatibility modes must be named rather than silently emulated.

### 10. Operations and analytics belong together

The platform should support the complete path from incoming transaction or business event through calculation, accounting, risk analysis, decision, approval, persistence, and reporting.

## Proposed library architecture

`finance.bas` should grow substantially, but it should not become a monolith. It is the dependency-light mathematical foundation on which specialized libraries build.

| Library | Responsibility |
|---|---|
| `finance` | Rates, time value, cash flows, conventions, financial schedules, depreciation |
| `lending` | Loans, amortization, servicing, underwriting, credit analysis |
| `deposits` | Deposit interest, dividends, CDs, tiering, cost of funds |
| `fixedincome` | Bonds, yields, duration, convexity, curves, spreads |
| `portfolio` | Holdings, lots, performance, attribution, rebalancing |
| `derivatives` | Forwards, futures, options, swaps, Greeks |
| `risk` | Market, credit, liquidity, interest-rate, and operational risk |
| `insurance` | Premiums, claims, reserves, loss development, reinsurance |
| `actuarial` | Survival, mortality, annuities, life contingencies, credibility |
| `accounting` | Double entry, ledgers, statements, receivables, payables, costing |
| `treasury` | Cash position, funding, debt, liquidity, hedging, bank reconciliation |
| `payments` | Payment instructions, settlement, reconciliation, payment formats |
| `businessfinance` | Capital budgeting, valuation, unit economics, profitability |
| `sales` | Pipeline, pricing, contracts, commissions, customer economics |
| `operations` | Capacity, inventory, supply chain, quality, service levels |
| `forecast` | Business forecasting, backtesting, accuracy, reconciliation |
| `optimization` | Allocation, scheduling, routing, portfolio, pricing, inventory |
| `audit` | Reconciliation, sampling, anomalies, lineage, evidence trails |

Existing libraries remain authoritative in their established areas:

- `stats` supplies probability, inference, econometrics, time series, market risk metrics, and causal analysis;
- `dates` supplies general date and business-calendar facilities;
- `market` supplies normalized price history;
- `frame`, `grid`, `consolidate`, and `dbframe` supply business-data structures and ingestion;
- `edgar` and its companion libraries supply public-company acquisition and analysis;
- `chart`, `xlsx`, database modules, `persist`, `web`, and GUI libraries supply application and reporting infrastructure.

Specialized libraries should depend on these facilities instead of duplicating them.

## Workstream A: Extend `finance.bas`

### Time value of money

- complete the five core solvers with `rate`;
- generalize `pv` and `fv` to include periodic payments and terminal balances;
- support beginning- and end-of-period payments;
- ordinary, due, deferred, growing, and perpetuity annuities;
- simple, periodic, and continuous compounding;
- discount and accumulation factors;
- real and nominal value conversion;
- inflation-adjusted cash flows;
- doubling time and capital recovery factors.

The existing short-form functions should remain available where compatibility permits. Generalized functions should make all sign and timing conventions explicit.

### Rate representation and conversion

- nominal and effective annual rates;
- periodic rates and compounding frequencies;
- APR and APY foundations;
- continuously compounded rates;
- discount, money-market, bank-discount, and bond-equivalent yields;
- basis-point and spread conversions;
- rate equivalence across frequencies;
- real rate and inflation relationships.

A later language-design investigation should consider semantic modifiers or domain objects for annual rates, periodic rates, percentages, and basis points.

### Cash-flow analysis

- NPV and IRR;
- MIRR;
- XNPV and XIRR for dated flows;
- detection and reporting of multiple possible IRRs;
- discounted and undiscounted payback periods;
- profitability index;
- equivalent annual value;
- present worth and future worth;
- break-even discount rates;
- sensitivity tables and scenario evaluation.

### Financial dates and conventions

- Actual/360;
- Actual/365 Fixed and Actual/365L;
- Actual/Actual ISDA and ICMA;
- 30/360 US, 30E/360, and 30E/360 ISDA;
- following, modified following, preceding, and modified preceding adjustment;
- end-of-month behavior;
- settlement and coupon schedule generation;
- regular and stub periods;
- configurable holiday calendars.

These facilities should build on `dates.bas`, while `finance.bas` defines financial interpretation.

### Depreciation and amortization

- retain straight-line, sum-of-years-digits, and double-declining methods;
- add fixed and variable declining balance;
- units-of-production schedules;
- partial-period conventions;
- book-value schedules;
- intangible-asset amortization;
- premium, discount, and fee amortization.

Tax-specific depreciation belongs in versioned jurisdiction packages.

## Workstream B: Lending and deposits

### Lending products

- installment, mortgage, auto, commercial, bridge, construction, and balloon loans;
- interest-only and graduated-payment periods;
- daily simple-interest and negative-amortization products;
- adjustable-rate loans with caps, floors, indexes, margins, and reset dates;
- lines of credit and revolving credit;
- leases and lease-versus-loan comparisons.

### Loan schedules and servicing

- exact amortization schedules with declared rounding policy;
- irregular and extra payments;
- payment holidays and modifications;
- rate and payment changes;
- fees financed into principal;
- payoff quotes and per-diem interest;
- recasting and refinancing comparisons;
- partial-payment waterfalls;
- delinquency aging, cure, roll-rate, charge-off, and recovery analysis.

### Underwriting and credit

- LTV, CLTV, DTI, PTI, DSCR, and coverage measures;
- collateral and borrowing-base calculations;
- global cash flow;
- PD, LGD, EAD, expected loss, and risk-adjusted return;
- loan pricing to a target return;
- vintage, cohort, migration, and survival analysis.

### Deposit products

- savings, checking, tiered-rate, and certificate accounts;
- daily balance, average daily balance, and minimum balance methods;
- interest and credit-union dividend calculations;
- compounding and crediting schedules;
- maturity and early-withdrawal calculations;
- CD ladders and renewal projections;
- blended rates, deposit beta, decay, attrition, and cost of funds.

## Workstream C: Markets and investments

### Fixed income

- clean and dirty bond prices;
- accrued interest;
- YTM, YTC, YTP, YTW, current yield, and tax-equivalent yield;
- zero-coupon, floating-rate, step-up, callable, putable, and amortizing instruments;
- Macaulay, modified, effective, key-rate, and spread duration;
- DV01/PV01, convexity, weighted-average life, Z-spread, and OAS;
- spot, discount, par, and forward curves;
- curve bootstrapping, interpolation, shocks, and Nelson-Siegel families.

### Portfolio accounting and performance

- positions, lots, cost basis, realized and unrealized gains;
- FIFO, LIFO, average cost, and specific identification;
- dividends, interest, splits, spin-offs, mergers, and return of capital;
- multi-currency valuation;
- time-weighted, money-weighted, and Modified Dietz returns;
- contribution and Brinson-style attribution;
- benchmark-relative analytics and fee-adjusted performance;
- tax-aware and constraint-aware rebalancing.

### Portfolio construction

- covariance and correlation workflows;
- minimum variance, maximum Sharpe, efficient frontier, and risk parity;
- target return and target volatility;
- sector, asset, position, turnover, and transaction-cost constraints;
- Black-Litterman and scenario-based construction.

### Derivatives

- forward, futures, and cost-of-carry pricing;
- Black-Scholes, Black 76, trees, and Monte Carlo valuation;
- implied volatility and Greeks;
- American, Asian, barrier, and digital options;
- swaps, forward-rate agreements, caps, floors, and swaptions;
- currency and commodity contracts;
- scenario P&L and volatility surfaces.

Derivatives should follow the stable delivery of rates, dates, curves, solvers, and fixed income.

## Workstream D: Accounting, treasury, and payments

### Accounting

- charts of accounts and account classifications;
- balanced journal entries with debit/credit validation;
- posting, trial balances, periods, adjustments, closing, and reversals;
- general and subsidiary ledgers;
- multi-entity, multi-currency, consolidation, and eliminations;
- balance sheet, income, cash-flow, and equity statements;
- receivables, payables, aging, discounts, partial payments, and credit memos;
- FIFO, LIFO, weighted average, standard cost, job cost, and process cost;
- budgets, forecasts, cost centers, departments, segments, and variance analysis;
- standard liquidity, profitability, efficiency, leverage, and coverage ratios.

### Treasury

- cash position and forecasting;
- account reconciliation;
- sweeps, pooling, concentration, and intercompany funding;
- debt and covenant schedules;
- funding and short-term investment optimization;
- FX and interest-rate exposure;
- hedging records and effectiveness analysis;
- counterparty limits and bank-fee analysis.

### Payments

- payment batches, instructions, statuses, reversals, and returns;
- settlement dates, cutoffs, fees, and routing;
- duplicate detection and idempotency;
- reconciliation and exception handling;
- approval workflows and transaction limits;
- adapters for ACH/NACHA, Fedwire, SWIFT, ISO 20022, cards, checks, and fixed-width partner formats.

Message formats and changing network rules should be versioned adapters rather than embedded in core financial mathematics.

## Workstream E: Risk, insurance, and compliance

### Risk

- historical, parametric, and Monte Carlo VaR;
- expected shortfall, marginal risk, component risk, and backtesting;
- stress and scenario testing;
- credit transition, migration, concentration, and expected-loss analysis;
- liquidity gaps, cash-flow ladders, runoff scenarios, and survival horizons;
- net-interest-income and economic-value-of-equity simulation;
- repricing and duration gaps, rate shocks, and curve twists;
- operational loss frequency/severity and key-risk indicators.

Existing statistical risk functions remain in `stats`; `risk` composes them into institutional models and reports.

### Insurance and actuarial work

- mortality tables, survival probabilities, life expectancy, and life contingencies;
- annuity factors, benefits, premiums, reserves, lapse, and surrender assumptions;
- claim frequency and severity;
- earned/written premium, loss ratio, expense ratio, and combined ratio;
- loss-development triangles, chain ladder, Bornhuetter-Ferguson, and IBNR;
- credibility, experience rating, deductibles, limits, coinsurance, and reinsurance layers;
- underwriting, renewal, and policy-profitability analysis.

### Audit and compliance mechanisms

- reconciliation, sampling, sequence gaps, duplicates, and round-dollar tests;
- Benford and anomaly analysis;
- related-party and suspicious-pattern hooks;
- segregation of duties and approval limits;
- calculation provenance and data lineage;
- immutable or cryptographically verifiable audit records;
- policy version, jurisdiction, source, and effective-date metadata.

The platform must not claim legal or regulatory compliance merely because a calculation exists.

## Workstream F: Corporate finance, sales, and operations

### Corporate and business finance

- break-even, contribution margin, and operating leverage;
- WACC, cost of capital, capital budgeting, and terminal value;
- DCF, dividend, comparable-company, and transaction valuation;
- working capital and cash-conversion cycle;
- make/buy, lease/buy, and outsource/insource comparisons;
- product, customer, branch, store, and contract profitability;
- unit economics, CAC, LTV, churn, retention, MRR, ARR, and runway.

### Sales

- leads, opportunities, stages, probabilities, and weighted pipeline;
- conversion, velocity, sales-cycle, quota, territory, and win/loss measures;
- price lists, tiers, volume discounts, bundles, and promotions;
- margin floors, discount approval, contract pricing, escalators, renewals, and proration;
- commissions and crediting;
- cohorts, segmentation, retention, cross-sell, and customer concentration.

### Operations and supply chain

- demand, capacity, workforce, utilization, throughput, queue, and service levels;
- reorder points, safety stock, EOQ, lead-time demand, fill rate, and days of supply;
- inventory classification, purchase planning, supplier scoring, and warehouse measures;
- transportation, assignment, routing, and load calculations;
- control charts, capability, defect rates, reliability, MTBF, MTTR, and OEE.

## Workstream G: Forecasting and optimization

### Forecasting

- moving averages and exponential smoothing;
- Holt, Holt-Winters, decomposition, and intermittent-demand methods;
- regression and scenario forecasts;
- ARIMA and related methods through `stats`;
- rolling-origin validation and backtesting;
- MAE, RMSE, MAPE, sMAPE, and MASE;
- hierarchical forecast reconciliation;
- prediction intervals and explicit treatment of uncertainty.

### Optimization

- linear and integer programming;
- goal and constraint programming;
- transportation, assignment, network-flow, and knapsack problems;
- portfolio, inventory, capacity, scheduling, pricing, and routing optimization;
- constraints, infeasibility reports, sensitivity, and scenario comparison.

This work likely requires an adapter to a mature solver before considering native implementations.

## Cross-cutting platform requirements

### Domain values

Investigate first-class or modifier-based representations for:

- rates and compounding;
- percentages and basis points;
- quantities, units, and unit prices;
- exchange rates and currency pairs;
- periods and tenors;
- probabilities;
- account, security, and transaction identifiers.

The investigation must distinguish capabilities that belong in the language from record-based conventions that can remain in the standard library.

### Currency and foreign exchange

- explicit currency compatibility;
- exact conversion with supplied rates;
- rate direction and inversion;
- triangulation policies;
- as-of timestamps and source metadata;
- realized and unrealized FX effects;
- functional, transaction, and reporting currencies.

### Rounding

- half-even, half-up, half-away, floor, ceiling, and truncation;
- currency minor units;
- per-line, per-period, subtotal, and final rounding;
- residual allocation;
- preservation of the declared rounding policy in result metadata.

### Scenario and sensitivity framework

Libraries should share a small convention for named scenarios, assumptions, shocks, outputs, and comparisons instead of creating incompatible scenario representations independently.

### Explainable result convention

Rich results should use consistent field names where applicable (a shape, not
a statement — a bare record literal is not a gBASIC statement on its own):

```basic
{
    ok: true,
    value: answer,
    inputs: inputs,
    conventions: conventions,
    as_of: as_of,
    schedule: rows,
    warnings: warnings,
    iterations: iterations,
    converged: true,
    method: "bisection",
    version: "finance.irr/1"
}
```

Not every function needs every field, but common meanings should not acquire different names in every module.

### Data lineage

Business results should be able to retain or emit:

- input source and extraction time;
- as-of and effective dates;
- transformation history;
- formula and policy version;
- assumptions and overrides;
- warnings, missing values, and estimation status;
- links from aggregates to underlying transactions.

## API guidelines

1. Prefer records when a calculation has more than a few independently meaningful inputs.
2. Use positional arguments for small, unmistakable mathematical functions.
3. Name periodic versus annual rates explicitly.
4. Use `money` for amounts and numbers for ratios; never strip currency merely for convenience.
5. Return schedules as arrays of consistently shaped records suitable for frames, spreadsheets, databases, charts, and GUIs.
6. Preserve sign conventions consistently and document them beside the API.
7. Provide scalar convenience APIs and explanatory APIs only when both remain unambiguous.
8. Treat failure to converge, multiple solutions, stale data, and unsupported conventions as first-class outcomes.
9. Keep provider acquisition separate from calculations so tests can run offline.
10. Avoid embedding present-day regulatory policy in an apparently timeless function.

## Validation strategy

Financial software requires a stronger standard than matching a single golden output.

### Tests

- byte-exact golden tests for stable presentation and ordinary deterministic behavior;
- table-driven tests for published examples and edge cases;
- independent calculation of invariants, such as a loan schedule reconciling to principal and ending exactly at zero;
- property tests for monotonicity, equivalence, conservation, and inverse relationships;
- randomized differential tests against independent implementations;
- explicit tests for zero rates, negative rates where supported, very long periods, stub periods, leap days, end-of-month dates, currency boundaries, and final-rounding residuals;
- negative tests for invalid currencies, conventions, schedules, rates, and nonconvergence;
- fuzzing of parsers and external financial-message formats.

### Reference comparisons

Depending on the function, compare against at least two of:

- published textbook or standards examples;
- Excel and LibreOffice;
- NumPy Financial and SciPy;
- QuantLib;
- statsmodels and `arch`;
- recognized actuarial packages;
- authoritative institution or regulator worksheets.

Agreement with another package is evidence, not proof. Tests should also establish internal identities and invariants.

### Accuracy contracts

Each numerical API should document:

- valid input domain;
- units and conventions;
- precision or tolerance;
- convergence method and limits;
- behavior when several solutions exist;
- behavior on missing or degenerate data.

## Documentation strategy

Every delivered library should include:

- a status-marked design document;
- API reference entries;
- a task-first cookbook with executed examples;
- a convention and sign-reference page;
- a validation note naming reference sources and tolerances;
- examples that move results into frames, databases, spreadsheets, charts, and reports;
- warnings about plausible-but-wrong failure modes.

The documentation gate should distinguish shipped behavior from proposals and partial implementations, following existing repository practice.

## Phased roadmap

### Phase 0: Foundations and compatibility

- inventory the current `finance` and `stats` finance surface;
- specify naming, sign, timing, rounding, result, and error conventions;
- establish reference datasets and differential-test harnesses;
- decide which existing APIs must remain unchanged through the release-candidate period.

**Exit criterion:** an approved core design and test matrix with no unresolved semantic ambiguity in the next phase.

### Phase 1: Complete core finance

- add `rate`, generalized PV/FV/PMT/NPER, timing, MIRR, XNPV, and XIRR;
- add rate conversions and compounding objects or records;
- add day-count conventions and financial schedule generation;
- strengthen IRR ambiguity and convergence reporting;
- expand depreciation and amortization;
- publish a finance cookbook.

**Exit criterion:** core functions pass independent reference comparisons, edge cases, and algebraic invariants.

### Phase 2: Lending and deposits

- implement product-neutral loan definitions and amortization;
- add daily accrual, payoff, irregular-payment, rate-change, and modification support;
- add underwriting ratios and loan-pricing analytics;
- implement deposit and CD calculations;
- create realistic credit-union and commercial-lending examples.

**Exit criterion:** several end-to-end products reconcile payments, balances, accrued interest, and final settlement under declared policies.

### Phase 3: Accounting and business finance

- implement balanced journal entries, ledgers, periods, and statements;
- connect loan, deposit, sales, and purchasing events to accounting entries;
- add receivables, payables, inventory costing, ratios, valuation, and unit economics;
- demonstrate database and spreadsheet reporting workflows.

**Exit criterion:** a small business can be modeled from transactions through statements and analytical reports.

### Phase 4: Fixed income and portfolios

- deliver bonds, curves, yields, duration, convexity, and spreads;
- deliver lots, positions, returns, attribution, and rebalancing;
- integrate with `market`, `stats`, and EDGAR libraries.

**Exit criterion:** a reproducible portfolio report can be produced from offline fixtures and reconciled independently.

### Phase 5: Risk, treasury, insurance, and payments

- institutional risk and ALM compositions;
- cash position, funding, exposure, and reconciliation;
- insurance and actuarial foundations;
- versioned payment-format adapters and operational controls.

**Exit criterion:** each domain has an end-to-end reference application and domain-reviewed calculations.

### Phase 6: Sales, operations, forecasting, and optimization

- pricing, pipeline, contracts, commissions, and customer economics;
- inventory, capacity, service, supply-chain, and quality analytics;
- unified forecasting evaluation;
- solver adapter and common optimization model.

**Exit criterion:** operational decisions can be modeled, optimized, persisted, and reported within one gBASIC application.

## Initial concrete backlog

The first implementation milestone should remain deliberately smaller than the full vision:

1. Write the core finance API and convention design.
2. Add `finance.rate` with explicit convergence reporting.
3. Generalize PV, FV, PMT, and NPER around one documented equation.
4. Add payment timing without breaking current end-of-period behavior.
5. Add nominal/effective/periodic conversion, APR foundation, and APY.
6. Add Actual/360, Actual/365 Fixed, Actual/Actual, and 30/360 US.
7. Add XNPV and XIRR with dated cash flows.
8. Detect or warn about cash-flow patterns that permit multiple IRRs.
9. Define and test schedule rounding and final-payment reconciliation.
10. Add a task-first finance cookbook executed by the documentation tests.

This milestone would create a stable base for `lending.bas` without prematurely committing to every later API.

## Non-goals

- promising regulatory or legal compliance merely because formulas are supplied;
- embedding tax or regulatory policy without jurisdiction, source, version, and effective dates;
- replacing `stats` with duplicate analytics;
- turning every financial term into a new interpreter-level type before record-based APIs are tested;
- implementing every product variant before the shared conventions are stable;
- optimizing pure-gBASIC code in C without measurement;
- silently cloning Excel behavior where Excel's historical convention is ambiguous or unsafe;
- requiring live market or provider access for tests.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Scope becomes unmanageable | Deliver vertical phases with explicit exit criteria |
| `finance.bas` becomes a monolith | Keep universal mathematics in core and domain behavior in dependent libraries |
| APIs encode ambiguous conventions | Require explicit rate, timing, date, and rounding semantics |
| Plausible numerical errors escape tests | Use invariants, differential testing, and published references |
| Regulation changes after release | Isolate versioned jurisdiction-specific packages |
| Duplicate functions appear across libraries | Maintain a capability map and dependency rules |
| Domain types overcomplicate the language | Prove record/modifier patterns before interpreter changes |
| External data makes tests unreliable | Provider separation and committed offline fixtures |
| Performance is insufficient | Profile realistic applications, then add narrow primitives or solver adapters |
| Users treat analytical output as advice | Document model assumptions and avoid claims of suitability or compliance |

## Success measures

The initiative succeeds when:

- financial formulas express units and conventions clearly at the call site;
- money remains exact and currency-safe throughout business workflows;
- schedules reconcile exactly under their declared rounding policy;
- numerical results match independent references within documented tolerances;
- important results disclose assumptions, warnings, provenance, and convergence;
- libraries compose instead of duplicating statistics, dates, data frames, and market acquisition;
- cookbooks demonstrate realistic bank, credit-union, investment, insurance, sales, and operations work;
- complete applications can ingest data, calculate, decide, account, persist, and report in gBASIC;
- finance and business professionals can review programs without translating dense implementation machinery back into business meaning.

## Decision requested

Approve the platform direction and authorize Phase 0 followed by the initial Phase 1 backlog. Approval does not commit the project to every later module or API. Each workstream should receive its own narrower design review before implementation.

The immediate next document should be a detailed core-finance design covering the generalized TVM equation, rate representation, dated cash flows, day-count conventions, rounding, solver behavior, compatibility with the current `finance.bas`, and the exact public API proposed for Phase 1.
