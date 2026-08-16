# Statistics Library & Data Frames — Design

Status: **IMPLEMENTED** through the scientist-shortlist phases; 23
`examples/stats_*_test.bas` goldens run in `tests/run_examples.sh`. This line
read "design proposal; Phase 1 numeric foundation in progress" until 2026-08-15,
contradicting the rest of its own paragraph. The
elementary math builtins (now incl. `erf`/`erfc`), the dispersion/shape
reductions, the order statistics / paired measures (`quantile`/`percentile`/`iqr`/
`range`/`correlation`/`covariance`), and the `stdlib/stats.bas` distribution
suite — the **normal**, **Student's t**, **chi-squared**, **F**, **binomial**,
and **Poisson** distributions over shared incomplete-gamma/incomplete-beta
engines (plus the `lgamma` primitive they need), the **`stdlib/matrix.bas`**
toolkit, **`ols`** linear regression, a **seedable RNG** (`seed`/`random`/
`random_int`), **resampling** (`shuffle`/`sample`/`resample`/`bootstrap`), and
the **`stdlib/frame.bas`** structural data-frame layer — have shipped, so
**Phase 1 is complete** (see §8 Phase 1). **Phase 2 (inferential statistics —
t-tests, ANOVA, chi-squared, nonparametric rank tests, confidence intervals,
effect sizes, multiple-comparison corrections, and logistic/Poisson GLM via
IRLS) is also complete** (see §8 Phase 2). **Phase 3 (multivariate / unsupervised
— k-means, agglomerative hierarchical clustering, PCA over a pure-gBASIC Jacobi
eigensolver, and z-score / IQR anomaly detection) is also complete** (see §8
Phase 3); notably the PCA eigensolver stayed in gBASIC — the §6 "earn it" bar for
a C builtin was not crossed. **Phase 4 (time series — moving averages SMA/EWMA,
differencing, acf/pacf, and the exponential-smoothing family: simple, Holt's
linear trend, additive Holt-Winters) is also complete** (see §8 Phase 4),
likewise pure gBASIC. **The ARIMA/GARCH stretch has since shipped too**, on the
shared MLE optimizer it was waiting for — `examples/stats_optimize_test.bas`,
`stats_arima_test.bas`, `stats_arima_mle_test.bas`, `stats_garch_test.bas` —
along with the scientist-shortlist phases (GLM suite, mediation/moderation,
econometric diagnostics, robust SEs, power analysis, reliability, experimental
design), each with its own golden. This sketches the data representation and the
statistical-operation surface for a gBASIC statistics library. The guiding
constraint is **simplicity**: make statistical work as easy as possible *without
sacrificing functionality*, building on the values gBASIC already has. Any change
to the gBASIC core is admitted **only if the library demonstrably earns it** — see
§6.

---

## 1. Representation: a record of lists

A data frame is **column-major**: a record whose fields are equal-length lists.
In Python parlance, a dictionary of lists. This matches every serious data-frame
library (R's `data.frame` is "a named list of equal-length vectors"; pandas,
Arrow, Polars are all columnar) because analytics operate per column.

```basic
people = {
    name:   ["Ada", "Grace", "Alan"],
    age:    [36, 45, 41],
    active: [true, true, false]
}
```

No new value kind, no hidden fields, no magic — a frame is plain inspectable data.

**The payoff: a column is just a list.** `people.age` *is* the age list, so the
entire univariate-statistics surface is ordinary list functions — and most already
exist as builtins (`sum`, `mean`, `median`, `mode`, `min`, `max`, `count`, `sort`,
`unique`). `mean(people.age)` works today. The library only has to add the missing
list functions (`stdev`, `variance`, `quantile`, `correlation`, `covariance`) and
the **structural** layer (import, clean, reshape, group, join). The frame
abstraction earns its keep on multi-column operations, not single-column stats.

### 1.1 Convenience: list-of-records view

Some work is naturally row-wise (iteration, interop, hand-entry). The library
offers a round-tripping conversion to a list of records and back:

```basic
rows = frame.to_rows(people)     ' [ {name:"Ada", age:36, active:true}, ... ]
df   = frame.from_rows(rows)     ' back to the column-major frame
```

`to_rows`/`from_rows` are exact inverses for a well-formed frame. The dict-of-lists
remains the canonical form; the list-of-records is a view for convenience.

---

## 2. Missing data is `unknown`

gBASIC already has a first-class **`unknown`** ("value not available / not yet
known"), distinct from `nothing` ("deliberately absent"). This *is* a native NA —
no sentinel hack of the kind pandas (`NaN`), R (typed `NA`), or SQL (`NULL`) had to
bolt on.

- an empty / NA cell on import becomes `unknown`
- a structurally-missing field is `nothing`
- `is_unknown()` already exists for NA-aware logic

**NA policy:** list statistics **skip `unknown` by default** (documented per
function), with an optional override to propagate (`unknown` in → `unknown` out) or
error. This is the single most important ergonomic decision in the library and
gBASIC gets it nearly for free.

---

## 3. Why a record, not a new value kind

Build on records **all the way**. A new `VALUE_DATAFRAME` is a contingency, not a
plan. A plain record buys, for free, everything a custom kind would have to
re-earn:

- `print`, serialization, actor-send, watchers, COW sharing, `--ast`
- transparency — a frame is inspectable plain data; users can build or repair one
  by hand
- the whole library can be written in gBASIC stdlib (`.bas`), dogfooding the
  language; no C for v1

What a record cannot do, and the honest response to each:

| Limitation | Response |
|---|---|
| No enforced rectangular invariant (equal column lengths) | Library functions validate at entry; cheap. |
| No place for schema / dtypes / NA policy | Infer per-operation on demand (cache later if needed). |
| Prints as a record, not a table | A `frame.show()` function, not a value kind. |
| Boxed `Value`s — large numeric columns are heavy | **The one real future force.** See below. |

**Revisit a dedicated value kind only if** profiling on real datasets shows the
boxed-`Value` overhead is the bottleneck and packed, unboxed, typed columnar
storage (Arrow-style) is required. Until then, records win on every axis that
matters. Design the API so storage could later swap to packed buffers **without
changing user-facing code**.

---

## 4. The workflow → API surface

Statistical work is three phases: import, clean, then operate. Each transform
returns a **new** frame; PBI's copy-on-write cells make this cheap (untouched
columns are shared by reference, only a changed column forks).

### 4.1 Import
```basic
df = frame.read_csv(path)        ' per-column type inference; empty cells -> unknown
frame.write_csv(df, path)
```
Caution: gBASIC numbers are IEEE doubles (no exact int), so import keeps
**ID/account-like columns as strings** to avoid silent precision loss on big
integers — the same rule the DB module mappings already follow.

### 4.2 Clean / reshape (each returns a new frame)
```basic
frame.select(df, ["name", "age"])      ' keep columns
frame.drop(df, ["scratch"])
frame.rename(df, { age: "years" })
frame.filter(df, function(row) return row.age >= 40 end function)
frame.with_column(df, "adult", function(row) return row.age >= 18 end function)
frame.coerce(df, "age", "number")      ' retype a column
frame.fill_missing(df, "age", 0)       ' or frame.drop_missing(df)
frame.dedupe(df)
frame.sort_by(df, "age")
frame.join(left, right, on: "id")
```

### 4.3 Operate
Single-column stats are list functions (existing + added):
```basic
mean(df.age)         median(df.age)        mode(df.age)
stdev(df.age)        variance(df.age)      quantile(df.age, 0.95)
correlation(df.height, df.weight)
```
Multi-column / grouped — split-apply-combine:
```basic
frame.summarize(df, by: "city", {
    n:       function(g) return count(g.age)  end function,
    avg_age: function(g) return mean(g.age)   end function
})
```
`summarize` splits the frame into per-group sub-frames, applies each aggregator,
and combines the results into a new frame.

### 4.4 Inspect
```basic
frame.shape(df)      ' [rows, cols]
frame.columns(df)    ' list of names
frame.dtypes(df)     ' inferred type per column
frame.show(df)       ' pretty table ; frame.head(df, n)
```

---

## 5. Library shape

The library ships as gBASIC stdlib modules, loaded with `load` (like
`stdlib/dates.bas`):

- **C builtins** — elementary math and the basic list reductions (mean/median/
  mode/variance/stdev/…), because they join an existing builtin family and must
  work without a `load` (see §8 Phase 1).
- `stdlib/stats.bas` — higher-level *compositions* in gBASIC: distributions,
  hypothesis tests, regression.
- `stdlib/frame.bas` — the structural `frame` layer (§4.1, §4.2, §4.4).

Compositions are written in gBASIC, not C. One drops to a C builtin **only if**
profiling proves the gBASIC implementation is too slow on real data — same earn-it
rule as §3.

---

## 6. Possible core improvements (flagged, not committed)

These would improve ergonomics but touch the gBASIC core, so each is admitted
**only if the library proves it necessary** in practice:

1. **Pipe / chaining operator** (`df |> filter(...) |> summarize(...)`). Statistical
   work is pipelines, and the string-modifier pipelines are a precedent. The
   highest-value ergonomic, but a real grammar change — defer until the
   function-call nesting actually hurts.
2. **Vectorized column ops** (`df.age * 2`, `df.age > 30`). gBASIC has no operator
   overloading. Resolve with functions first (`scale(df.age, 2)`); consider a typed
   **Series** abstraction (column + dtype + NA metadata) only if function-style
   vectorization proves too clumsy.
3. **Packed columnar storage / dedicated value kind** — the §3 scaling cliff. Only
   if real datasets prove boxed `Value`s are the bottleneck.

The library is built first; these are revisited from evidence, not anticipated.

---

## 7. Open questions

1. **Row identity / index.** v1 uses implicit integer row position. A named index
   (pandas) / row names (R) is deferred until a use case needs it.
2. **Type inference vs explicit schema.** Infer per-operation (simplest) vs let a
   frame carry an explicit dtype map. Start inferred.
3. **`filter`/`with_column` predicate form.** Row-record callback (shown above,
   readable) vs column-mask (faster, more vectorized-feeling). Possibly both.
4. **Join semantics.** Which join kinds (inner/left/outer), key collision rules,
   and column-name disambiguation on overlap.
5. **Categorical / factor columns.** Whether to model categoricals specially or
   leave them as string columns with helper functions.
6. **Filter-pipeline data cleaning (planned, undeveloped).** A direction under
   development: model cleaning as a *filter through which each observation flows*
   — a declarative pipeline of per-observation rules — rather than ad-hoc
   imperative transforms (it rhymes with gBASIC's modifier/lens pipelines). May
   reshape the §4.2 clean API. To be fleshed out in a later phase.

---

## 8. Roadmap & development plan

**Committed scope is Phase 1.** Everything beyond it is *planned, not promised* —
the catalog is comprehensive so the architecture is right, but each later phase is
admitted on evidence and appetite. Most "advanced" methods are thin compositions
of the Phase 1 foundation (a distribution + a regression + a p-value), so Phase 1
is the load-bearing work.

Each phase follows the project's golden-file test convention: bake externally
computed reference values (R/numpy/scipy) into `.out` fixtures.

### Shared infrastructure (cuts across all phases)
- **`stdlib/matrix.bas` — vector/matrix toolkit.** Covariance and OLS need it in
  Phase 1; eigen/SVD make it heavy by Phase 3. This is the most likely component
  to *earn* C builtins (the §3 scaling cliff) — defer until profiling demands it.
- **Seedable RNG. DONE.** `seed(n)` / `random()` / `random_int(lo, hi)` C
  builtins — xoshiro256** seeded via SplitMix64, pure fixed-width integer math
  so the stream is byte-identical across x86 / s390x / riscv64 (the exact-match
  golden tests rely on this). Unseeded programs auto-seed from `/dev/urandom`;
  tests call `seed()` for reproducibility. Kept distinct from `secure_token`,
  which stays a non-reproducible CSPRNG. Verified in
  `examples/stats_resample_test.bas`.
- **Cross-architecture float determinism.** Golden tests are exact string matches,
  and gBASIC now runs on x86, s390x, and riscv64. Floating-point results can differ
  in the last bits across architectures, so **stats output for fixtures must be
  rounded to a fixed precision** (define a display-precision convention) or goldens
  will be flaky across the ports.

### Phase 1 — Foundation *(ship first)*
- **Goal:** the primitives every later phase composes from.
- **Deliverables:**
  - **DONE — elementary scalar math builtins** (`sqrt`, `abs`, `exp`, `log`,
    `log10`, `floor`, `ceil`, `sign`, `pow`) in `eval.c`. Discovered during Phase 1
    that gBASIC had *no* math functions at all (only `round`); no statistical or
    scientific library can exist without these, so they were the true first step —
    the clearest possible "library earns a core change."
  - **DONE — dispersion/shape reductions** `variance`/`stdev` (sample, n−1),
    `pvariance`/`pstdev` (population, n), `skewness`, `kurtosis` (moment-based,
    excess kurtosis — scipy defaults). **Decision (revises §3/§5): these ship as C
    builtins alongside the existing `mean`/`median`/`mode`**, not as `stdlib/
    stats.bas`. Rationale: their siblings are already builtins, and the north-star
    is simplicity — `stdev(xs)` should just work like `mean(xs)` with no `load`.
    The gBASIC `stdlib` layer is reserved for higher compositions (distributions,
    tests, regression, the frame layer). Verified against hand-computed values in
    `examples/stats_test.bas`.
  - **DONE — order statistics and paired measures** (also C builtins, same
    rationale): `range` and `iqr` (1-arg), `quantile(xs, q)` / `percentile(xs, p)`
    (2-arg, type-7 linear interpolation — the R/NumPy default), `correlation`
    (Pearson's r) and `covariance` (sample, n−1, two equal-length arrays). The
    quantile interpolation lives in one helper (`quantile_sorted`) so `iqr` and
    `percentile` share it. Verified in `examples/stats_test.bas`.
  - **DONE — `erf`/`erfc` scalar builtins** (libm, same family as `exp`/`log`):
    the primitive the normal CDF needs. Added to the §8 elementary-math block.
  - **DONE — normal distribution** in `stdlib/stats.bas` — the first real gBASIC
    *composition*, proving the "compositions are written in gBASIC, not C" rule.
    `normal_pdf`/`normal_cdf`/`normal_quantile`/`zscore` for any `(mu, sigma)`.
    CDF via `erfc`; the inverse CDF is Acklam's rational approximation refined by
    one Halley step against the erf-based CDF (near machine precision on (0,1)).
    Out-of-domain inputs (σ ≤ 0, p ∉ (0,1)) return `unknown`, never a bogus
    number. Verified against scipy in `examples/stats_normal_test.bas`.
  - **DONE — `lgamma` scalar builtin** (libm, same elementary-math family):
    the one new C primitive the remaining distribution CDFs need. Poles at
    non-positive integers raise rather than return a bogus number.
  - **DONE — incomplete-gamma / incomplete-beta engines** in `stdlib/stats.bas`,
    written in gBASIC over `lgamma` (the "compositions in gBASIC" rule):
    regularized lower incomplete gamma `_gammp` (series + continued-fraction by
    region, Numerical Recipes' gser/gcf) and regularized incomplete beta
    `_betai` (Lentz continued fraction). These are the shared CDF engines.
  - **DONE — t, χ², F, binomial, Poisson distributions** in `stdlib/stats.bas`,
    each with `pdf`/`pmf`, `cdf`, and `quantile`. Continuous CDFs go through
    `_betai` (t, F) or `_gammp` (χ²); discrete CDFs use the same engines via the
    incomplete-beta / incomplete-gamma identities (binomial = `_betai`,
    Poisson = `_gammp`). Quantiles are bisection on the monotone CDF (continuous)
    or smallest-`k` search (discrete). Out-of-domain inputs return `unknown`.
    Verified against scipy in `examples/stats_dist_test.bas`. `sample` (random
    draws) is deferred until the seedable RNG lands.
  - Remaining: `sample`/resampling (`shuffle`, `bootstrap`) — pending the RNG.
  - **DONE — `stdlib/matrix.bas`** — the shared vector/matrix toolkit
    (transpose, multiply, matrix×vector, identity, Gauss–Jordan inverse with
    partial pivoting, dot). A matrix is a list of rows, indices 0-based;
    shape mismatch / singular / non-square return `unknown`. Written in gBASIC
    (the component most likely to *earn* C builtins later). Verified against
    numpy in `examples/matrix_test.bas`.
  - **DONE — `ols(y, xs)`** in `stdlib/stats.bas` — linear regression via the
    normal equations over `matrix.bas`. `xs` is a list of predictor columns (or
    a flat list for simple regression); an intercept is fitted automatically.
    Returns coefficients, fitted, residuals, `r_squared`, `adj_r_squared`,
    `std_errors`, `t_values`, `p_values` (two-sided, via `t_cdf` on df = n−p),
    `n`, `df`; `unknown` on malformed / under-determined / rank-deficient input.
    Verified against numpy/scipy in `examples/stats_ols_test.bas`. (Normal
    equations as noted below; QR hardening deferred.)
  - **DONE — resampling** in `stdlib/stats.bas` over the seedable RNG:
    `shuffle` (Fisher–Yates, non-mutating), `sample(xs, k)` (without
    replacement), `resample(xs, k)` (with replacement), and `bootstrap(xs,
    statistic, b)` (b size-n resamples → the list of statistic values).
    `statistic` must be a user-defined function value (builtins aren't yet
    first-class — wrap them). Verified in `examples/stats_resample_test.bas`.
  - **DONE — `stdlib/frame.bas`**: the §4 structural layer, all in gBASIC over
    plain records-of-lists (no new value kind). Inspect (`shape`/`columns`/
    `dtypes`/`head`/`show`), conversions (`to_rows`/`from_rows`), select/reshape
    (`select`/`drop`/`rename`/`filter`/`with_column`/`coerce`), clean
    (`fill_missing`/`drop_missing`/`dedupe`/`sort_by`), group (`summarize`,
    split-apply-combine with user-function aggregators), `join` (inner, on a key
    column, collisions suffixed `_right`), and CSV IO (`read_csv` with per-column
    type inference — empty → `unknown`, ID-like/precision-risk columns kept as
    strings; `write_csv`). Every transform returns a new frame (PBI COW).
    Verified in `examples/stats_frame_test.bas`. Known limits: `read_csv` is
    simple comma-splitting (no quoted fields / embedded commas yet); `filter`/
    `with_column`/`summarize` take user-function values (builtins aren't yet
    first-class). **Phase 1 is now complete.**
- **Dependencies:** first-class functions ✓, `unknown` ✓, list builtins ✓, RNG,
  `matrix.bas`.
- **Key risks (numerical):** stable variance (Welford / two-pass); a chosen
  quantile-interpolation convention; `erf`-based normal CDF; incomplete-beta /
  incomplete-gamma for t/F/χ² CDFs with Newton/bisection inverses; OLS via normal
  equations is simplest but ill-conditioning-prone (note QR as later hardening).

### Phase 2 — Inferential (science core) — **COMPLETE**
- **Goal:** hypothesis testing + GLM, mostly compositions of Phase 1.
- **Deliverables (all DONE, in `stdlib/stats.bas`):**
  - **DONE — t-tests:** `t_test_1sample(xs, mu0)`, `t_test_2sample(xs, ys)`
    (pooled / equal-variance), `t_test_welch(xs, ys)` (Welch–Satterthwaite df),
    `t_test_paired(xs, ys)` (one-sample t on the paired differences). Each
    returns `{statistic, df, p_value, …}` with a two-sided p via `t_cdf`.
  - **DONE — `confidence_interval(xs, level)`** — Student-t interval for a mean
    (`mean`/`lower`/`upper`/`se`/`margin`/`df`).
  - **DONE — `cohens_d(xs, ys)`** — pooled-SD effect size.
  - **DONE — `anova_oneway(groups)`** — one-way ANOVA over a list of groups;
    returns F, between/within df + SS + MS, and p via `f_cdf`.
  - **DONE — chi-squared:** `chi_square_gof(observed, expected)` (goodness of
    fit, df = k−1) and `chi_square_independence(table)` (contingency table,
    df = (r−1)(c−1), returns the expected-count table); p via `chi2_cdf`.
  - **DONE — nonparametric (rank-based):** shared `_rank` (average ties) and
    `_tie_term` helpers, then `mann_whitney(xs, ys)` (U for the first sample,
    tie-corrected normal approx), `wilcoxon(xs, ys)` (signed-rank, zeros
    dropped, statistic = min(W+, W−)), `kruskal_wallis(groups)` (tie-corrected
    H, χ² approx). All without continuity correction (matched to scipy's
    `use_continuity=False` / `correction=False` asymptotic modes).
  - **DONE — multiple-comparison corrections:** `bonferroni(pvals)` and
    `benjamini_hochberg(pvals)` (step-up FDR with monotone enforcement),
    both returning adjusted p-values aligned to input order.
  - **DONE — GLM via IRLS:** `logistic_regression(y, xs)` (logit link) and
    `poisson_regression(y, xs)` (log link), sharing `_wls_step` (weighted
    normal equations over `matrix.bas`), `_design`, `_norm_cols`, and
    `_glm_result`. Each returns coefficients (intercept first), std_errors,
    z_values, two-sided p_values (normal), fitted, log_likelihood, iterations,
    converged, n; `unknown` on malformed / singular input.
- **Verification:** every value checked against `scipy.stats` (t-tests, ANOVA,
  chi-squared, Mann–Whitney/Wilcoxon/Kruskal–Wallis) and `statsmodels`
  (BH FDR, GLM logistic + Poisson) in `examples/stats_inference_test.bas`.
- **Dependencies:** Phase 1 distributions, `ols`, rank utilities — all present.
- **Notes:** Welch–Satterthwaite df, IRLS convergence (max-Δβ < 1e-10, 100-iter
  cap, weights floored at 1e-10 against separation), and average-rank tie
  handling all landed as planned. Out-of-domain inputs return `unknown` (no new
  C builtins were needed — Phase 2 is pure gBASIC composition).

### Phase 3 — Multivariate / unsupervised — **DONE (2026-06-30)**
- **Goal:** segmentation, dimensionality reduction, outliers.
- **Shipped** (all in `stdlib/stats.bas`, pure gBASIC — **no new C builtins**):
  - `kmeans(data, k, max_iter)` — Lloyd's algorithm with **k-means++** seeding
    (RNG-driven, so `seed()` for reproducibility); returns `labels`, `centroids`,
    `inertia`, `iterations`, `converged`. Finds the global optimum on
    well-separated data (verified inertia against the true-partition value).
  - `hierarchical(data, method)` — agglomerative clustering, `method` ∈
    `single`/`complete`/`average` (UPGMA); returns `merges` in scipy **linkage Z**
    format (`[id1, id2, dist, size]`, leaves `0..n-1`, merge `m` → id `n+m`,
    smaller id first). Companion `cut_tree(model, k)` cuts the dendrogram to `k`
    flat clusters. Merge rows match `scipy.cluster.hierarchy.linkage` exactly when
    merge distances are distinct (ties are where scipy's chain order would diverge
    — the test data is chosen tie-free).
  - `pca(data, ncomp)` — centre → sample covariance (ddof=1, matching sklearn's
    `explained_variance_`) → **`_jacobi_eigen`** (cyclic Jacobi, trig-free
    Numerical-Recipes rotation, sqrt only) → top `ncomp` `components`,
    `explained_variance`, `explained_variance_ratio`, projected `scores`, `mean`.
    Each component is **sign-fixed** (largest-magnitude loading positive) for
    cross-run / cross-arch determinism.
  - `zscore_outliers(xs, threshold)` — population-stdev z-scores (matches
    `scipy.stats.zscore` ddof=0); `iqr_outliers(xs, k)` — Tukey fences over the
    numpy-linear-interp `quantile`.
  - Shared helpers: `_dist2`/`_euclid`/`_copy_point`/`_concat`/`_link_dist`.
  - Each entry point returns a record or `unknown` on malformed input.
- **Verified** against scipy / numpy in `examples/stats_cluster_test.bas`
  (133 positive / 189 negative suites green).
- **Earn-it outcome:** the Jacobi eigensolver — the §6 candidate for the first
  *earned* C builtin — proved fast and accurate enough in pure gBASIC at these
  sizes, so the C line was **not** crossed. Revisit only if PCA on large feature
  counts shows up in a profile.
- **Deferred (stretch):** `dbscan`, `isolation_forest`, Ward linkage.

### Phase 4 — Time series — **DONE (2026-06-30)**
- **Goal:** temporal methods (finance + business forecasting).
- **Shipped** (all in `stdlib/stats.bas`, pure gBASIC — **no new C builtins**):
  - **Moving averages:** `sma(xs, window)` (trailing window, first `window-1`
    positions `unknown`, matching `pandas.rolling`), `ewma(xs, alpha)` (recursive
    `adjust=False` form, matching `pandas.ewm`).
  - **`diff(xs, d)`** — d-th order differencing for de-trending.
  - **`acf(xs, nlags)`** — biased autocorrelation (divides by n, full-series
    demean), matching `statsmodels acf(adjusted=False)`; lags `0..nlags`, lag 0 = 1.
  - **`pacf(xs, nlags)`** — partial autocorrelation via Durbin-Levinson over the
    biased acf (Yule-Walker), matching `statsmodels pacf(method="ywm")`.
  - **Exponential smoothing (Hyndman state-space recursions, parameters supplied
    not optimized):** `ses(xs, alpha, h)` (simple), `holt(xs, alpha, beta, h)`
    (additive linear trend), `holt_winters(xs, alpha, beta, gamma, period, h)`
    (additive triple — seasonal init from first cycle, trend init from the first
    two cycle means, recursion from `t=period`). Each returns level/trend/(season)/
    `fitted`/`forecast`/`sse`, or `unknown` on bad domain.
  - Shared helper: `_range_mean(xs, lo, count)` for seasonal/trend init.
- **Verified** against pandas / statsmodels in `examples/stats_timeseries_test.bas`
  (134 positive / 189 negative suites green). Smoothing recursions cross-checked
  against an independent Python reimplementation of the same state-space form
  (per the "test components, not end-to-end" rule — fixed parameters, no MLE).
- **Deferred (stretch):** `arima`, `garch` — both need a shared **MLE optimizer**
  (`optimize` primitive worth factoring out); date/time first-class values would
  help time indexing. MLE convergence and exact ARIMA/GARCH parity remain the
  open risks when that work is taken up.

---

## 9. Industry lenses (appendix)

Thin vocabulary packs composed over the core; build any order once the relevant
phases exist. They are *compositions*, not new engines.

- **`stats.finance`** — returns (simple/log/cumulative), annualized volatility,
  **Sharpe** / **Sortino**, **VaR** / **CVaR**, **max drawdown**, **CAPM** α/β (an
  `ols` against the market), **Monte Carlo** (RNG + distributions), portfolio
  covariance / mean-variance optimization. *(needs P1; GARCH from P4.)*
- **`stats.business`** — **RFM** segmentation, **A/B testing** (two-proportion z /
  χ²), cohort/funnel summaries, **anomaly detection** (P3), market-basket / Apriori.
- **`stats.science`** — convenience wrappers over P2, **power analysis**, FDR
  correction, tidy reporting helpers.

---

End of statistics design.
