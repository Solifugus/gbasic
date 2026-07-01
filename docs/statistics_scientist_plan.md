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

## Phase 6 — Distribution expansion  *(broad natural-science value)*

Each is `*_pdf` + `*_cdf` + `*_quantile`, built on the incomplete gamma/beta
engines already present. Matches the existing distribution style.

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

## Phase 7 — Experimental-design tests  *(psychology & experimental science)*

| Function | Returns | Verify vs |
|---|---|---|
| `anova_twoway(data, a_levels, b_levels)` | per-factor + interaction `{ss, df, ms, f, p}`, balanced designs first (Type II) | `statsmodels` ols + `anova_lm` |
| `anova_repeated(subjects_x_conditions)` | `{f, df1, df2, p, partial_eta2}` | pingouin `rm_anova` (manual) |
| `friedman(data)` | `{statistic, df, p_value}` (nonparametric RM) | `scipy.stats.friedmanchisquare` |
| `tukey_hsd(groups)` | list of `{pair, mean_diff, ci_low, ci_high, p_adj, reject}` | `statsmodels` `pairwise_tukeyhsd` |

**Earn-it — the one hard spot:** Tukey HSD needs the **studentized-range
distribution** (`ptukey`/`qtukey`), which has no closed form. Options, in order of
preference: (a) numerical integration of the range CDF in pure gBASIC (feasible,
moderate work); (b) ship mean-differences + confidence intervals now and add exact
`p_adj` when (a) lands; (c) a C helper only if (a) proves too slow/inaccurate.
**Recommend (a), fall back to (b).** Everything else in this phase is pure gBASIC.

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

## Cross-cutting — power analysis  *(small; slot with Phase 7)*

`power_ttest`, `power_anova`, and `sample_size_ttest` — required by reviewers in
psychology and the sciences. These need **noncentral t / F** distributions (new
special functions, moderate difficulty via series). Verify vs
`statsmodels.stats.power`. Can ship approximate first, exact later.

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
