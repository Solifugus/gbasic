# Statistics library — "usable by working scientists" plan

**Status:** proposal (2026-06-30). Extends the completed `stats.bas` (Phases 1–4,
see `docs/statistics_design.md`) with the shortlist that moves it from a strong
*classical-inference core* to a *primary tool* for behavioral, social,
communications, and natural-science researchers.

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

## Phase 8 — Reliability & agreement  *(psychometrics & communications)*

Depends on variance components from Phase 7's repeated-measures machinery.

| Function | Returns | Verify vs |
|---|---|---|
| `cronbach_alpha(items)` | `{alpha, n_items, n_subjects}` | pingouin `cronbach_alpha` (manual) |
| `cohens_kappa(r1, r2)` | `{kappa, po, pe}` (two-rater agreement) | `sklearn.metrics.cohen_kappa_score` (manual) |
| `icc(ratings)` | `{icc1, icc2, icc3, ...}` intraclass correlation | pingouin `intraclass_corr` (manual) |
| `krippendorff_alpha(data, level)` | `{alpha}` (nominal first; ordinal/interval later) | published worked examples |

**Earn-it:** none — pure gBASIC (Cronbach/κ/ICC are variance-ratio compositions).
Krippendorff is the most involved; nominal level first, others as a follow-up.

## Phase 9 — Proportions & the business lens  *(advertising / marketing / comms)*

| Function | Returns | Verify vs |
|---|---|---|
| `prop_test_1(successes, n, p0)` | `{z, p_value, ci_low, ci_high}` | `statsmodels` `proportions_ztest` |
| `prop_test_2(s1, n1, s2, n2)` | `{z, p_value, diff, ci_low, ci_high}` | `statsmodels` `proportions_ztest` |
| `ab_test(control, variant)` | `{lift, p_value, ci, significant}` (A/B convenience) | manual over `prop_test_2` |
| `rfm(transactions)` | per-customer recency/frequency/monetary + segment | manual |
| `cohort_retention(events)` / `funnel(steps)` | cohort matrix / step conversion + drop-off | manual |

**Earn-it:** none. This is the `stats.business` lens from the original design's §9,
now spec'd. Pure gBASIC over Phase 1 + Phase 5.

## Phase 10 (keystone) — the optimizer  *(unlocks the most)*

A single general optimizer is the highest-leverage foundational piece: it unlocks
**nonlinear curve fitting** (physical science), the **ARIMA/GARCH** deferred in
Phase 4, and **richer GLM families** — all at once.

- `optimize(objective, initial_params, options)` → `{params, value, iterations,
  converged}`. **Recommended method: Nelder–Mead simplex** — derivative-free, so it
  needs only function evaluations (no Jacobian), which means it can be written in
  **pure gBASIC** using first-class functions (the objective is passed as a
  function value — already supported). This likely keeps the earn-it bar uncrossed
  yet again.
- `curve_fit(f, xs, ys, initial)` → `{params, residuals, r_squared, ...}`
  (nonlinear least squares via `optimize`). Verify vs `scipy.optimize.curve_fit`
  on functions with known fits (exponential decay, logistic growth).
- Follow-ons enabled (own phases later): `arima`, `garch`, GLM families beyond
  logistic/Poisson.

**Earn-it:** evaluate after Nelder–Mead lands. Only if a real workload shows it too
slow would a C optimizer be considered.

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
