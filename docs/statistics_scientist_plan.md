# Statistics library — "usable by working scientists" plan

**Status:** COMPLETE (2026-07-01). Extended the completed `stats.bas` (Phases
1–4, see `docs/statistics_design.md`) with this shortlist — Phases 5–10 plus the
power-analysis cross-cut are all shipped — moving it from a strong
*classical-inference core* to a *primary tool* for behavioral, social,
communications, and natural-science researchers. Every phase stayed pure gBASIC:
the §6 C earn-it bar was never crossed. Data-science / predictive ML remains a
separate future track (out of scope). Follow-ons now unblocked by the Phase 10
optimizer: ARIMA/GARCH and richer GLM families.

**Explicitly out of scope:** predictive machine learning (train/test split,
cross-validation, classification metrics, tree ensembles, SVM/kNN). That is a
separate future track, not a stretch of `stats.bas`.

## Principles (carried over from Phases 1–4)

- **Compositions live in gBASIC**; only true primitives are C builtins. Every new
  entry point is expected to be pure gBASIC over the existing base
  (`_gammp`/`_betai` incomplete gamma/beta, `_rank`/`_tie_term`, `_jacobi_eigen`,
  `_glm_result`/`_design`, `matrix.bas`).
- **Verify every numeric result** against scipy / statsmodels (pingouin values
  reproduced manually where it isn't installed). Golden tests round to 6 decimals
  (the cross-architecture determinism rule).
- **Return a record or `unknown`** on malformed / out-of-domain input.
- A C builtin is admitted **only if a feature demonstrably earns it** (§6 of the
  original design). The earn-it candidates here are called out explicitly.

## Data shapes (reuse existing conventions)

- A sample = a flat list of numbers.
- Grouped data (ANOVA) = a list of groups, each a list of numbers.
- A matrix / items×subjects table = list of rows (row-major), as `matrix.bas`
  and `frame.bas` already use.
- A contingency table = list of rows of counts.

---

## Phase 5 — Correlation family & effect sizes  *(cheapest, broadest)* — **DONE (2026-06-30)**

Thin compositions; no new dependencies. Highest leverage per line — touches every
audience. Shipped in `stdlib/stats.bas`, verified against scipy in
`examples/stats_correlation_test.bas`. Helpers added: `_tie_pairs` (tau-b
denominator), `_corr_p` (t-approx two-sided p). Kendall uses the asymptotic normal
(no-continuity) p, matching `scipy` on tie-free data.

| Function | Returns | Verify vs |
|---|---|---|
| `spearman(x, y)` | `{rho, p_value, n}` (Pearson on `_rank`s; t-approx p) | `scipy.stats.spearmanr` |
| `kendall_tau(x, y)` | `{tau, p_value, n}` (tau-b, tie-corrected) | `scipy.stats.kendalltau` |
| `partial_correlation(x, y, z)` | `{r, p_value, n}` (control for z) | pingouin `partial_corr` (manual) |
| `point_biserial(binary, x)` | `{r, p_value}` | `scipy.stats.pointbiserialr` |
| `cramers_v(table)` | `{v, chi2, dof}` from a contingency table | manual over `chi_square_independence` |
| `hedges_g(a, b)` | `{g}` (bias-corrected Cohen's d) | manual |
| `odds_ratio(table2x2)` | `{odds_ratio, ci_low, ci_high, log_or_se}` | `scipy.stats.contingency.odds_ratio` |
| `eta_squared(groups)` | `{eta_squared, omega_squared}` (ANOVA effect size) | manual over `anova_oneway` |

**Earn-it:** none — all pure gBASIC.

## Phase 6 — Distribution expansion  *(broad natural-science value)* — **DONE (2026-07-01)**

Each is `*_pdf` + `*_cdf` + `*_quantile`, built on the incomplete gamma/beta
engines already present. Matches the existing distribution style. Shipped in
`stdlib/stats.bas`, verified against scipy in
`examples/stats_distribution_test.bas`. Parameterizations match scipy (see the
header comment in the source). Continuous quantiles are closed-form where one
exists (uniform/expon/lognormal/weibull) and bisection over the CDF otherwise
(gamma/beta); the discrete negbinom quantile searches upward like `binom_quantile`.

| Family | Built on | Verify vs |
|---|---|---|
| `uniform_*(a, b)` | closed form | `scipy.stats.uniform` |
| `expon_*(rate)` | closed form | `scipy.stats.expon` |
| `gamma_*(shape, rate)` | `_gammp` | `scipy.stats.gamma` |
| `beta_*(a, b)` | `_betai` | `scipy.stats.beta` |
| `lognormal_*(mu, sigma)` | `_norm_cdf_std` | `scipy.stats.lognorm` |
| `weibull_*(shape, scale)` | closed form | `scipy.stats.weibull_min` |
| `negbinom_*(r, p)` | `_betai` | `scipy.stats.nbinom` |

**Earn-it:** none — reuses existing special-function engines.

## Phase 7 — Experimental-design tests  *(psychology & experimental science)* — **DONE (2026-07-01, incl. power analysis)**

Shipped in `stdlib/stats.bas`, verified against statsmodels/scipy in
`examples/stats_expdesign_test.bas`. Signatures as implemented:

| Function | Returns | Verify vs |
|---|---|---|
| `anova_twoway(cells)` | balanced A×B design; `{a, b, interaction, residual}`, each factor line `{ss, df, ms, statistic, p_value}` (Type I = II = III when balanced) | `statsmodels` ols + `anova_lm(typ=2)` |
| `anova_repeated(data)` | subjects×conditions matrix → `{statistic, df1, df2, p_value, partial_eta2}` | manual (matches pingouin `rm_anova`) |
| `friedman(data)` | subjects×conditions → `{statistic, df, p_value}`, tie-corrected | `scipy.stats.friedmanchisquare` |
| `tukey_hsd(groups)` | list of `{group1, group2, mean_diff, ci_low, ci_high, p_adj, reject}` (alpha 0.05, Tukey-Kramer SE) | `statsmodels` `pairwise_tukeyhsd` |

**Note on shapes:** `anova_twoway` takes a nested `cells[i][j]` = replicate list
(levels are derived, so the proposed `a_levels`/`b_levels` args were dropped as
redundant).

**Earn-it — resolved in pure gBASIC.** The studentized-range distribution
(`_ptukey`/`_qtukey`) that Tukey HSD needs was implemented by **nested composite
Simpson quadrature** of the classic range-CDF integral (inner: 160 z-panels over
[-7.5, 7.5]; outer: 80 s-panels), no C helper. Agreement with
`scipy.stats.studentized_range` is ~1e-6 absolute (the Simpson floor) — far beyond
any practical p-value reporting need; `reject` decisions and CIs are exact to
display precision. This is the slowest test in the suite (~3 s) because Tukey runs
one `_qtukey` plus one `_ptukey` per pair.

**Power analysis — DONE (2026-07-01).** See Cross-cutting below.

## Phase 8 — Reliability & agreement  *(psychometrics & communications)* — **DONE (2026-07-01)**

Shipped in `stdlib/stats.bas`, verified in `examples/stats_reliability_test.bas`.
All pure gBASIC (variance-ratio and coincidence-matrix compositions). Signatures
as implemented:

| Function | Returns | Verify vs |
|---|---|---|
| `cronbach_alpha(data)` | `{alpha, n_items, n_subjects}` (rows = subjects, cols = items; ddof=1) | direct formula |
| `cohens_kappa(r1, r2)` | `{kappa, po, pe}` (two-rater agreement) | `statsmodels` `cohens_kappa` |
| `icc(data)` | `{icc1, icc2, icc3, icc1k, icc2k, icc3k}` (rows = subjects, cols = raters) | published Shrout & Fleiss (1979) |
| `krippendorff_alpha(data, level)` | `{alpha}`; `level` = "nominal" / "interval" / "ordinal" | standard worked example (nominal 0.743, interval 0.849) |

**Note on shapes:** `krippendorff_alpha` takes rows = observers, cols = units,
with `unknown` marking a missing rating. All three metric levels shipped (not
just nominal). ICC returns all six Shrout & Fleiss coefficients.

**Earn-it:** none — pure gBASIC. The one non-obvious dependency was value
enumeration; done with the `sort`/`contains` builtins plus a linear `_index_in`.

## Phase 9 — Proportions & the business lens  *(advertising / marketing / comms)* — **DONE (2026-07-01)**

Shipped in `stdlib/stats.bas`, verified in `examples/stats_proportions_test.bas`.
The proportion z-tests match statsmodels exactly; the conveniences are
deterministic compositions. Signatures as implemented:

| Function | Returns | Verify vs |
|---|---|---|
| `prop_test_1(x, n, p0)` | `{z, p_value, phat, ci_low, ci_high}` (null-SE statistic, Wald CI) | `statsmodels` `proportions_ztest` (exact) |
| `prop_test_2(s1, n1, s2, n2)` | `{z, p_value, diff, ci_low, ci_high}` (pooled statistic, unpooled Wald CI) | `statsmodels` `proportions_ztest` (exact) |
| `ab_test(control, variant)` | `{lift, diff, p_value, ci_low, ci_high, significant}`; args are records `{successes, n}` | over `prop_test_2` |
| `rfm(transactions)` | per-customer `{customer, recency, frequency, monetary, r_score, f_score, m_score, rfm}`; tx = `{customer, day, amount}`, tertile 1..3 scoring (recency inverted) | manual |
| `funnel(steps)` | per-stage `{stage, count, conversion, cumulative, dropoff}` | manual |
| `cohort_retention(events)` | per-cohort `{cohort, size, retention}`; events = `{customer, cohort, period}`, distinct-customer counts | manual |

**Earn-it:** none — pure gBASIC over the standard normal + base builtins. Note
`step` is a reserved token, so the funnel field is `stage`. `sort` mutates its
argument in place, so `rfm` sorts *copies* to keep per-customer alignment.

## Phase 10 (keystone) — the optimizer  *(unlocks the most)* — **DONE (2026-07-01)**

Shipped in `stdlib/stats.bas`, verified against `scipy.optimize` in
`examples/stats_optimize_test.bas`. Signatures as implemented:

- `optimize(objective, initial, opts, ctx)` → `{params, value, iterations,
  converged}`. **Nelder–Mead simplex**, derivative-free, pure gBASIC. The
  objective is a function value called as `objective(params, ctx)`; `ctx` is
  arbitrary user data forwarded to every call (this replaces closures, which
  gBASIC function values do not provide). `opts` is a record with optional
  `max_iter` / `tol`, or `unknown` for defaults (`max_iter = 400·n`,
  `tol = 1e-12`). Verified: Rosenbrock from (−1.2, 1) converges to (1, 1).
- `curve_fit(f, xs, ys, initial)` → `{params, residuals, sse, r_squared,
  iterations, converged}`. Nonlinear least squares: builds an SSE objective
  (`_curve_sse`, ctx = `{f, xs, ys}`) and minimizes via `optimize`. Model `f`
  is a function value `f(x, params)`. Verified vs `scipy.optimize.curve_fit`:
  exponential decay recovers (2.5, 0.4, 0.5) and logistic growth (10, 1, 5),
  both r² = 1 on noiseless data. Runs in ~0.1 s.

**Earn-it — resolved in pure gBASIC.** Nelder–Mead is fast enough at these
sizes; the C bar stayed uncrossed. This unblocks the ARIMA/GARCH deferred in
Phase 4 and richer GLM families (their own future phases).

## Follow-on (optimizer-enabled) — ARIMA-family time series — **DONE (2026-07-01)**

First follow-on the Phase 10 optimizer unblocks. Shipped in `stdlib/stats.bas`,
verified in `examples/stats_arima_test.bas` (AR/CSS) and
`examples/stats_arima_mle_test.bas` (exact MLE).

| Function | Returns | Verify vs |
|---|---|---|
| `ar_fit(xs, p)` | `{const, phi, sigma2, aic, bic, llf, n}` — OLS conditional MLE | `statsmodels` `AutoReg(trend='c')` (exact: coef, AIC/BIC/llf) |
| `ar_forecast(model, xs, h)` | recursive h-step forecast list | `AutoReg.forecast` (exact) |
| `arma_fit(xs, p, q)` | `{const (=mean), phi, theta, sigma2, llf, aic, bic}` — **exact Gaussian MLE via Kalman filter** | `statsmodels` `ARIMA(order=(p,0,q), trend='c')` (exact) |
| `arma_css_fit(xs, p, q)` | `{const, phi, theta, sse, …}` — fast CSS via `optimize` | best-effort (approximate) |
| `arima_fit(xs, p, d, q)` | `{p, d, q, const, phi, theta, sigma2, aic, bic}` — differences then AR-OLS (q=0) or exact ARMA MLE | exact vs statsmodels |
| `arma_forecast(model, xs, h)` | stationary ARMA forecast via the Kalman terminal state | `statsmodels` `ARIMA.forecast` (exact) |
| `arima_forecast(model, xs, h)` | integrated forecast, **any q**, d∈{0,1} | `statsmodels` `ARIMA.forecast` (exact) |

**Status.** `arma_fit` is exact maximum likelihood: a Kalman filter over the
Harvey ARMA state space (stationary initialization by solving the discrete
Lyapunov equation `vec(P) = (I − T⊗T)⁻¹ vec(RR')` via `matrix.bas`), with `sigma2`
concentrated out and the mean/phi/theta optimized by Nelder–Mead. Matches
`statsmodels` ARIMA to display precision on σ²/log-likelihood/AIC/BIC and to ~4
decimals on the parameters (the surface is flat near the optimum). `arma_css_fit`
is retained as a fast approximate alternative. MA-model forecasting is supported
via `arma_forecast`/`arima_forecast` (the Kalman filter's terminal predicted
state is propagated by the transition matrix), matching `statsmodels
ARIMA.forecast` exactly. **Earn-it:** none — pure gBASIC over `matrix.bas` and
the optimizer.

## Follow-on (optimizer-enabled) — GARCH(1,1) volatility — **DONE (2026-07-01)**

Shipped in `stdlib/stats.bas`, verified in `examples/stats_garch_test.bas`.

| Function | Returns | Verify vs |
|---|---|---|
| `garch_fit(r)` | `{mu, omega, alpha, beta, persistence, llf, aic, bic}` — constant-mean GARCH(1,1) Gaussian MLE via `optimize` | the **`arch`** package (Sheppard) — ~3 decimals |

Replicates `arch`'s exponentially-weighted variance backcast (0.94-decay over the
first min(75, n) squared residuals) so the likelihood matches; parameters and llf
agree to ~3 decimals (a ~1e-3 backcast gap remains, negligible for inference).
Admissibility (ω>0, α,β≥0, α+β<1) is enforced by likelihood penalty. **Validation
note:** the `arch` package was pip-installed into a venv to serve as the
gold-standard reference, and a from-scratch Python likelihood was matched to it
before porting. **Earn-it:** none — pure gBASIC.

## Follow-on — GLM family expansion  *(social / communication science)* — **DONE (2026-07-01)**

The existing GLM core (OLS with full inference, logistic, Poisson) already
carried SE/z/p tables; this adds the families that survey-heavy fields
(communication, health, PR, new media) lean on. Shipped in `stdlib/stats.bas`,
verified against statsmodels in `examples/stats_glm_test.bas`. `_glm_result` now
also reports `aic`/`bic`/`df_resid`.

| Function | Returns | Verify vs |
|---|---|---|
| `probit_regression(y, xs)` | GLM result (SE/z/p, aic/bic) — IRLS, normal-CDF link | `statsmodels` GLM(Binomial, probit) (exact) |
| `negbinom_regression(y, xs)` | `{coefficients, std_errors, z_values, p_values, alpha, llf, aic, bic, …}` — NB2, dispersion by joint MLE | `statsmodels` NegativeBinomial (exact) |
| `ordinal_regression(y, xs)` | proportional-odds logit `{coefficients (slopes), std_errors, …, cutpoints, …}` | `statsmodels` OrderedModel(logit) (exact) |
| `multinomial_regression(y, xs)` | baseline-category logit; coefficients/errors as K-1 blocks | `statsmodels` MNLogit (exact) |

The MLE-based families (NB, ordinal, multinomial) are fit with `optimize` and
share a numerical-Hessian covariance helper `_mle_cov` (central-difference
observed information → inverse), which reproduces statsmodels standard errors to
~4 decimals. Ordinal cutpoints are kept increasing via an exp-increment
parameterization; the multinomial baseline is category 0. **Earn-it:** none —
pure gBASIC over `matrix.bas` and the optimizer.

**Reporting helpers (2026-07-01).** For write-ups: `conf_int(model, level)` (Wald
CIs — t for OLS, z otherwise), `odds_ratios(model, level)` (exp-coef + CI: odds
ratios for logistic, incidence-rate ratios for Poisson/NB), and McFadden
`pseudo_r2` (+ `null_log_likelihood`) now on every logistic/probit/Poisson/
ordinal/multinomial result. Null log-likelihoods are the closed-form intercept-
only models (Bernoulli / Poisson / marginal-frequency), matching statsmodels
`prsquared` exactly; CI/OR verified against statsmodels `conf_int`.

## Cross-cutting — power analysis  *(slotted with Phase 7)* — **DONE (2026-07-01)**

Shipped in `stdlib/stats.bas`, verified against `scipy.stats.nct`/`ncf` and
`statsmodels.stats.power` to ~1e-10 in `examples/stats_power_test.bas`.

| Function | Returns | Verify vs |
|---|---|---|
| `nct_cdf(x, df, ncp)` | noncentral t CDF (Lenth 1989 AS 243 series) | `scipy.stats.nct.cdf` |
| `ncf_cdf(x, df1, df2, ncp)` | noncentral F CDF (Poisson mixture of `_betai`) | `scipy.stats.ncf.cdf` |
| `power_ttest(d, n, alpha, sided)` | two-sample (indep, equal per-group `n`) t-test power | `statsmodels` `TTestIndPower` |
| `power_ttest_paired(d, n, alpha, sided)` | one-sample / paired t-test power | `statsmodels` `TTestPower` |
| `power_anova(k, n, f, alpha)` | one-way ANOVA power (Cohen's `f`, per-group `n`) | `statsmodels` `FTestAnovaPower` |
| `sample_size_ttest(d, power, alpha, sided)` | smallest per-group `n` for two-sample power (upward search) | `statsmodels` `solve_power` |

**Earn-it — resolved in pure gBASIC.** Both noncentral distributions were built
over the existing `_betai` incomplete-beta engine and `_norm_cdf_std`, so the §6
C bar stayed uncrossed. The **exact** series (not the recommended approximation)
was cheap enough to ship directly. `sided` is 1 or 2 (one/two-tailed).

---

## Suggested sequencing & rationale

1. **Phase 5** first — cheapest, broadest, immediately makes the library feel
   "complete" to a scientist skimming the function list (correlations + effect
   sizes are conspicuous by their absence today).
2. **Phase 6** next — independent, broad, low risk; good natural-science signal.
3. **Phase 7 + power** — the experimental-design bundle; the Tukey `ptukey` work is
   the first real research task of this plan.
4. **Phase 8** — rides Phase 7's variance-component machinery.
5. **Phase 9** — self-contained; do any time after Phase 5.
6. **Phase 10 (optimizer)** — schedule when nonlinear curve fitting or ARIMA is
   wanted; it is foundational but not blocking the above.

## Open decisions (worth settling before coding the affected phase)

- **Tukey/`ptukey`:** numerical integration in gBASIC now, or CIs-only interim?
  (Recommend integration; fall back to CIs.)
- **ANOVA sum-of-squares type** for `anova_twoway`: Type II first (balanced), or go
  straight to Type III? (Recommend Type II, balanced designs, then extend.)
- **Power analysis noncentral distributions:** exact series now, or approximation
  first? (Recommend approximation first, exact later.)
- **Optimizer method:** confirm Nelder–Mead (derivative-free, pure gBASIC) as the
  default. (Recommend yes.)
