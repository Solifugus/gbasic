# Statistics cookbook — Social & Behavioral Sciences

**For:** communication science, psychology, sociology, political science,
education, public health (behavioral), and marketing / PR — fields whose work is
built on **surveys, Likert scales, coded content, and designed experiments**,
and whose goal is usually *inference about effects* rather than prediction.

This is a **task-first** guide: find the question you're asking, copy the recipe,
read how to report it. Every snippet here is executed by
`examples/cookbook_social_test.bas` (run by the test suite), so the code is
known-good.

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
- A **variable / sample** is a flat list: `[4.1, 3.8, 4.4, ...]`.
- **Groups** (for ANOVA) are a list of lists: `[[...], [...], [...]]`.
- **Predictors** for a regression are a list of columns: `[x1, x2]` (each a
  list). A single predictor can be passed as `[x]`.
- A **categorical predictor** (factor) is a list of labels (strings or numbers);
  turn it into columns with `dummy_code` (below).
- Any function returns a **record** of results, or `unknown` on bad/degenerate
  input — guard with `is_unknown(...)`.

---

## 1. Describe a variable

```basic
mean(x)    median(x)    stdev(x)    variance(x)
quantile(sort(x), 0.25)    iqr(x)    skewness(x)    kurtosis(x)
```

## 2. Compare two conditions

**Question:** does the outcome differ between two groups?

```basic
tt = stats.t_test_2sample(control, treatment)   ' Welch: t_test_welch; paired: t_test_paired
d  = stats.cohens_d(control, treatment)         ' effect size
' non-normal? use the rank test:
mw = stats.mann_whitney(control, treatment)
```
**Report:** *t*(df) = `tt.statistic`, *p* = `tt.p_value`, Cohen's *d* = `d`. Give
the group means and a CI (`confidence_interval`). Cohen's *d* ≈ 0.2 / 0.5 / 0.8 =
small / medium / large.

## 3. Compare three or more groups (one-way)

```basic
av = stats.anova_oneway(groups)          ' groups = [[...],[...],[...]]
es = stats.eta_squared(groups)           ' eta^2 (and omega^2)
hsd = stats.tukey_hsd(groups)            ' which pairs differ (Tukey HSD)
```
Non-parametric alternative: `kruskal_wallis(groups)`.
**Report:** *F*(df1, df2) = `av.statistic`, *p* = `av.p_value`, η² =
`es.eta_squared`; then the significant Tukey pairs with their CIs.

## 4. Factorial & repeated-measures designs

```basic
aw = stats.anova_twoway(cells)        ' cells[i][j] = replicate list; {a, b, interaction, residual}
rm = stats.anova_repeated(data)       ' data[subject][condition]; within-subjects
fr = stats.friedman(data)             ' non-parametric repeated measures
```
**Report** each effect: *F*, *p*, and for two-way the interaction line — the term
communication reviewers look at first.

## 5. Association between two variables

```basic
pearson: stats.ols(y, [x]) or correlation(x, y)
stats.spearman(x, y)          ' rank correlation {rho, p_value, n}
stats.kendall_tau(x, y)       ' tau-b, tie-corrected
stats.partial_correlation(x, y, z)     ' control for z
stats.point_biserial(binary, x)        ' one dichotomous variable
stats.cramers_v(table)                 ' two categorical (from a contingency table)
```

## 6. Predict a continuous outcome (regression)

```basic
' categorical predictor -> dummy columns (first level = reference)
d = stats.dummy_code(group)
fit = stats.ols(y, [x, d.columns[0], d.columns[1]])
fit.coefficients   fit.std_errors   fit.p_values   fit.r_squared   fit.adj_r_squared
ci = stats.conf_int(fit, 0.95)

' interaction term (moderation in a regression):
fit2 = stats.ols(y, [x, d.columns[0], stats.interaction(x, d.columns[0])])

' heteroskedasticity? robust standard errors (HC0/HC1/HC2/HC3):
rob = stats.ols_robust(y, [x, d.columns[0], d.columns[1]], "HC3")
```
**Report:** *b* (SE), *t*, *p* per predictor, and *R²* / adjusted *R²*. With
dummies, each coefficient is the contrast vs the reference level.

## 7. Predict a categorical or count outcome (GLM)

| Outcome | Function | Report |
|---|---|---|
| Binary (0/1) | `logistic_regression(y, xs)` | odds ratios + CI, pseudo-R² |
| Binary (probit) | `probit_regression(y, xs)` | coefficients, marginal effects |
| Ordered / Likert | `ordinal_regression(y, xs)` | proportional-odds ORs, cutpoints |
| Nominal (>2 categories) | `multinomial_regression(y, xs)` | per-category coefficients |
| Counts | `poisson_regression(y, xs)` | incidence-rate ratios |
| Over-dispersed counts | `negbinom_regression(y, xs)` | IRRs, dispersion `alpha` |

```basic
lg  = stats.logistic_regression(yb, [x])
orr = stats.odds_ratios(lg, 0.95)        ' orr[j] = {odds_ratio, ci_low, ci_high}
me  = stats.marginal_effects(lg, [x])    ' average marginal effects (interpretable)
lg.pseudo_r2                       ' McFadden
```
**Report (logistic):** OR = `orr[1].odds_ratio`, 95% CI [`ci_low`, `ci_high`],
*p*; the AME (`me.effects`) as "a one-unit increase in x changes P(y=1) by …";
McFadden pseudo-R². **Likert outcomes belong here** — use `ordinal_regression`,
not OLS.

## 8. Mediation & moderation

**Mediation** (does X affect Y *through* M?):
```basic
seed(42)                              ' reproducible bootstrap CI
med = stats.mediation(y, x, m, 5000)
med.indirect      med.boot_low  med.boot_high    ' a*b with percentile CI
med.c_direct      med.c_total   med.prop_mediated
```
**Report:** indirect effect *a·b* = `med.indirect`, 95% bootstrap CI
[`boot_low`, `boot_high`] — **significant if the CI excludes 0** (the Preacher &
Hayes standard); plus direct and total effects.

**Moderation** (does the X→Y effect *depend on* W?):
```basic
ss = stats.simple_slopes(y, x, w, [mean(w) - stdev(w), mean(w), mean(w) + stdev(w)])
ss.interaction_coef   ss.interaction_p         ' the X*W term
ss.slopes[k]                                    ' {w, slope, se, t, p_value}
```
**Report:** the interaction *b* and *p*; then the simple slope of X at low / mean
/ high W (the classic ±1 SD probe).

## 9. Measurement: reliability & agreement

**Scale reliability** (do these items hang together?):
```basic
stats.cronbach_alpha(items)      ' items[subject][item]; {alpha, n_items, n_subjects}
```
**Content analysis / intercoder agreement:**
```basic
stats.krippendorff_alpha(coded, "nominal")   ' coded[coder][unit], unknown = missing
stats.cohens_kappa(coder1, coder2)           ' two coders
stats.icc(ratings)                           ' intraclass correlation, ratings[subject][rater]
```
**Report:** α ≥ .70 acceptable; Krippendorff α ≥ .80 (≥ .667 tentative). Use
Krippendorff for content analysis (handles >2 coders and missing codes);
`"interval"` / `"ordinal"` levels are available for scaled codes.

## 10. Proportions & campaign metrics

```basic
stats.prop_test_1(successes, n, p0)                 ' one proportion vs a benchmark
stats.prop_test_2(s1, n1, s2, n2)                   ' two proportions
ab = stats.ab_test({successes: 90, n: 1000}, {successes: 120, n: 1000})
ab.lift    ab.p_value    ab.significant       ' relative lift + test
stats.funnel(steps)    stats.cohort_retention(events)     ' conversion / retention
```

## 11. Design: power & sample size

Run **before** collecting data (a priori power) or to justify *N* to reviewers:
```basic
stats.power_ttest(effect_size, n, alpha, sided)     ' two-sample t-test power
stats.power_anova(k, n, effect_size, alpha)         ' one-way ANOVA (Cohen's f)
stats.sample_size_ttest(effect_size, power, alpha, sided)   ' required per-group n
stats.power_ttest_paired(effect_size, n, alpha, sided)
```
**Report:** "To detect *d* = 0.5 at α = .05 with 80% power (two-tailed), we need
*n* = `sample_size_ttest(0.5, 0.8, 0.05, 2)` per group."

## 12. Time to an event (survival analysis)

For outcomes where the question is *when*, not *whether* — relapse, dropout,
churn, time to hire — and where for some people the event **has not happened
yet** when observation stops:

```basic
km = stats.kaplan_meier(times, events)     ' events: 1 = event, 0 = still censored
km.times   km.survival   km.se   km.lower   km.upper
km.median   km.median_reached   km.at_risk   km.n_events
stats.survival_at(km, 10)                  ' S(t) at a specific time
stats.logrank(t_a, e_a, t_b, e_b)          ' compare two groups
```

**Censoring is the whole subject and it is not optional.** Both shortcuts are
wrong and neither announces itself: dropping censored people leaves only
failures and understates survival; counting them as events understates it a
different way. On the standard leukaemia trial those two give medians of 10 and
16 where the answer is **23**. A person censored at time *t* still counts as at
risk *at* t. A median the curve never reaches is `unknown`, not the largest
observed time.

To **model** it rather than describe it — covariates, not just curves:

```basic
cx = stats.cox_ph(times, events, [treatment, age])
cx.hazard_ratios   cx.std_errors   cx.p_values   cx.ci_low   cx.ci_high
stats.hr_per(cx, 1, 10)                    ' the ratio over a 10-unit change in age
```
A hazard ratio is **per unit**, so a covariate in dollars gives 1.0000-something
and reads as nothing — `hr_per` restates it over an interval you choose, without
re-fitting. Ties use Breslow's approximation (stated, not silent). The model
assumes the ratio is **constant over time**: where hazards cross, one number
describes neither period, which no *p*-value reveals and the Kaplan-Meier curves
do.

**Report:** median survival per group with CI, the log-rank χ² and *p*, and for
Cox the HR with CI and *p* — plus a sentence on whether proportional hazards
looked plausible.

## 13. Pooling a literature (meta-analysis)

```basic
ma = stats.meta_analysis(studies, { model: "random" })    ' or "fixed"
ma.estimate   ma.ci_low   ma.ci_high   ma.p
ma.q   ma.q_p   ma.i_squared   ma.tau_squared       ' heterogeneity, always
stats.smd_variance(d, n1, n2)                             ' reported d -> variance
stats.eggers_test(studies)                                ' funnel asymmetry
```
Each study is `{effect:, variance:}`. Random effects (DerSimonian–Laird) is the
honest default when studies differ in population or protocol; the heterogeneity
is reported **beside** the estimate because a pooled number over wildly
heterogeneous studies is a precise summary of nothing.

**Ratio measures pool on the log scale.** An odds, risk or hazard ratio is
multiplicative: 0.5 and 2.0 are the same effect in opposite directions, so the
true pooled effect is *none* — yet averaged as plain numbers they give 1.25, a
25% apparent harm. Pass `scale: "ratio"` and the estimate and interval are
back-transformed. Nothing guesses, because a set of ratios and a set of raw
differences are both just numbers.

**Report:** *k*, the model, the pooled estimate with CI, and *I²* with Q's *p*.

## 14. Latent structure (exploratory factor analysis)

When several items are thought to measure a smaller number of underlying
constructs:

```basic
fa = stats.factor_analysis(cols, { factors: 2, rotate: "varimax" })
fa.loadings   fa.communalities   fa.uniquenesses
fa.eigenvalues   fa.variance_explained   fa.heywood
```

**This is not PCA**, and the difference is the point. PCA summarises the
observed variables and explains *total* variance; factor analysis posits latent
causes and explains *common* variance only — in the arithmetic, 1s versus
communalities down the diagonal. On half-noise data that gap is 0.40 against
0.60, so using PCA where a latent construct is meant overstates what the factors
explain by half. Rotation **cannot improve fit** — it reproduces the same
communalities exactly and only relabels the axes — and it does not promise a
factor *order*, so read the block pattern, never "item 1 is on factor 1". A
communality reaching 1 is a **Heywood case**, reported rather than clamped.

**Report:** the extraction and rotation used, the loading matrix (suppressing
small loadings), communalities, and variance explained.

## 15. Did the program work? (causal designs)

Program evaluation, policy changes and natural experiments use
difference-in-differences (`did`, `pre_trends`) and instrumental variables
(`iv_2sls`). Those live in
**[cookbook_econometrics_finance.md §11](cookbook_econometrics_finance.md)** —
the methods are identical, and the warning that matters is the same in both
fields: the coefficient can be right while the standard error is wrong, and the
wrong one is the flattering one. If you have repeated measures on the same
people or sites, **cluster**.

## 16. Reporting quick reference

| You have | Recipe | Headline numbers |
|---|---|---|
| 2 groups, continuous | `t_test_2sample` + `cohens_d` | *t*, *p*, *d* |
| 3+ groups | `anova_oneway` + `tukey_hsd` + `eta_squared` | *F*, *p*, η², pairs |
| Factor × factor | `anova_twoway` | main + interaction *F*/*p* |
| Two continuous vars | `spearman` / `kendall_tau` | ρ / τ, *p* |
| Continuous outcome + predictors | `ols` (+ `ols_robust`) | *b*, SE, *p*, *R²* |
| Binary outcome | `logistic_regression` + `odds_ratios` | OR, CI, pseudo-R² |
| Likert outcome | `ordinal_regression` | proportional-odds OR |
| Counts | `poisson`/`negbinom_regression` | IRR |
| X → M → Y | `mediation` | indirect + bootstrap CI |
| Effect depends on W | `simple_slopes` | interaction *p*, simple slopes |
| Scale / coders | `cronbach_alpha` / `krippendorff_alpha` | α |
| Two rates / A-B | `prop_test_2` / `ab_test` | *z*, *p*, lift |
| Planning *N* | `sample_size_ttest` / `power_anova` | required *n*, power |
| Time until an event | `kaplan_meier` + `logrank` | median, S(t), χ², *p* |
| Event time + covariates | `cox_ph` (+ `hr_per`) | HR, CI, *p* |
| Many studies, one question | `meta_analysis` | pooled est, CI, *I²* |
| Items → latent constructs | `factor_analysis` | loadings, communalities |
| Program on/off over time | `did` (+ `cluster:`) | ATT, clustered SE, *p* |

---

*Results in §1–§11 are verified numerically against SciPy / statsmodels.
§12–§14 are verified against *published* results and independent
implementations instead: survival and Cox against the Freireich 1963 leukaemia
trial (median 23 weeks, S(10) = 0.7529, log-rank χ² = 16.79, HR 4.523),
meta-analysis against Python, factor analysis against a constructed known
structure. See `docs/statistics_scientist_plan.md` for method notes and
`examples/cookbook_social_test.bas` for the runnable version of every recipe
above. Predictive machine learning (train/test, cross-validation, classifiers) is
a separate track, intentionally out of scope here.*
