# Statistics cookbook — Econometrics & Finance

**For:** economics, applied econometrics, quantitative finance, and business
analytics — fields whose work is built on **time series, returns, and
observational regression**, and whose central worries are *non-stationarity*,
*serial correlation*, and *heteroskedasticity* rather than experimental control.

This is a **task-first** guide: find the question you're asking, copy the recipe,
read how to report it. Every snippet here is executed by
`examples/cookbook_econ_test.bas` (run by the test suite), so the code is
known-good, and every statistic is verified to match SciPy / statsmodels exactly.

## Setup

```basic
program analysis(args)
    load stats from "stdlib/stats.bas"    ' path relative to your program
    ' ... recipes below ...
end program
```

Run with the stdlib on the path: `GBASIC_PATH=stdlib ./gbasic analysis.bas`
(after `make install`, `load stats from "stats.bas"` resolves on its own).

**Data conventions**
- A **series** (prices, returns, a variable) is a flat list, in time order:
  `[100.0, 102.0, 101.0, ...]`.
- **Predictors** for a regression are a list of columns: `[x1, x2]` (each a
  list). A single predictor is `[x]`.
- Any function returns a **record** of results, or `unknown` on bad/degenerate
  input — guard with `is_unknown(...)`.

---

## 1. Returns from prices

```basic
r  = simple_returns(prices)     ' r_t = P_t/P_{t-1} - 1   (length n-1)
lr = log_returns(prices)        ' r_t = ln(P_t/P_{t-1})
tot = cumulative_return(r)      ' compounded: prod(1+r) - 1
```
Use **log returns** for statistical modeling (they add across time); **simple
returns** for reporting performance.

## 2. Performance & risk metrics

```basic
sharpe_ratio(r, rf, periods)    ' annualized; periods = 252 daily, 12 monthly
sortino_ratio(r, rf, periods)   ' downside-only volatility in the denominator
md = max_drawdown(prices)       ' {max_drawdown, peak, trough}; drawdown is negative
value_at_risk(r, 0.05, "historical")   ' 95% VaR, as a positive loss; or "normal"
cvar(r, 0.05)                          ' expected shortfall (average tail loss)
```
**Report:** annualized Sharpe (`rf` is the per-period risk-free rate), max
drawdown as a percentage, and VaR/CVaR as the loss you'd expect to breach `alpha`
of the time. Sharpe uses total volatility; Sortino penalizes only downside moves.

## 3. Systematic risk (CAPM / market model)

**Question:** how much of an asset's excess return is explained by the market?

```basic
cp = capm(asset_returns, market_returns, rf)
cp.beta        ' systematic risk (market sensitivity)
cp.alpha       ' Jensen's alpha (skill / mispricing); cp.alpha_p to test it vs 0
cp.r_squared   ' fraction of variance explained by the market
```
**Report:** β = `cp.beta` (SE `cp.beta_se`), α = `cp.alpha` with its *p*-value —
a significant positive α is the headline claim of "outperformance." Extend to
multi-factor (Fama-French) models with `ols(asset_excess, [mkt, smb, hml])`.

## 4. Regression with robust inference

Ordinary standard errors assume homoskedastic, uncorrelated errors — rarely true
for economic data. Match the standard error to the violation:

```basic
fit = ols(y, [x1, x2])                 ' point estimates (always valid under OLS)
rob = ols_robust(y, [x1, x2], "HC3")   ' heteroskedasticity-robust SEs
nw  = newey_west(y, [x1, x2], 4)       ' HAC: robust to heteroskedasticity + autocorrelation
```
- Cross-section, unequal variance → **`ols_robust`** (HC0/HC1/HC2/HC3; HC3 is the
  safe default in small samples).
- Time series with serially correlated errors → **`newey_west`** with `maxlags`
  (a common rule is ⌊4·(n/100)^(2/9)⌋).

The **coefficients are identical** across all three; only the SEs / *p*-values
change. **Report** the coefficient with the robust SE and say which estimator you
used and why.

## 5. Diagnosing the regression

```basic
breusch_pagan(fit.residuals, [x1, x2])   ' heteroskedasticity: small p -> non-constant variance
ljung_box(fit.residuals, 4)              ' leftover autocorrelation: small p -> correlated errors
durbin_watson(fit.residuals)             ' ~2 = no AR(1); <2 positive, >2 negative
```
**Report:** if Breusch-Pagan rejects, use `ols_robust`/`newey_west`; if Ljung-Box
or Durbin-Watson flags autocorrelation, use `newey_west` (or model the dynamics —
§7). Each returns `{statistic, df, p_value}` (Durbin-Watson returns a bare
statistic).

## 6. Is the series stationary? (unit-root testing)

**Do this first** for any time-series model — regressing one non-stationary
series on another produces *spurious* results.

```basic
a = adf_test(x, lags, "c")     ' Augmented Dickey-Fuller; trend "n"/"c"/"ct"
a.statistic    a.p_value    a.crit_5     ' reject unit root (stationary) if p small / stat < crit
```
If it fails to reject (large *p*), **difference and retest**:
```basic
dx = diff(x, 1)                ' first difference
a2 = adf_test(dx, lags, "c")   ' usually stationary now -> the series is I(1)
```
**Report:** the ADF statistic, its *p*-value, and the differencing order *d* you
settled on. `trend="c"` for a series around a constant mean, `"ct"` if it drifts
with a linear trend, `"n"` for zero-mean.

## 7. Modeling a time series (ARIMA family)

**Identify the order** from the autocorrelation functions, then fit:

```basic
acf(x, 20)      ' MA signature: cuts off after lag q
pacf(x, 20)     ' AR signature: cuts off after lag p

m  = arima_fit(x, p, d, q)     ' d = differencing from §6
m.phi   m.theta   m.aic   m.bic
fc = arima_forecast(m, x, h)   ' h-step-ahead forecast
```
Special cases: `ar_fit(x, p)` (pure AR by OLS), `arma_fit(x, p, q)` (exact
Kalman-filter MLE, matches statsmodels). **Report:** the chosen (p, d, q), the
coefficients, AIC/BIC for model comparison, and — crucially — confirm the
residuals are white noise with `ljung_box` (§5). Compare candidate orders by
**lowest AIC/BIC**.

## 8. Volatility clustering (ARCH / GARCH)

Financial returns have calm and turbulent periods — variance that clusters in
time. Test for it, then model it:

```basic
arch_lm(returns, 2)            ' Engle's test; small p -> ARCH effects present
g = garch_fit(returns)         ' GARCH(1,1)
g.omega   g.alpha   g.beta   g.persistence     ' persistence = alpha+beta (near 1 = long memory)
```
**Report:** the ARCH-LM test result (justifies modeling volatility at all), then
the GARCH parameters. `alpha` is the reaction to shocks, `beta` the persistence
of past variance; `alpha + beta` close to 1 means volatility shocks decay slowly.
(GARCH is verified against the `arch` package.)

## 9. Where the prices come from

Everything above starts from a price series, and `market` is what produces one:

```basic
load market
m = market.tiingo(env("TIINGO_KEY"))      ' or market.stooq() -- no key needed
start{date}  = "2024-01-01"
finish{date} = "2024-12-31"
r = market.daily(m, "MSFT", start, finish)
if r.ok then
    prices = r.frame["close"]              ' the flat list every recipe above wants
end if
```
`daily` returns `{ok, frame, adjusted, message}` — a **failure is a value, not a
raise**, because an unknown symbol and a dead network are ordinary outcomes here.

**Two traps, both of which produce ordinary-looking numbers rather than errors:**
- **Order.** Providers disagree about oldest-first versus newest-first. On a
  reversed series `simple_returns` returns the **negated** sequence — every sign
  wrong, nothing else out of place. Rows are always sorted ascending by date.
- **Adjustment.** A 2-for-1 split halves the raw close, so returns computed from
  unadjusted prices read as a −50% day. `r.adjusted` reports what the provider
  actually supplies; it is never assumed. Where an adjusted series exists it is
  used for **every** price column, not just the close — mixing an adjusted close
  with raw highs and lows puts the columns on different scales.

For tests, `market.offline(m, dir)` replays committed fixtures so nothing
touches the network.

## 10. Event studies: did the announcement move the stock?

Fit a normal-return model *before* an event, then measure the residual across
it. This is what turns a filing date into a testable claim:

```basic
w  = event_window(dates, event_date, 5, 5)       ' 5 TRADING days either side
ev = abnormal_returns(asset_r, market_r, { event: 150, pre: 1, post: 1, estimation: 120 })
ev.alpha   ev.beta   ev.ar   ev.car              ' the model, and the abnormal return
agg = event_study([ev1, ev2, ev3])               ' pool events -> CAAR + t-test
agg.caar   agg.t   agg.p   agg.n   agg.contaminated
```
Windows count **trading days** — they index the dates the series actually has,
so a weekend cannot shorten one. An event on a closed day moves to the next
trading day and `shifted` says so. Three things are **refused** rather than
answered: an estimation window overlapping its own event window, a CAAR over
windows of unequal width, and an unknown model name. Clustered events whose
baselines contain each other are **reported** (`contaminated`, `note`) rather
than refused, because clustering is sometimes unavoidable.

**Report:** the estimation window and model, CAR (or CAAR) with its *t* and *p*,
the number of events, and any contamination.

## 11. Causal inference: did X cause Y, or merely move with it?

Two designs, and one warning that applies to both: **the coefficient can be
right while the standard error is wrong**, and the wrong one is the flattering
one. Nothing about the output looks off.

**Difference-in-differences** — compare a treated group's before/after against
an untreated group's:

```basic
d = did(y, treated, post, { cluster: unit_id })
d.att   d.std_error   d.p_value   d.conf_low   d.conf_high
d.means      ' the four cells: control_pre/post, treated_pre/post
```
With no covariates the coefficient **is** the four-cell arithmetic
`(treated_post − treated_pre) − (control_post − control_pre)`, and `means`,
`diff_in_means` and `saturated` let you check it by hand.

**Cluster your standard errors.** On serially correlated panel data the
conventional error is badly understated (Bertrand, Duflo & Mullainathan 2004) —
in the library's own test panel it is 3.2× too small, reporting *p* < 0.001
where clustering reports *p* > 0.10 on an identical estimate. Pass `cluster:`
with one id per row.

**Parallel trends is an assumption and `did` does not test it** — it cannot,
since it is a claim about what the treated group *would* have done. What you
can test is whether the groups moved together *before* treatment:

```basic
pt = pre_trends(y, treated, period, treat_start)
pt.f_stat   pt.p_value   pt.leads   pt.periods
```
A large *p* here is the **absence of evidence against** parallel trends over
however many pre-periods you happen to have — not evidence for it. The returned
`note` says so.

**Instrumental variables** — use a variable that moves X but not Y directly:

```basic
iv = iv_2sls(y, endog, instruments, { exog: controls })
iv.estimate   iv.std_error   iv.p_value
iv.first_stage[0].f_stat      ' below ~10 the instrument is weak
iv.weak   iv.sargan   iv.wu_hausman
```
**Do not run this as two `ols` calls.** Fitting *x* on *z* and then *y* on *x̂*
gives the identical point estimate and measures residuals against *x̂*; the
model's residuals are against the **original** *x*. On two datasets differing
only in the *sign* of the confounding, the naive standard error comes out 1.78×
too large and 2.7× too small — it is not conservative, and which way it errs
depends on something you cannot observe.

Read the diagnostics: `first_stage` (a weak instrument biases 2SLS toward OLS
and its interval under-covers), `sargan` (present only when there are more
instruments than endogenous regressors — with exact identification there is
nothing to test), and `wu_hausman` (whether the regressor was endogenous at all;
if not, plain OLS was fine).

**Report:** the design, the estimate with its *clustered* or 2SLS standard
error, the first-stage F, and — for DiD — the pre-trend test with its honest
caveat.

## 12. A typical workflow

1. **Prices → returns** (`log_returns`), describe with §2 metrics.
2. **Stationarity** (`adf_test`); difference until stationary (§6).
3. **Identify** order from `acf`/`pacf` (§7).
4. **Fit** `arima_fit`; compare orders by AIC/BIC.
5. **Check residuals** are white noise (`ljung_box`, `durbin_watson`, §5) and
   test for ARCH (`arch_lm`, §8).
6. If volatility clusters, **add `garch_fit`**; if you only need robust
   regression inference, use `newey_west` (§4).

For a **causal** question rather than a descriptive one, the sequence is
different: establish the design first (§11), then worry about the standard
error, then report the diagnostics that say whether the design held.

## 13. Reporting quick reference

| You have | Recipe | Headline numbers |
|---|---|---|
| A price series | `simple_returns` / `log_returns` | returns, cumulative return |
| Return series | `sharpe_ratio` / `sortino_ratio` | annualized ratio |
| Downside risk | `value_at_risk` / `cvar` | VaR, expected shortfall |
| Drawdown | `max_drawdown` | worst peak-to-trough % |
| Asset vs market | `capm` | β, α (*p*), R² |
| Regression, unequal variance | `ols_robust` | *b*, robust SE, *p* |
| Regression, serial correlation | `newey_west` | *b*, HAC SE, *p* |
| Heteroskedasticity check | `breusch_pagan` | LM, *p* |
| Autocorrelation check | `ljung_box` / `durbin_watson` | *Q*/*p*, DW |
| Unit root / stationarity | `adf_test` | ADF stat, *p*, crit |
| Series dynamics | `arima_fit` + `arima_forecast` | (p,d,q), AIC, forecast |
| Volatility clustering | `arch_lm` + `garch_fit` | ARCH *p*, α+β |
| No price data yet | `market.daily` | frame, `adjusted` flag |
| Event vs. announcement | `abnormal_returns` + `event_study` | CAR/CAAR, *t*, *p* |
| Policy on/off, two groups | `did` (+ `cluster:`) | ATT, clustered SE, *p* |
| Parallel-trends check | `pre_trends` | joint *F*, *p*, leads |
| Endogenous regressor | `iv_2sls` | estimate, first-stage *F*, Sargan |

---

*Results in §1–§8 are verified numerically against SciPy / statsmodels (GARCH
against the `arch` package). §10–§11 are verified differently and deliberately
so: their suites (`tests/run_event_study.sh`, `tests/run_causal.sh`) derive
nearly every number a second, independent way rather than recording it, because
both methods can be right in the estimate and wrong in the uncertainty — a
golden would enshrine the wrong standard error. See
`docs/statistics_scientist_plan.md` for method notes and
`examples/cookbook_econ_test.bas` for the runnable version of every recipe above.
Predictive machine learning (train/test, cross-validation, classifiers) is a
separate track, intentionally out of scope here.*
