' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' stats.bas — higher-level statistical compositions in gBASIC.
'
' These build on the C builtins (the elementary math and list reductions from
' statistics_design.md §8 Phase 1) without dropping to C themselves — the
' "compositions are written in gBASIC" rule. This module is the first proof of
' that layering: the normal distribution, expressed entirely in gBASIC on top of
' exp/log/sqrt/erf/erfc.
'
' Out-of-domain inputs return `unknown` (the native NA), never a bogus number:
' a non-positive standard deviation, or a quantile probability outside (0,1).
library stats
    ' OLS regression composes over the shared matrix toolkit (same stdlib dir).
    load matrix from "matrix.bas"

    ' sqrt(2) and sqrt(2*pi) as constants — gBASIC has no scientific-notation
    ' literals, so the digits are written out in full.
    function _sqrt2()
        return 1.4142135623730951
    end function

    function _sqrt2pi()
        return 2.5066282746310002
    end function

    ' Standard normal CDF, Phi(z), via the complementary error function.
    ' Phi(z) = 0.5 * erfc(-z / sqrt(2)).
    function _norm_cdf_std(z)
        return 0.5 * erfc((0 - z) / _sqrt2())
    end function

    ' Standard normal inverse CDF (the probit), Acklam's rational approximation
    ' refined by a single Halley step against the erf-based CDF. Accurate to
    ' near machine precision across (0,1).
    function _inv_norm_std(p)
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if

        a1 = -39.69683028665376
        a2 = 220.9460984245205
        a3 = -275.9285104469687
        a4 = 138.3577518672690
        a5 = -30.66479806614716
        a6 = 2.506628277459239

        b1 = -54.47609879822406
        b2 = 161.5858368580409
        b3 = -155.6989798598866
        b4 = 66.80131188771972
        b5 = -13.28068155288572

        c1 = -0.007784894002430293
        c2 = -0.3223964580411365
        c3 = -2.400758277161838
        c4 = -2.549732539343734
        c5 = 4.374664141464968
        c6 = 2.938163982698783

        d1 = 0.007784695709041462
        d2 = 0.3224671290700398
        d3 = 2.445134137142996
        d4 = 3.754408661907416

        if p < 0.02425 then
            q = sqrt(0 - 2 * log(p))
            z = (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) / ((((d1 * q + d2) * q + d3) * q + d4) * q + 1)
            goto refine
        end if
        if p > 0.97575 then
            q = sqrt(0 - 2 * log(1 - p))
            z = 0 - (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) / ((((d1 * q + d2) * q + d3) * q + d4) * q + 1)
            goto refine
        end if

        q = p - 0.5
        r = q * q
        z = (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q / (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1)

    refine:
        e = _norm_cdf_std(z) - p
        u = e * _sqrt2pi() * exp(z * z / 2)
        z = z - u / (1 + z * u / 2)
        return z
    end function

    ' Normal probability density at x for mean mu and standard deviation sigma.
    function normal_pdf(x, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        z = (x - mu) / sigma
        return exp(0 - z * z / 2) / (sigma * _sqrt2pi())
    end function

    ' Normal cumulative probability P(X <= x).
    function normal_cdf(x, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        return _norm_cdf_std((x - mu) / sigma)
    end function

    ' Normal quantile (inverse CDF): the x with P(X <= x) = p.
    function normal_quantile(p, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        z = _inv_norm_std(p)
        if is_unknown(z) then
            return unknown
        end if
        return mu + sigma * z
    end function

    ' Standardize x to a z-score under N(mu, sigma).
    function zscore(x, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        return (x - mu) / sigma
    end function

    ' --- Special functions: the engines the t / chi-squared / F / Poisson CDFs
    ' compose from. The only new C primitive they need is lgamma (log Gamma);
    ' everything else is gBASIC, following the "compositions in gBASIC" rule.

    function _pi()
        return 3.141592653589793
    end function

    ' Regularized lower incomplete gamma P(a, x) by its series expansion;
    ' valid (and fast) for x < a + 1. Numerical Recipes' gser.
    function _gser(a, x)
        if x <= 0 then
            return 0
        end if
        eps = pow(10, -14)
        ap = a
        sum = 1 / a
        del = sum
        n = 1
        while n <= 300
            ap = ap + 1
            del = del * x / ap
            sum = sum + del
            if abs(del) < abs(sum) * eps then
                break
            end if
            n = n + 1
        end while
        return sum * exp(-x + a * log(x) - lgamma(a))
    end function

    ' Regularized upper incomplete gamma Q(a, x) by its continued fraction;
    ' valid (and fast) for x >= a + 1. Numerical Recipes' gcf (Lentz).
    function _gcf(a, x)
        eps = pow(10, -14)
        tiny = pow(10, -30)
        b = x + 1 - a
        c = 1 / tiny
        d = 1 / b
        h = d
        i = 1
        while i <= 300
            an = -i * (i - a)
            b = b + 2
            d = an * d + b
            if abs(d) < tiny then
                d = tiny
            end if
            c = b + an / c
            if abs(c) < tiny then
                c = tiny
            end if
            d = 1 / d
            del = d * c
            h = h * del
            if abs(del - 1) < eps then
                break
            end if
            i = i + 1
        end while
        return exp(-x + a * log(x) - lgamma(a)) * h
    end function

    ' Regularized lower incomplete gamma P(a, x): the chi-squared / Poisson
    ' engine. Picks series or continued fraction by region.
    function _gammp(a, x)
        if a <= 0 then
            return unknown
        end if
        if x < 0 then
            return unknown
        end if
        if x < a + 1 then
            return _gser(a, x)
        end if
        return 1 - _gcf(a, x)
    end function

    ' Continued fraction for the incomplete beta function (Lentz).
    ' Numerical Recipes' betacf.
    function _betacf(a, b, x)
        eps = pow(10, -14)
        tiny = pow(10, -30)
        qab = a + b
        qap = a + 1
        qam = a - 1
        c = 1
        d = 1 - qab * x / qap
        if abs(d) < tiny then
            d = tiny
        end if
        d = 1 / d
        h = d
        m = 1
        while m <= 300
            m2 = 2 * m
            aa = m * (b - m) * x / ((qam + m2) * (a + m2))
            d = 1 + aa * d
            if abs(d) < tiny then
                d = tiny
            end if
            c = 1 + aa / c
            if abs(c) < tiny then
                c = tiny
            end if
            d = 1 / d
            h = h * d * c
            aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
            d = 1 + aa * d
            if abs(d) < tiny then
                d = tiny
            end if
            c = 1 + aa / c
            if abs(c) < tiny then
                c = tiny
            end if
            d = 1 / d
            del = d * c
            h = h * del
            if abs(del - 1) < eps then
                break
            end if
            m = m + 1
        end while
        return h
    end function

    ' Regularized incomplete beta I_x(a, b): the t / F / binomial engine.
    function _betai(a, b, x)
        if x < 0 then
            return unknown
        end if
        if x > 1 then
            return unknown
        end if
        if x = 0 then
            return 0
        end if
        if x = 1 then
            return 1
        end if
        bt = exp(lgamma(a + b) - lgamma(a) - lgamma(b) + a * log(x) + b * log(1 - x))
        if x < (a + 1) / (a + b + 2) then
            return bt * _betacf(a, b, x) / a
        end if
        return 1 - bt * _betacf(b, a, 1 - x) / b
    end function

    ' --- Student's t distribution (df degrees of freedom) ---

    function t_pdf(x, df)
        if df <= 0 then
            return unknown
        end if
        lc = lgamma((df + 1) / 2) - lgamma(df / 2)
        return exp(lc) / sqrt(df * _pi()) * pow(1 + x * x / df, -(df + 1) / 2)
    end function

    function t_cdf(x, df)
        if df <= 0 then
            return unknown
        end if
        ib = 0.5 * _betai(df / 2, 0.5, df / (df + x * x))
        if x > 0 then
            return 1 - ib
        end if
        return ib
    end function

    function t_quantile(p, df)
        if df <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        tol = pow(10, -10)
        lo = -1000000
        hi = 1000000
        i = 0
        while i < 200
            mid = (lo + hi) / 2
            if t_cdf(mid, df) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < tol then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' --- Chi-squared distribution (k degrees of freedom) ---

    function chi2_pdf(x, k)
        if k <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        if x = 0 then
            if k = 2 then
                return 0.5
            end if
            if k > 2 then
                return 0
            end if
            return unknown
        end if
        return exp((k / 2 - 1) * log(x) - x / 2 - (k / 2) * log(2) - lgamma(k / 2))
    end function

    function chi2_cdf(x, k)
        if k <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        return _gammp(k / 2, x / 2)
    end function

    function chi2_quantile(p, k)
        if k <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        tol = pow(10, -10)
        lo = 0
        hi = 1000000
        i = 0
        while i < 300
            mid = (lo + hi) / 2
            if chi2_cdf(mid, k) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < tol then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' --- F distribution (d1 numerator, d2 denominator degrees of freedom) ---

    function f_pdf(x, d1, d2)
        if d1 <= 0 then
            return unknown
        end if
        if d2 <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        if x = 0 then
            if d1 = 2 then
                return 1
            end if
            if d1 > 2 then
                return 0
            end if
            return unknown
        end if
        lb = lgamma((d1 + d2) / 2) - lgamma(d1 / 2) - lgamma(d2 / 2)
        return exp(lb + (d1 / 2) * log(d1 / d2) + (d1 / 2 - 1) * log(x) - ((d1 + d2) / 2) * log(1 + d1 * x / d2))
    end function

    function f_cdf(x, d1, d2)
        if d1 <= 0 then
            return unknown
        end if
        if d2 <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        return _betai(d1 / 2, d2 / 2, d1 * x / (d1 * x + d2))
    end function

    function f_quantile(p, d1, d2)
        if d1 <= 0 then
            return unknown
        end if
        if d2 <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        tol = pow(10, -10)
        lo = 0
        hi = 1000000
        i = 0
        while i < 300
            mid = (lo + hi) / 2
            if f_cdf(mid, d1, d2) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < tol then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' --- Binomial distribution (n trials, success probability p) ---

    function binom_pmf(k, n, p)
        if p < 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        if n < 0 then
            return unknown
        end if
        if k < 0 then
            return 0
        end if
        if k > n then
            return 0
        end if
        if p = 0 then
            if k = 0 then
                return 1
            end if
            return 0
        end if
        if p = 1 then
            if k = n then
                return 1
            end if
            return 0
        end if
        lc = lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1)
        return exp(lc + k * log(p) + (n - k) * log(1 - p))
    end function

    ' P(X <= k) via the regularized incomplete beta identity
    ' I_{1-p}(n-k, k+1) (exact, and far cheaper than summing pmf for large n).
    function binom_cdf(k, n, p)
        if p < 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        if n < 0 then
            return unknown
        end if
        kk = floor(k)
        if kk < 0 then
            return 0
        end if
        if kk >= n then
            return 1
        end if
        return _betai(n - kk, kk + 1, 1 - p)
    end function

    function binom_quantile(p, n, prob)
        if prob < 0 then
            return unknown
        end if
        if prob > 1 then
            return unknown
        end if
        if n < 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        k = 0
        while k <= n
            if binom_cdf(k, n, prob) >= p then
                return k
            end if
            k = k + 1
        end while
        return n
    end function

    ' --- Poisson distribution (rate lambda) ---

    function pois_pmf(k, lambda)
        if lambda < 0 then
            return unknown
        end if
        if k < 0 then
            return 0
        end if
        kk = floor(k)
        if lambda = 0 then
            if kk = 0 then
                return 1
            end if
            return 0
        end if
        return exp(kk * log(lambda) - lambda - lgamma(kk + 1))
    end function

    ' P(X <= k) = Q(k+1, lambda) = 1 - P(k+1, lambda) (regularized incomplete
    ' gamma); the same gamma engine the chi-squared CDF uses.
    function pois_cdf(k, lambda)
        if lambda < 0 then
            return unknown
        end if
        kk = floor(k)
        if kk < 0 then
            return 0
        end if
        if lambda = 0 then
            return 1
        end if
        return 1 - _gammp(kk + 1, lambda)
    end function

    function pois_quantile(p, lambda)
        if lambda < 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        k = 0
        while k < 10000000
            if pois_cdf(k, lambda) >= p then
                return k
            end if
            k = k + 1
        end while
        return unknown
    end function

    ' --- Ordinary least squares regression ---
    '
    ' y is a list of n responses; xs is the predictors either as a list of
    ' columns (each a list of n values) or, for simple regression, a single
    ' flat list of n values. An intercept is fitted automatically, so
    ' coefficients[0] is the intercept and coefficients[j] the slope for the
    ' (j-1)th predictor column. Returns a record:
    '   coefficients, fitted, residuals, r_squared, adj_r_squared,
    '   std_errors, t_values, p_values, n, df
    ' or `unknown` if the inputs are malformed, under-determined, or the design
    ' matrix is rank-deficient. Two-sided p-values use the t distribution on
    ' df = n - (predictors + 1).
    function ols(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if

        ' Normalize predictors to a list of columns.
        cols = xs
        if len(xs) > 0 then
            if not is_array(xs[0]) then
                cols = [xs]
            end if
        end if
        k = len(cols)

        ' Every predictor column must match the response length.
        c = 0
        while c < k
            if len(cols[c]) != n then
                return unknown
            end if
            c = c + 1
        end while

        ' Need more observations than parameters for a residual df.
        p = k + 1
        if n <= p then
            return unknown
        end if

        ' Design matrix: leading intercept column of ones, then the predictors.
        bigx = []
        i = 0
        while i < n
            row = []
            append(row, 1)
            c = 0
            while c < k
                append(row, cols[c][i])
                c = c + 1
            end while
            append(bigx, row)
            i = i + 1
        end while

        ' Normal equations: beta = (X'X)^-1 X'y.
        xt = mat_transpose(bigx)
        xtxinv = mat_inverse(mat_mul(xt, bigx))
        if is_unknown(xtxinv) then
            return unknown
        end if
        beta = mat_vec(xtxinv, mat_vec(xt, y))

        ' Fitted values, residuals, and residual sum of squares.
        fitted = mat_vec(bigx, beta)
        residuals = []
        rss = 0
        i = 0
        while i < n
            e = y[i] - fitted[i]
            append(residuals, e)
            rss = rss + e * e
            i = i + 1
        end while

        ' Total sum of squares about the mean.
        ybar = mean(y)
        tss = 0
        i = 0
        while i < n
            d = y[i] - ybar
            tss = tss + d * d
            i = i + 1
        end while

        r2 = unknown
        if tss > 0 then
            r2 = 1 - rss / tss
        end if
        dof = n - p
        adj = unknown
        if not is_unknown(r2) then
            adj = 1 - (1 - r2) * (n - 1) / dof
        end if

        ' Coefficient covariance is sigma^2 (X'X)^-1; SEs are its diagonal.
        sigma2 = rss / dof
        ses = []
        tvals = []
        pvals = []
        j = 0
        while j < p
            v = sigma2 * xtxinv[j][j]
            se = unknown
            tv = unknown
            pv = unknown
            if v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    tv = beta[j] / se
                    pv = 2 * (1 - t_cdf(abs(tv), dof))
                end if
            end if
            append(ses, se)
            append(tvals, tv)
            append(pvals, pv)
            j = j + 1
        end while

        ' Full coefficient covariance sigma^2 (X'X)^-1 (for simple slopes etc.).
        cov = []
        a = 0
        while a < p
            row = []
            b = 0
            while b < p
                append(row, sigma2 * xtxinv[a][b])
                b = b + 1
            end while
            append(cov, row)
            a = a + 1
        end while

        return { coefficients: beta, fitted: fitted, residuals: residuals, r_squared: r2, adj_r_squared: adj, std_errors: ses, t_values: tvals, p_values: pvals, cov: cov, n: n, df: dof }
    end function

    ' --- Resampling ---
    '
    ' These draw from the seedable PRNG (the seed()/random()/random_int()
    ' builtins). Call seed(n) first for reproducible results; tests rely on it.

    ' Fisher-Yates shuffle. Returns a new list; the input is left untouched
    ' (gBASIC lists are copy-on-write, so the local copy forks on first write).
    function shuffle(xs)
        result = xs
        n = len(result)
        i = n - 1
        while i > 0
            j = random_int(0, i)
            tmp = result[i]
            result[i] = result[j]
            result[j] = tmp
            i = i - 1
        end while
        return result
    end function

    ' k items drawn WITHOUT replacement (0 <= k <= n); unknown otherwise.
    ' A partial Fisher-Yates over a copy of the data.
    function sample(xs, k)
        n = len(xs)
        if k < 0 then
            return unknown
        end if
        if k > n then
            return unknown
        end if
        pool = xs
        result = []
        i = 0
        while i < k
            j = random_int(i, n - 1)
            tmp = pool[i]
            pool[i] = pool[j]
            pool[j] = tmp
            append(result, pool[i])
            i = i + 1
        end while
        return result
    end function

    ' k items drawn WITH replacement (the bootstrap building block).
    function resample(xs, k)
        n = len(xs)
        if n = 0 then
            return unknown
        end if
        if k < 0 then
            return unknown
        end if
        result = []
        i = 0
        while i < k
            append(result, xs[random_int(0, n - 1)])
            i = i + 1
        end while
        return result
    end function

    ' Bootstrap: draw `b` resamples of size n with replacement, apply the
    ' `statistic` function to each, and return the list of b statistic values
    ' (feed that to mean/stdev/quantile for the estimate and its error/CI).
    ' `statistic` is a function value taking a list and returning a number. It
    ' must be a user-defined function (builtins like `mean` are not yet
    ' first-class values, so wrap them: `function avg(xs) return mean(xs) end`).
    function bootstrap(xs, statistic, b)
        n = len(xs)
        if n = 0 then
            return unknown
        end if
        if b < 0 then
            return unknown
        end if
        result = []
        i = 0
        while i < b
            append(result, statistic(resample(xs, n)))
            i = i + 1
        end while
        return result
    end function

    ' ======================================================================
    ' Phase 2 — Inferential statistics (statistics_design.md §8 Phase 2).
    ' Hypothesis tests, ANOVA, chi-squared, nonparametric rank tests,
    ' confidence intervals, effect sizes, multiple-comparison corrections,
    ' and GLM (logistic / Poisson) by iteratively reweighted least squares.
    ' All compositions in gBASIC over the Phase 1 foundation (distributions,
    ' ols, matrix toolkit). Every test returns a record (statistic, df,
    ' p_value, …) or `unknown` on malformed input — the same contract as ols.
    ' ======================================================================

    ' --- Confidence interval for a mean (Student's t) ---
    ' Returns the sample mean, the (lower, upper) bounds at the given
    ' confidence level (e.g. 0.95), the standard error, margin, and df.
    function confidence_interval(xs, level)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        if level <= 0 then
            return unknown
        end if
        if level >= 1 then
            return unknown
        end if
        xbar = mean(xs)
        se = stdev(xs) / sqrt(n)
        tcrit = t_quantile(1 - (1 - level) / 2, n - 1)
        margin = tcrit * se
        return { mean: xbar, lower: xbar - margin, upper: xbar + margin, se: se, margin: margin, df: n - 1, level: level }
    end function

    ' --- One-sample t-test: is mean(xs) different from mu0? ---
    function t_test_1sample(xs, mu0)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        xbar = mean(xs)
        se = stdev(xs) / sqrt(n)
        if se <= 0 then
            return unknown
        end if
        tstat = (xbar - mu0) / se
        dof = n - 1
        pv = 2 * (1 - t_cdf(abs(tstat), dof))
        return { statistic: tstat, df: dof, p_value: pv, mean: xbar, se: se, n: n }
    end function

    ' --- Two-sample t-test, pooled variance (assumes equal variances) ---
    function t_test_2sample(xs, ys)
        n1 = len(xs)
        n2 = len(ys)
        if n1 < 2 then
            return unknown
        end if
        if n2 < 2 then
            return unknown
        end if
        m1 = mean(xs)
        m2 = mean(ys)
        v1 = variance(xs)
        v2 = variance(ys)
        dof = n1 + n2 - 2
        sp2 = ((n1 - 1) * v1 + (n2 - 1) * v2) / dof
        se = sqrt(sp2 * (1 / n1 + 1 / n2))
        if se <= 0 then
            return unknown
        end if
        tstat = (m1 - m2) / se
        pv = 2 * (1 - t_cdf(abs(tstat), dof))
        return { statistic: tstat, df: dof, p_value: pv, mean1: m1, mean2: m2, se: se, n1: n1, n2: n2 }
    end function

    ' --- Welch's two-sample t-test (does NOT assume equal variances) ---
    ' df by the Welch-Satterthwaite approximation.
    function t_test_welch(xs, ys)
        n1 = len(xs)
        n2 = len(ys)
        if n1 < 2 then
            return unknown
        end if
        if n2 < 2 then
            return unknown
        end if
        m1 = mean(xs)
        m2 = mean(ys)
        v1 = variance(xs)
        v2 = variance(ys)
        a = v1 / n1
        b = v2 / n2
        se = sqrt(a + b)
        if se <= 0 then
            return unknown
        end if
        tstat = (m1 - m2) / se
        dof = (a + b) * (a + b) / (a * a / (n1 - 1) + b * b / (n2 - 1))
        pv = 2 * (1 - t_cdf(abs(tstat), dof))
        return { statistic: tstat, df: dof, p_value: pv, mean1: m1, mean2: m2, se: se, n1: n1, n2: n2 }
    end function

    ' --- Paired t-test: a one-sample t-test on the paired differences ---
    function t_test_paired(xs, ys)
        n = len(xs)
        if n != len(ys) then
            return unknown
        end if
        if n < 2 then
            return unknown
        end if
        diffs = []
        i = 0
        while i < n
            append(diffs, xs[i] - ys[i])
            i = i + 1
        end while
        res = t_test_1sample(diffs, 0)
        if is_unknown(res) then
            return unknown
        end if
        return { statistic: res.statistic, df: res.df, p_value: res.p_value, mean_diff: res.mean, se: res.se, n: n }
    end function

    ' --- Cohen's d effect size (pooled standard deviation) ---
    function cohens_d(xs, ys)
        n1 = len(xs)
        n2 = len(ys)
        if n1 < 2 then
            return unknown
        end if
        if n2 < 2 then
            return unknown
        end if
        sp = sqrt(((n1 - 1) * variance(xs) + (n2 - 1) * variance(ys)) / (n1 + n2 - 2))
        if sp <= 0 then
            return unknown
        end if
        return (mean(xs) - mean(ys)) / sp
    end function

    ' --- One-way ANOVA. `groups` is a list of lists (one per group). ---
    function anova_oneway(groups)
        k = len(groups)
        if k < 2 then
            return unknown
        end if
        ntot = 0
        grand = 0
        g = 0
        while g < k
            ng = len(groups[g])
            if ng < 1 then
                return unknown
            end if
            j = 0
            while j < ng
                grand = grand + groups[g][j]
                j = j + 1
            end while
            ntot = ntot + ng
            g = g + 1
        end while
        if ntot <= k then
            return unknown
        end if
        grand = grand / ntot
        ssb = 0
        ssw = 0
        g = 0
        while g < k
            ng = len(groups[g])
            gm = mean(groups[g])
            ssb = ssb + ng * (gm - grand) * (gm - grand)
            j = 0
            while j < ng
                d = groups[g][j] - gm
                ssw = ssw + d * d
                j = j + 1
            end while
            g = g + 1
        end while
        dfb = k - 1
        dfw = ntot - k
        msb = ssb / dfb
        msw = ssw / dfw
        if msw <= 0 then
            return unknown
        end if
        fstat = msb / msw
        pv = 1 - f_cdf(fstat, dfb, dfw)
        return { statistic: fstat, df_between: dfb, df_within: dfw, p_value: pv, ss_between: ssb, ss_within: ssw, ms_between: msb, ms_within: msw }
    end function

    ' --- Chi-squared goodness of fit. observed/expected are equal-length
    ' count lists. df = k - 1. ---
    function chi_square_gof(observed, expected)
        k = len(observed)
        if k < 2 then
            return unknown
        end if
        if len(expected) != k then
            return unknown
        end if
        stat = 0
        i = 0
        while i < k
            e = expected[i]
            if e <= 0 then
                return unknown
            end if
            d = observed[i] - e
            stat = stat + d * d / e
            i = i + 1
        end while
        dof = k - 1
        pv = 1 - chi2_cdf(stat, dof)
        return { statistic: stat, df: dof, p_value: pv }
    end function

    ' --- Chi-squared test of independence. `table` is a contingency table
    ' (list of rows, each a list of cell counts). Pearson's statistic with
    ' expected = row_total * col_total / grand_total; df = (r-1)(c-1). The
    ' record carries the expected-count table too. ---
    function chi_square_independence(table)
        r = len(table)
        if r < 2 then
            return unknown
        end if
        c = len(table[0])
        if c < 2 then
            return unknown
        end if
        rowsum = []
        colsum = []
        j = 0
        while j < c
            append(colsum, 0)
            j = j + 1
        end while
        grand = 0
        i = 0
        while i < r
            if len(table[i]) != c then
                return unknown
            end if
            rs = 0
            j = 0
            while j < c
                rs = rs + table[i][j]
                colsum[j] = colsum[j] + table[i][j]
                j = j + 1
            end while
            append(rowsum, rs)
            grand = grand + rs
            i = i + 1
        end while
        if grand <= 0 then
            return unknown
        end if
        stat = 0
        expected = []
        i = 0
        while i < r
            erow = []
            j = 0
            while j < c
                e = rowsum[i] * colsum[j] / grand
                append(erow, e)
                if e <= 0 then
                    return unknown
                end if
                d = table[i][j] - e
                stat = stat + d * d / e
                j = j + 1
            end while
            append(expected, erow)
            i = i + 1
        end while
        dof = (r - 1) * (c - 1)
        pv = 1 - chi2_cdf(stat, dof)
        return { statistic: stat, df: dof, p_value: pv, expected: expected }
    end function

    ' --- Rank utilities for the nonparametric tests ---

    ' Fractional ranks (1-based) with ties assigned their average rank.
    ' Returned aligned to the input order. Insertion sort on an index array
    ' (fine for the modest n the rank tests target).
    function _rank(xs)
        n = len(xs)
        idx = []
        i = 0
        while i < n
            append(idx, i)
            i = i + 1
        end while
        i = 1
        while i < n
            key = idx[i]
            j = i - 1
            while j >= 0 and xs[idx[j]] > xs[key]
                idx[j + 1] = idx[j]
                j = j - 1
            end while
            idx[j + 1] = key
            i = i + 1
        end while
        ranks = []
        i = 0
        while i < n
            append(ranks, 0)
            i = i + 1
        end while
        i = 0
        while i < n
            j = i
            while j + 1 < n and xs[idx[j + 1]] = xs[idx[i]]
                j = j + 1
            end while
            avg = ((i + 1) + (j + 1)) / 2
            p = i
            while p <= j
                ranks[idx[p]] = avg
                p = p + 1
            end while
            i = j + 1
        end while
        return ranks
    end function

    ' Tie term sum(t^3 - t) over tied groups of a value list (the standard
    ' correction factor for rank-test variances).
    function _tie_term(xs)
        s = sort(xs)
        n = len(s)
        total = 0
        i = 0
        while i < n
            j = i
            while j + 1 < n and s[j + 1] = s[i]
                j = j + 1
            end while
            t = j - i + 1
            total = total + (t * t * t - t)
            i = j + 1
        end while
        return total
    end function

    ' --- Mann-Whitney U test (two independent samples), normal approximation
    ' with tie correction (no continuity correction). U is reported for the
    ' FIRST sample. Matches scipy.stats.mannwhitneyu(use_continuity=False,
    ' method='asymptotic'). ---
    function mann_whitney(xs, ys)
        n1 = len(xs)
        n2 = len(ys)
        if n1 < 1 then
            return unknown
        end if
        if n2 < 1 then
            return unknown
        end if
        combined = []
        i = 0
        while i < n1
            append(combined, xs[i])
            i = i + 1
        end while
        i = 0
        while i < n2
            append(combined, ys[i])
            i = i + 1
        end while
        ranks = _rank(combined)
        r1 = 0
        i = 0
        while i < n1
            r1 = r1 + ranks[i]
            i = i + 1
        end while
        u1 = r1 - n1 * (n1 + 1) / 2
        u2 = n1 * n2 - u1
        nn = n1 + n2
        mu = n1 * n2 / 2
        tie = _tie_term(combined)
        sigma = sqrt((n1 * n2 / 12) * ((nn + 1) - tie / (nn * (nn - 1))))
        zstat = unknown
        pv = unknown
        if sigma > 0 then
            zstat = (u1 - mu) / sigma
            pv = 2 * (1 - _norm_cdf_std(abs(zstat)))
        end if
        return { u: u1, u1: u1, u2: u2, z: zstat, p_value: pv, n1: n1, n2: n2 }
    end function

    ' --- Wilcoxon signed-rank test (paired samples), normal approximation
    ' with tie correction, no continuity correction (zero differences
    ' dropped, the 'wilcox' convention). statistic is min(W+, W-). Matches
    ' scipy.stats.wilcoxon(correction=False, mode='approx'). ---
    function wilcoxon(xs, ys)
        n = len(xs)
        if n != len(ys) then
            return unknown
        end if
        absd = []
        sgn = []
        i = 0
        while i < n
            d = xs[i] - ys[i]
            if d != 0 then
                append(absd, abs(d))
                if d > 0 then
                    append(sgn, 1)
                else
                    append(sgn, -1)
                end if
            end if
            i = i + 1
        end while
        m = len(absd)
        if m < 1 then
            return unknown
        end if
        ranks = _rank(absd)
        wplus = 0
        wminus = 0
        i = 0
        while i < m
            if sgn[i] > 0 then
                wplus = wplus + ranks[i]
            else
                wminus = wminus + ranks[i]
            end if
            i = i + 1
        end while
        wstat = wplus
        if wminus < wplus then
            wstat = wminus
        end if
        mu = m * (m + 1) / 4
        tie = _tie_term(absd)
        sigma = sqrt(m * (m + 1) * (2 * m + 1) / 24 - tie / 48)
        zstat = unknown
        pv = unknown
        if sigma > 0 then
            zstat = (wplus - mu) / sigma
            pv = 2 * (1 - _norm_cdf_std(abs(zstat)))
        end if
        return { statistic: wstat, w_plus: wplus, w_minus: wminus, z: zstat, p_value: pv, n: m }
    end function

    ' --- Kruskal-Wallis H test (k independent groups), tie-corrected,
    ' chi-squared approximation on k-1 df. Matches scipy.stats.kruskal. ---
    function kruskal_wallis(groups)
        k = len(groups)
        if k < 2 then
            return unknown
        end if
        combined = []
        sizes = []
        g = 0
        while g < k
            ng = len(groups[g])
            if ng < 1 then
                return unknown
            end if
            append(sizes, ng)
            j = 0
            while j < ng
                append(combined, groups[g][j])
                j = j + 1
            end while
            g = g + 1
        end while
        nn = len(combined)
        ranks = _rank(combined)
        ssum = 0
        pos = 0
        g = 0
        while g < k
            ng = sizes[g]
            rsum = 0
            j = 0
            while j < ng
                rsum = rsum + ranks[pos]
                pos = pos + 1
                j = j + 1
            end while
            ssum = ssum + rsum * rsum / ng
            g = g + 1
        end while
        h = 12 / (nn * (nn + 1)) * ssum - 3 * (nn + 1)
        tie = _tie_term(combined)
        correction = 1 - tie / (nn * nn * nn - nn)
        if correction <= 0 then
            return unknown
        end if
        h = h / correction
        dof = k - 1
        pv = 1 - chi2_cdf(h, dof)
        return { statistic: h, df: dof, p_value: pv }
    end function

    ' --- Multiple-comparison corrections. Both take a list of raw p-values
    ' and return a list of adjusted p-values, aligned to the input order. ---

    ' Bonferroni: p_adj = min(p * m, 1).
    function bonferroni(pvals)
        m = len(pvals)
        out = []
        i = 0
        while i < m
            v = pvals[i] * m
            if v > 1 then
                v = 1
            end if
            append(out, v)
            i = i + 1
        end while
        return out
    end function

    ' Benjamini-Hochberg (FDR). Step-up: sort ascending, scale by m/rank,
    ' enforce monotonicity from the largest down, clamp to 1. Matches
    ' statsmodels multipletests(method='fdr_bh'). ---
    function benjamini_hochberg(pvals)
        m = len(pvals)
        if m = 0 then
            return []
        end if
        ' order indices by p ascending (insertion sort)
        idx = []
        i = 0
        while i < m
            append(idx, i)
            i = i + 1
        end while
        i = 1
        while i < m
            key = idx[i]
            j = i - 1
            while j >= 0 and pvals[idx[j]] > pvals[key]
                idx[j + 1] = idx[j]
                j = j - 1
            end while
            idx[j + 1] = key
            i = i + 1
        end while
        ' adjusted values in sorted order, then enforce monotone non-increasing
        ' running minimum from the top rank downward
        adj = []
        i = 0
        while i < m
            append(adj, 0)
            i = i + 1
        end while
        prev = 1
        rank = m
        while rank >= 1
            raw = pvals[idx[rank - 1]] * m / rank
            if raw > prev then
                raw = prev
            end if
            if raw > 1 then
                raw = 1
            end if
            adj[rank - 1] = raw
            prev = raw
            rank = rank - 1
        end while
        ' scatter back to original order
        out = []
        i = 0
        while i < m
            append(out, 0)
            i = i + 1
        end while
        i = 0
        while i < m
            out[idx[i]] = adj[i]
            i = i + 1
        end while
        return out
    end function

    ' --- GLM via iteratively reweighted least squares (IRLS) ---
    '
    ' Shared weighted-least-squares step: given the design matrix bigx, a
    ' per-observation weight list w, and a working response z, return the
    ' solution (X'WX)^-1 X'Wz together with the (X'WX)^-1 covariance basis,
    ' or `unknown` if the weighted normal matrix is singular.
    function _wls_step(bigx, w, z)
        n = len(bigx)
        wx = []
        wz = []
        i = 0
        while i < n
            row = []
            p = len(bigx[i])
            c = 0
            while c < p
                append(row, w[i] * bigx[i][c])
                c = c + 1
            end while
            append(wx, row)
            append(wz, w[i] * z[i])
            i = i + 1
        end while
        xt = mat_transpose(bigx)
        inv = mat_inverse(mat_mul(xt, wx))
        if is_unknown(inv) then
            return unknown
        end if
        beta = mat_vec(inv, mat_vec(xt, wz))
        return { beta: beta, cov: inv }
    end function

    ' Build the design matrix (leading intercept column of ones, then the
    ' predictor columns) shared by both GLMs. `cols` is a list of columns.
    function _design(cols, n)
        k = len(cols)
        bigx = []
        i = 0
        while i < n
            row = []
            append(row, 1)
            c = 0
            while c < k
                append(row, cols[c][i])
                c = c + 1
            end while
            append(bigx, row)
            i = i + 1
        end while
        return bigx
    end function

    ' Normalize predictors to a list-of-columns (a flat list becomes a single
    ' column), validating equal lengths; `unknown` if malformed.
    function _norm_cols(xs, n)
        cols = xs
        if len(xs) > 0 then
            if not is_array(xs[0]) then
                cols = [xs]
            end if
        end if
        k = len(cols)
        c = 0
        while c < k
            if len(cols[c]) != n then
                return unknown
            end if
            c = c + 1
        end while
        return cols
    end function

    ' Logistic regression (binary y in {0,1}) by IRLS, logit link. Returns
    ' coefficients (intercept first), std_errors, z_values, two-sided
    ' p_values (normal), fitted probabilities, log_likelihood, iterations,
    ' converged, n; `unknown` on malformed / singular input. Matches
    ' statsmodels GLM(family=Binomial).
    function logistic_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p then
            return unknown
        end if
        bigx = _design(cols, n)
        beta = []
        j = 0
        while j < p
            append(beta, 0)
            j = j + 1
        end while
        converged = false
        cov = unknown
        iter = 0
        while iter < 100
            w = []
            zw = []
            i = 0
            while i < n
                eta = 0
                j = 0
                while j < p
                    eta = eta + bigx[i][j] * beta[j]
                    j = j + 1
                end while
                mu = 1 / (1 + exp(0 - eta))
                wi = mu * (1 - mu)
                if wi < 0.0000000001 then
                    wi = 0.0000000001
                end if
                append(w, wi)
                append(zw, eta + (y[i] - mu) / wi)
                i = i + 1
            end while
            fit = _wls_step(bigx, w, zw)
            if is_unknown(fit) then
                return unknown
            end if
            nb = fit.beta
            cov = fit.cov
            delta = 0
            j = 0
            while j < p
                d = abs(nb[j] - beta[j])
                if d > delta then
                    delta = d
                end if
                j = j + 1
            end while
            beta = nb
            iter = iter + 1
            if delta < 0.0000000001 then
                converged = true
                break
            end if
        end while
        fitted = []
        loglik = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * beta[j]
                j = j + 1
            end while
            mu = 1 / (1 + exp(0 - eta))
            append(fitted, mu)
            mc = mu
            if mc < 0.0000000001 then
                mc = 0.0000000001
            end if
            if mc > 0.9999999999 then
                mc = 0.9999999999
            end if
            loglik = loglik + y[i] * log(mc) + (1 - y[i]) * log(1 - mc)
            i = i + 1
        end while
        return _glm_result(beta, cov, fitted, loglik, iter, converged, n, p, _bern_null_ll(y), "logit")
    end function

    ' Poisson regression (count y >= 0) by IRLS, log link. Same result shape
    ' as logistic_regression. Matches statsmodels GLM(family=Poisson).
    function poisson_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p then
            return unknown
        end if
        bigx = _design(cols, n)
        beta = []
        j = 0
        while j < p
            append(beta, 0)
            j = j + 1
        end while
        converged = false
        cov = unknown
        iter = 0
        while iter < 100
            w = []
            zw = []
            i = 0
            while i < n
                eta = 0
                j = 0
                while j < p
                    eta = eta + bigx[i][j] * beta[j]
                    j = j + 1
                end while
                mu = exp(eta)
                wi = mu
                if wi < 0.0000000001 then
                    wi = 0.0000000001
                end if
                append(w, wi)
                append(zw, eta + (y[i] - mu) / wi)
                i = i + 1
            end while
            fit = _wls_step(bigx, w, zw)
            if is_unknown(fit) then
                return unknown
            end if
            nb = fit.beta
            cov = fit.cov
            delta = 0
            j = 0
            while j < p
                d = abs(nb[j] - beta[j])
                if d > delta then
                    delta = d
                end if
                j = j + 1
            end while
            beta = nb
            iter = iter + 1
            if delta < 0.0000000001 then
                converged = true
                break
            end if
        end while
        fitted = []
        loglik = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * beta[j]
                j = j + 1
            end while
            mu = exp(eta)
            append(fitted, mu)
            loglik = loglik + y[i] * log(mu) - mu - lgamma(y[i] + 1)
            i = i + 1
        end while
        return _glm_result(beta, cov, fitted, loglik, iter, converged, n, p, _pois_null_ll(y), "log")
    end function

    ' Intercept-only (null) log-likelihood for a Bernoulli outcome — used for
    ' McFadden's pseudo-R^2 in logistic / probit.
    function _bern_null_ll(y)
        n = len(y)
        s = sum(y)
        pbar = s / n
        if pbar <= 0 then
            return 0
        end if
        if pbar >= 1 then
            return 0
        end if
        return s * log(pbar) + (n - s) * log(1 - pbar)
    end function

    ' Intercept-only (null) log-likelihood for a Poisson count outcome.
    function _pois_null_ll(y)
        n = len(y)
        m = mean(y)
        s = 0
        i = 0
        while i < n
            s = s + y[i] * log(m) - m - lgamma(y[i] + 1)
            i = i + 1
        end while
        return s
    end function

    ' Intercept-only (null) log-likelihood for a K-category outcome (the
    ' marginal-frequency model) — used by ordinal / multinomial pseudo-R^2.
    function _cat_null_ll(y, k)
        n = len(y)
        counts = []
        c = 0
        while c < k
            append(counts, 0)
            c = c + 1
        end while
        i = 0
        while i < n
            counts[y[i]] = counts[y[i]] + 1
            i = i + 1
        end while
        s = 0
        c = 0
        while c < k
            if counts[c] > 0 then
                s = s + counts[c] * log(counts[c] / n)
            end if
            c = c + 1
        end while
        return s
    end function

    ' Shared GLM result assembler: turn coefficients + covariance into the
    ' standard-error / z / p-value table and the result record. null_ll is the
    ' intercept-only log-likelihood (for McFadden's pseudo-R^2).
    function _glm_result(beta, cov, fitted, loglik, iter, converged, n, p, null_ll, link)
        ses = []
        zvals = []
        pvals = []
        j = 0
        while j < p
            v = cov[j][j]
            se = unknown
            zv = unknown
            pv = unknown
            if v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    zv = beta[j] / se
                    pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            j = j + 1
        end while
        aic = 2 * p - 2 * loglik
        bic = log(n) * p - 2 * loglik
        pseudo = unknown
        if null_ll != 0 then
            pseudo = 1 - loglik / null_ll
        end if
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, fitted: fitted, log_likelihood: loglik, null_log_likelihood: null_ll, pseudo_r2: pseudo, aic: aic, bic: bic, iterations: iter, converged: converged, n: n, df_resid: n - p, cov: cov, link: link }
    end function

    ' Wald confidence intervals for a fitted model's coefficients (flat
    ' coefficient list). Uses the t-distribution for OLS (models carrying
    ' t_values + df) and the normal otherwise. level is e.g. 0.95. Returns a
    ' list of {low, high}, or unknown (including for multinomial's nested
    ' coefficients).
    function conf_int(model, level)
        ks = keys(model)
        if not contains(ks, "coefficients") then
            return unknown
        end if
        coefs = model.coefficients
        if len(coefs) > 0 then
            if is_array(coefs[0]) then
                return unknown
            end if
        end if
        ses = model.std_errors
        a = 1 - level
        crit = _inv_norm_std(1 - a / 2)
        if contains(ks, "t_values") then
            crit = t_quantile(1 - a / 2, model.df)
        end if
        out = []
        j = 0
        while j < len(coefs)
            se = ses[j]
            lo = unknown
            hi = unknown
            if not is_unknown(se) then
                lo = coefs[j] - crit * se
                hi = coefs[j] + crit * se
            end if
            append(out, { low: lo, high: hi })
            j = j + 1
        end while
        return out
    end function

    ' Exponentiated coefficients with confidence intervals: odds ratios for
    ' logistic, incidence-rate ratios for Poisson / negative binomial. Returns
    ' a list of {odds_ratio, ci_low, ci_high}, or unknown.
    function odds_ratios(model, level)
        ci = conf_int(model, level)
        if is_unknown(ci) then
            return unknown
        end if
        coefs = model.coefficients
        out = []
        j = 0
        while j < len(coefs)
            lo = unknown
            hi = unknown
            if not is_unknown(ci[j].low) then
                lo = exp(ci[j].low)
                hi = exp(ci[j].high)
            end if
            append(out, { odds_ratio: exp(coefs[j]), ci_low: lo, ci_high: hi })
            j = j + 1
        end while
        return out
    end function

    ' Treatment (dummy) coding of a categorical predictor. labels is a list of
    ' category values (numbers or strings). Levels are sorted; the first is the
    ' reference and is dropped, giving K-1 indicator columns (matching
    ' statsmodels / patsy C(x, Treatment)). Returns {columns (list of K-1
    ' columns), levels, reference}, ready to splice into a regression's
    ' predictor list. Returns unknown for fewer than two levels.
    function dummy_code(labels)
        n = len(labels)
        if n = 0 then
            return unknown
        end if
        levels = []
        i = 0
        while i < n
            if not contains(levels, labels[i]) then
                append(levels, labels[i])
            end if
            i = i + 1
        end while
        levels = sort(levels)
        k = len(levels)
        if k < 2 then
            return unknown
        end if
        cols = []
        m = 1
        while m < k
            col = []
            i = 0
            while i < n
                v = 0
                if labels[i] = levels[m] then
                    v = 1
                end if
                append(col, v)
                i = i + 1
            end while
            append(cols, col)
            m = m + 1
        end while
        return { columns: cols, levels: levels, reference: levels[0] }
    end function

    ' Elementwise product of two equal-length predictor columns, for building an
    ' interaction term. Returns the product column or unknown on a length
    ' mismatch.
    function interaction(a, b)
        n = len(a)
        if len(b) != n then
            return unknown
        end if
        out = []
        i = 0
        while i < n
            append(out, a[i] * b[i])
            i = i + 1
        end while
        return out
    end function

    ' OLS with heteroskedasticity-consistent (robust sandwich) standard errors.
    ' hc selects the variant: "HC0" (White), "HC1" (n/(n-p) correction, Stata
    ' default), "HC2" (leverage-adjusted), or "HC3" (small-sample, recommended).
    ' cov = (X'X)^-1 [Σ ω_i x_i x_i'] (X'X)^-1 with ω_i = e_i^2 scaled per
    ' variant; z / p-values use the normal distribution (matching statsmodels
    ' cov_type='HC*'). Returns {coefficients, std_errors, z_values, p_values,
    ' cov_type, fitted, residuals, n, df} or unknown.
    function ols_robust(y, xs, hc)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p then
            return unknown
        end if
        bigx = _design(cols, n)
        xt = mat_transpose(bigx)
        xtxinv = mat_inverse(mat_mul(xt, bigx))
        if is_unknown(xtxinv) then
            return unknown
        end if
        beta = mat_vec(xtxinv, mat_vec(xt, y))
        fitted = mat_vec(bigx, beta)
        e = []
        i = 0
        while i < n
            append(e, y[i] - fitted[i])
            i = i + 1
        end while
        omega = []
        i = 0
        while i < n
            ei2 = e[i] * e[i]
            om = ei2
            if hc = "HC1" then
                om = ei2 * n / (n - p)
            end if
            if hc = "HC2" then
                v = mat_vec(xtxinv, bigx[i])
                hi = 0
                a = 0
                while a < p
                    hi = hi + bigx[i][a] * v[a]
                    a = a + 1
                end while
                om = ei2 / (1 - hi)
            end if
            if hc = "HC3" then
                v = mat_vec(xtxinv, bigx[i])
                hi = 0
                a = 0
                while a < p
                    hi = hi + bigx[i][a] * v[a]
                    a = a + 1
                end while
                om = ei2 / ((1 - hi) * (1 - hi))
            end if
            append(omega, om)
            i = i + 1
        end while
        meat = []
        a = 0
        while a < p
            row = []
            b = 0
            while b < p
                append(row, 0)
                b = b + 1
            end while
            append(meat, row)
            a = a + 1
        end while
        i = 0
        while i < n
            a = 0
            while a < p
                b = 0
                while b < p
                    meat[a][b] = meat[a][b] + omega[i] * bigx[i][a] * bigx[i][b]
                    b = b + 1
                end while
                a = a + 1
            end while
            i = i + 1
        end while
        cov = mat_mul(mat_mul(xtxinv, meat), xtxinv)
        ses = []
        zvals = []
        pvals = []
        j = 0
        while j < p
            v = cov[j][j]
            se = unknown
            zv = unknown
            pv = unknown
            if v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    zv = beta[j] / se
                    pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            j = j + 1
        end while
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, cov_type: hc, fitted: fitted, residuals: e, n: n, df: n - p }
    end function

    ' Average marginal effects (per slope) for a beta vector over design bigx
    ' (with intercept). AME_j = beta_j · mean_i f'(eta_i), where f' is the
    ' logistic-variance p(1-p) for the logit link or the normal pdf for probit.
    ' Returns a length-(p-1) list (intercept excluded).
    function _ame_vec(beta, bigx, link)
        n = len(bigx)
        p = len(beta)
        s = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * beta[j]
                j = j + 1
            end while
            if link = "probit" then
                d = exp(0 - eta * eta / 2) / _sqrt2pi()
            else
                pmu = 1 / (1 + exp(0 - eta))
                d = pmu * (1 - pmu)
            end if
            s = s + d
            i = i + 1
        end while
        factor = s / n
        out = []
        j = 1
        while j < p
            append(out, beta[j] * factor)
            j = j + 1
        end while
        return out
    end function

    ' Average marginal effects for a fitted logistic or probit model. `model` is
    ' the result of logistic_regression / probit_regression (carrying its coef
    ' covariance and link); `xs` is the same predictor set used to fit it.
    ' Returns {effects, std_errors, z_values, p_values} — one entry per predictor
    ' (the intercept is excluded) — with delta-method standard errors. Matches
    ' statsmodels get_margeff(at='overall'). Returns unknown on bad input.
    function marginal_effects(model, xs)
        ks = keys(model)
        if not contains(ks, "link") then
            return unknown
        end if
        if not contains(ks, "cov") then
            return unknown
        end if
        link = model.link
        if link != "logit" then
            if link != "probit" then
                return unknown
            end if
        end if
        beta = model.coefficients
        cov = model.cov
        p = len(beta)
        n = model.n
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        bigx = _design(cols, n)
        effects = _ame_vec(beta, bigx, link)
        q = p - 1
        gmat = []
        r = 0
        while r < q
            row = []
            c = 0
            while c < p
                append(row, 0)
                c = c + 1
            end while
            append(gmat, row)
            r = r + 1
        end while
        c = 0
        while c < p
            h = 0.00001 * (abs(beta[c]) + 1)
            bp = beta
            bp[c] = bp[c] + h
            bm = beta
            bm[c] = bm[c] - h
            ap = _ame_vec(bp, bigx, link)
            am = _ame_vec(bm, bigx, link)
            r = 0
            while r < q
                gmat[r][c] = (ap[r] - am[r]) / (2 * h)
                r = r + 1
            end while
            c = c + 1
        end while
        covame = mat_mul(mat_mul(gmat, cov), mat_transpose(gmat))
        ses = []
        zvals = []
        pvals = []
        r = 0
        while r < q
            v = covame[r][r]
            se = unknown
            zv = unknown
            pv = unknown
            if v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    zv = effects[r] / se
                    pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            r = r + 1
        end while
        return { effects: effects, std_errors: ses, z_values: zvals, p_values: pvals }
    end function

    ' Moderation via simple slopes. Fits Y = b0 + bX·x + bW·w + bXW·(x·w) and
    ' reports the conditional effect of x (slope = bX + bXW·w) at each value in
    ' wvals, with its standard error, t, and p from the coefficient covariance:
    ' Var = Var(bX) + w^2 Var(bXW) + 2w Cov(bX, bXW). Returns {interaction_coef,
    ' interaction_p, slopes: [{w, slope, se, t, p_value}...]} or unknown. Feed
    ' e.g. [mean(w)-stdev(w), mean(w), mean(w)+stdev(w)] for the ±1 SD probe.
    function simple_slopes(y, x, w, wvals)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        xw = interaction(x, w)
        m = ols(y, [x, w, xw])
        if is_unknown(m) then
            return unknown
        end if
        cov = m.cov
        bx = m.coefficients[1]
        bxw = m.coefficients[3]
        df = m.df
        out = []
        i = 0
        while i < len(wvals)
            wv = wvals[i]
            slope = bx + bxw * wv
            var = cov[1][1] + wv * wv * cov[3][3] + 2 * wv * cov[1][3]
            se = unknown
            tv = unknown
            pv = unknown
            if var >= 0 then
                se = sqrt(var)
                if se > 0 then
                    tv = slope / se
                    pv = 2 * (1 - t_cdf(abs(tv), df))
                end if
            end if
            append(out, { w: wv, slope: slope, se: se, t: tv, p_value: pv })
            i = i + 1
        end while
        return { interaction_coef: bxw, interaction_p: m.p_values[3], slopes: out }
    end function

    ' Simple mediation (X -> M -> Y). Estimates the a path (M on X), the b and
    ' direct c' paths (Y on X and M), and the total c path (Y on X), all by OLS,
    ' then the indirect effect a·b with a nonparametric (percentile) bootstrap
    ' confidence interval over nboot resamples — the Preacher & Hayes approach.
    ' Call seed() beforehand for a reproducible CI. Returns {a, b, c_total,
    ' c_direct, indirect, prop_mediated, boot_low, boot_high, nboot} or unknown.
    function mediation(y, x, m, nboot)
        n = len(y)
        if n < 3 then
            return unknown
        end if
        if len(x) != n then
            return unknown
        end if
        if len(m) != n then
            return unknown
        end if
        full = _med_effects(y, x, m, n)
        a = full.a
        b = full.b
        cd = full.cprime
        ct = full.c
        indirect = a * b
        prop = unknown
        if ct != 0 then
            prop = indirect / ct
        end if
        boots = []
        bi = 0
        while bi < nboot
            rx = []
            rm = []
            ry = []
            i = 0
            while i < n
                idx = random_int(0, n - 1)
                append(rx, x[idx])
                append(rm, m[idx])
                append(ry, y[idx])
                i = i + 1
            end while
            eff = _med_effects(ry, rx, rm, n)
            if not is_unknown(eff) then
                append(boots, eff.a * eff.b)
            end if
            bi = bi + 1
        end while
        boots = sort(boots)
        nb = len(boots)
        blo = unknown
        bhi = unknown
        if nb > 0 then
            lo_idx = floor(0.025 * (nb - 1))
            hi_idx = floor(0.975 * (nb - 1))
            blo = boots[lo_idx]
            bhi = boots[hi_idx]
        end if
        return { a: a, b: b, c_total: ct, c_direct: cd, indirect: indirect, prop_mediated: prop, boot_low: blo, boot_high: bhi, nboot: nboot }
    end function

    ' Closed-form OLS paths for one mediation (re)sample: a = slope of M on X,
    ' b = partial slope of M in Y~X+M, cprime = partial slope of X there, c =
    ' total slope of Y on X. Returns {a, b, cprime, c} or unknown if degenerate.
    function _med_effects(y, x, m, n)
        sx = 0
        sm = 0
        sy = 0
        i = 0
        while i < n
            sx = sx + x[i]
            sm = sm + m[i]
            sy = sy + y[i]
            i = i + 1
        end while
        mx = sx / n
        mm = sm / n
        my = sy / n
        sxx = 0
        smm = 0
        sxm = 0
        sxy = 0
        smy = 0
        i = 0
        while i < n
            dx = x[i] - mx
            dm = m[i] - mm
            dy = y[i] - my
            sxx = sxx + dx * dx
            smm = smm + dm * dm
            sxm = sxm + dx * dm
            sxy = sxy + dx * dy
            smy = smy + dm * dy
            i = i + 1
        end while
        denom = sxx * smm - sxm * sxm
        if sxx = 0 then
            return unknown
        end if
        if denom = 0 then
            return unknown
        end if
        a = sxm / sxx
        bm = (sxx * smy - sxm * sxy) / denom
        bx = (smm * sxy - sxm * smy) / denom
        c = sxy / sxx
        return { a: a, b: bm, cprime: bx, c: c }
    end function

    ' Probit regression (binary y in {0,1}) by IRLS with the normal-CDF link.
    ' Same result shape as logistic_regression. Matches statsmodels Probit /
    ' GLM(family=Binomial, link=probit).
    function probit_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p then
            return unknown
        end if
        bigx = _design(cols, n)
        beta = []
        j = 0
        while j < p
            append(beta, 0)
            j = j + 1
        end while
        converged = false
        cov = unknown
        iter = 0
        while iter < 100
            w = []
            zw = []
            i = 0
            while i < n
                eta = 0
                j = 0
                while j < p
                    eta = eta + bigx[i][j] * beta[j]
                    j = j + 1
                end while
                mu = _norm_cdf_std(eta)
                if mu < 0.000000000001 then
                    mu = 0.000000000001
                end if
                if mu > 0.999999999999 then
                    mu = 0.999999999999
                end if
                dens = exp(0 - eta * eta / 2) / _sqrt2pi()
                if dens < 0.0000000001 then
                    dens = 0.0000000001
                end if
                wi = dens * dens / (mu * (1 - mu))
                append(w, wi)
                append(zw, eta + (y[i] - mu) / dens)
                i = i + 1
            end while
            fit = _wls_step(bigx, w, zw)
            if is_unknown(fit) then
                return unknown
            end if
            nb = fit.beta
            cov = fit.cov
            delta = 0
            j = 0
            while j < p
                d = abs(nb[j] - beta[j])
                if d > delta then
                    delta = d
                end if
                j = j + 1
            end while
            beta = nb
            iter = iter + 1
            if delta < 0.0000000001 then
                converged = true
                break
            end if
        end while
        fitted = []
        loglik = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * beta[j]
                j = j + 1
            end while
            mu = _norm_cdf_std(eta)
            append(fitted, mu)
            mc = mu
            if mc < 0.000000000001 then
                mc = 0.000000000001
            end if
            if mc > 0.999999999999 then
                mc = 0.999999999999
            end if
            loglik = loglik + y[i] * log(mc) + (1 - y[i]) * log(1 - mc)
            i = i + 1
        end while
        return _glm_result(beta, cov, fitted, loglik, iter, converged, n, p, _bern_null_ll(y), "probit")
    end function

    ' Numerical parameter covariance for an MLE: the inverse of the observed
    ' information (central-difference Hessian of the negative-log-likelihood
    ' objective at `params`). Shared by the optimizer-based GLM families
    ' (negative binomial, ordinal, multinomial). Returns unknown if singular.
    function _mle_cov(objective, params, ctx)
        m = len(params)
        hh = []
        i = 0
        while i < m
            append(hh, 0.0001 * (abs(params[i]) + 1))
            i = i + 1
        end while
        hess = []
        i = 0
        while i < m
            row = []
            j = 0
            while j < m
                append(row, 0)
                j = j + 1
            end while
            append(hess, row)
            i = i + 1
        end while
        i = 0
        while i < m
            j = i
            while j < m
                pp = params
                pp[i] = pp[i] + hh[i]
                pp[j] = pp[j] + hh[j]
                fpp = objective(pp, ctx)
                pm = params
                pm[i] = pm[i] + hh[i]
                pm[j] = pm[j] - hh[j]
                fpm = objective(pm, ctx)
                mp = params
                mp[i] = mp[i] - hh[i]
                mp[j] = mp[j] + hh[j]
                fmp = objective(mp, ctx)
                mn = params
                mn[i] = mn[i] - hh[i]
                mn[j] = mn[j] - hh[j]
                fmn = objective(mn, ctx)
                val = (fpp - fpm - fmp + fmn) / (4 * hh[i] * hh[j])
                hess[i][j] = val
                hess[j][i] = val
                j = j + 1
            end while
            i = i + 1
        end while
        return mat_inverse(hess)
    end function

    ' Negative NB2 log-likelihood. ctx = {y, bigx, p}; params = [beta..., ln_alpha].
    function _nb_negll(params, ctx)
        y = ctx.y
        bigx = ctx.bigx
        p = ctx.p
        n = len(y)
        alpha = exp(params[p])
        r = 1 / alpha
        ll = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * params[j]
                j = j + 1
            end while
            if eta > 30 then
                return pow(10, 12)
            end if
            mu = exp(eta)
            ll = ll + lgamma(y[i] + r) - lgamma(r) - lgamma(y[i] + 1) + r * log(r / (r + mu)) + y[i] * log(mu / (r + mu))
            i = i + 1
        end while
        return 0 - ll
    end function

    ' Negative binomial (NB2) regression for overdispersed counts, log link,
    ' with the dispersion `alpha` estimated jointly by maximum likelihood (via
    ' optimize, started from the Poisson fit). Returns {coefficients,
    ' std_errors, z_values, p_values, alpha, log_likelihood, aic, bic, n,
    ' converged} or unknown. Matches statsmodels NegativeBinomial (NB2).
    function negbinom_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p + 1 then
            return unknown
        end if
        bigx = _design(cols, n)
        init = []
        pois = poisson_regression(y, xs)
        if is_unknown(pois) then
            j = 0
            while j < p
                append(init, 0)
                j = j + 1
            end while
        else
            j = 0
            while j < p
                append(init, pois.coefficients[j])
                j = j + 1
            end while
        end if
        append(init, log(0.5))
        ctx = { y: y, bigx: bigx, p: p }
        res = optimize(_nb_negll, init, { max_iter: 8000, tol: pow(10, -10) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        prm = res.params
        loglik = 0 - res.value
        cov = _mle_cov(_nb_negll, prm, ctx)
        beta = []
        ses = []
        zvals = []
        pvals = []
        j = 0
        while j < p
            append(beta, prm[j])
            se = unknown
            zv = unknown
            pv = unknown
            if not is_unknown(cov) then
                v = cov[j][j]
                if v >= 0 then
                    se = sqrt(v)
                    if se > 0 then
                        zv = prm[j] / se
                        pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                    end if
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            j = j + 1
        end while
        kpar = p + 1
        aic = 2 * kpar - 2 * loglik
        bic = log(n) * kpar - 2 * loglik
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, alpha: exp(prm[p]), log_likelihood: loglik, aic: aic, bic: bic, n: n, converged: res.converged }
    end function

    ' Negative log-likelihood of the proportional-odds ordinal logit model.
    ' ctx = {y, bigx, p, kcat}; bigx is the n×p predictor matrix (no intercept).
    ' params = [slopes (p), cut_0, delta_1..delta_{K-2}] where cut_m = cut_{m-1}
    ' + exp(delta_m) keeps the cutpoints strictly increasing.
    function _ord_negll(params, ctx)
        y = ctx.y
        bigx = ctx.bigx
        p = ctx.p
        kcat = ctx.kcat
        n = len(y)
        nc = kcat - 1
        cuts = []
        append(cuts, params[p])
        m = 1
        while m < nc
            append(cuts, cuts[m - 1] + exp(params[p + m]))
            m = m + 1
        end while
        ll = 0
        i = 0
        while i < n
            eta = 0
            j = 0
            while j < p
                eta = eta + bigx[i][j] * params[j]
                j = j + 1
            end while
            yc = y[i]
            if yc = 0 then
                pr = 1 / (1 + exp(0 - (cuts[0] - eta)))
            else
                if yc = nc then
                    pr = 1 - 1 / (1 + exp(0 - (cuts[nc - 1] - eta)))
                else
                    pr = 1 / (1 + exp(0 - (cuts[yc] - eta))) - 1 / (1 + exp(0 - (cuts[yc - 1] - eta)))
                end if
            end if
            if pr < 0.000000000001 then
                pr = 0.000000000001
            end if
            ll = ll + log(pr)
            i = i + 1
        end while
        return 0 - ll
    end function

    ' Ordinal logistic regression (proportional-odds model) for an ordered
    ' categorical outcome y in {0, 1, ..., K-1}. Estimated by maximum likelihood
    ' via optimize. Returns {coefficients (slopes), std_errors, z_values,
    ' p_values, cutpoints, log_likelihood, aic, bic, n, converged} or unknown.
    ' Matches statsmodels OrderedModel(distr='logit').
    function ordinal_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols)
        if p < 1 then
            return unknown
        end if
        kcat = max(y) + 1
        if kcat < 2 then
            return unknown
        end if
        nc = kcat - 1
        bigx = []
        i = 0
        while i < n
            row = []
            c = 0
            while c < p
                append(row, cols[c][i])
                c = c + 1
            end while
            append(bigx, row)
            i = i + 1
        end while
        ' initialize slopes at 0 and cutpoints from empirical cumulative logits
        counts = []
        k = 0
        while k < kcat
            append(counts, 0)
            k = k + 1
        end while
        i = 0
        while i < n
            counts[y[i]] = counts[y[i]] + 1
            i = i + 1
        end while
        cum = 0
        rawcuts = []
        k = 0
        while k < nc
            cum = cum + counts[k]
            pcum = cum / n
            if pcum < 0.01 then
                pcum = 0.01
            end if
            if pcum > 0.99 then
                pcum = 0.99
            end if
            append(rawcuts, log(pcum / (1 - pcum)))
            k = k + 1
        end while
        init = []
        j = 0
        while j < p
            append(init, 0)
            j = j + 1
        end while
        append(init, rawcuts[0])
        m = 1
        while m < nc
            append(init, log(rawcuts[m] - rawcuts[m - 1]))
            m = m + 1
        end while
        ctx = { y: y, bigx: bigx, p: p, kcat: kcat }
        res = optimize(_ord_negll, init, { max_iter: 10000, tol: pow(10, -10) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        prm = res.params
        loglik = 0 - res.value
        cov = _mle_cov(_ord_negll, prm, ctx)
        beta = []
        ses = []
        zvals = []
        pvals = []
        j = 0
        while j < p
            append(beta, prm[j])
            se = unknown
            zv = unknown
            pv = unknown
            if not is_unknown(cov) then
                v = cov[j][j]
                if v >= 0 then
                    se = sqrt(v)
                    if se > 0 then
                        zv = prm[j] / se
                        pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                    end if
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            j = j + 1
        end while
        cuts = []
        append(cuts, prm[p])
        m = 1
        while m < nc
            append(cuts, cuts[m - 1] + exp(prm[p + m]))
            m = m + 1
        end while
        kpar = p + nc
        aic = 2 * kpar - 2 * loglik
        bic = log(n) * kpar - 2 * loglik
        nullll = _cat_null_ll(y, kcat)
        pseudo = unknown
        if nullll != 0 then
            pseudo = 1 - loglik / nullll
        end if
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, cutpoints: cuts, log_likelihood: loglik, null_log_likelihood: nullll, pseudo_r2: pseudo, aic: aic, bic: bic, n: n, converged: res.converged }
    end function

    ' Negative log-likelihood of the baseline-category multinomial logit
    ' (reference category 0). ctx = {y, bigx, p, kcat}; bigx is n×p with an
    ' intercept column; params is the flattened (K-1)×p coefficient block
    ' (category c > 0 occupies params[(c-1)*p .. (c-1)*p + p - 1]).
    function _mnl_negll(params, ctx)
        y = ctx.y
        bigx = ctx.bigx
        p = ctx.p
        kcat = ctx.kcat
        n = len(y)
        ll = 0
        i = 0
        while i < n
            denom = 1
            etas = []
            c = 1
            while c < kcat
                eta = 0
                j = 0
                while j < p
                    eta = eta + bigx[i][j] * params[(c - 1) * p + j]
                    j = j + 1
                end while
                if eta > 30 then
                    return pow(10, 12)
                end if
                append(etas, eta)
                denom = denom + exp(eta)
                c = c + 1
            end while
            num = 0
            if y[i] > 0 then
                num = etas[y[i] - 1]
            end if
            ll = ll + num - log(denom)
            i = i + 1
        end while
        return 0 - ll
    end function

    ' Multinomial (baseline-category) logistic regression for a nominal outcome
    ' y in {0, 1, ..., K-1} with 0 as the reference. Estimated by maximum
    ' likelihood via optimize. Returns {coefficients, std_errors, z_values,
    ' p_values, log_likelihood, aic, bic, n, converged}, where coefficients and
    ' the error/z/p tables are each a list of K-1 blocks (one per non-reference
    ' category, each a length-p list: intercept then predictors). Matches
    ' statsmodels MNLogit.
    function multinomial_regression(y, xs)
        n = len(y)
        if n = 0 then
            return unknown
        end if
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        kcat = max(y) + 1
        if kcat < 3 then
            return unknown
        end if
        if n <= (kcat - 1) * p then
            return unknown
        end if
        bigx = _design(cols, n)
        m = (kcat - 1) * p
        init = []
        i = 0
        while i < m
            append(init, 0)
            i = i + 1
        end while
        ctx = { y: y, bigx: bigx, p: p, kcat: kcat }
        res = optimize(_mnl_negll, init, { max_iter: 20000, tol: pow(10, -10) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        prm = res.params
        loglik = 0 - res.value
        cov = _mle_cov(_mnl_negll, prm, ctx)
        coefs = []
        allse = []
        allz = []
        allp = []
        c = 0
        while c < kcat - 1
            brow = []
            srow = []
            zrow = []
            prow = []
            j = 0
            while j < p
                idx = c * p + j
                append(brow, prm[idx])
                se = unknown
                zv = unknown
                pv = unknown
                if not is_unknown(cov) then
                    v = cov[idx][idx]
                    if v >= 0 then
                        se = sqrt(v)
                        if se > 0 then
                            zv = prm[idx] / se
                            pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                        end if
                    end if
                end if
                append(srow, se)
                append(zrow, zv)
                append(prow, pv)
                j = j + 1
            end while
            append(coefs, brow)
            append(allse, srow)
            append(allz, zrow)
            append(allp, prow)
            c = c + 1
        end while
        aic = 2 * m - 2 * loglik
        bic = log(n) * m - 2 * loglik
        nullll = _cat_null_ll(y, kcat)
        pseudo = unknown
        if nullll != 0 then
            pseudo = 1 - loglik / nullll
        end if
        return { coefficients: coefs, std_errors: allse, z_values: allz, p_values: allp, log_likelihood: loglik, null_log_likelihood: nullll, pseudo_r2: pseudo, aic: aic, bic: bic, n: n, converged: res.converged }
    end function

    ' ======================================================================
    ' Phase 3 — Multivariate / unsupervised (statistics_design.md §8 Phase 3).
    ' Segmentation (kmeans, hierarchical), dimensionality reduction (pca over a
    ' pure-gBASIC Jacobi eigensolver), and anomaly detection (zscore_outliers,
    ' iqr_outliers). Data here is a *list of points* — each point a list of
    ' feature values (row-major), matching the frame.bas to_rows shape. PCA
    ' holds the §6 "earn it" line: the symmetric eigensolver stays in gBASIC
    ' until profiling proves it must move to C. Verified against scipy /
    ' scikit-learn. Each returns a record, or `unknown` on malformed input.
    ' ======================================================================

    ' --- Anomaly detection (1-D) ---

    ' Standard-score outliers: flag values whose |z| exceeds `threshold`
    ' (a common choice is 3). Uses the population stdev (ddof=0), matching
    ' scipy.stats.zscore's default. Returns the flagged indices/values, the
    ' full score vector, and the mean/stdev used.
    function zscore_outliers(xs, threshold)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        if threshold <= 0 then
            return unknown
        end if
        m = mean(xs)
        sd = pstdev(xs)
        indices = []
        values = []
        scores = []
        i = 0
        while i < n
            z = 0
            if sd > 0 then
                z = (xs[i] - m) / sd
            end if
            append(scores, z)
            if abs(z) > threshold then
                append(indices, i)
                append(values, xs[i])
            end if
            i = i + 1
        end while
        return { indices: indices, values: values, scores: scores, mean: m, stdev: sd, threshold: threshold }
    end function

    ' Tukey-fence outliers: flag values outside [Q1 - k*IQR, Q3 + k*IQR].
    ' k = 1.5 is the classic "outlier" fence, 3.0 the "far out" fence. The
    ' quartiles use the same linear-interpolation `quantile` as numpy.
    function iqr_outliers(xs, k)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        if k < 0 then
            return unknown
        end if
        q1 = quantile(xs, 0.25)
        q3 = quantile(xs, 0.75)
        spread = q3 - q1
        lo = q1 - k * spread
        hi = q3 + k * spread
        indices = []
        values = []
        i = 0
        while i < n
            if xs[i] < lo or xs[i] > hi then
                append(indices, i)
                append(values, xs[i])
            end if
            i = i + 1
        end while
        return { indices: indices, values: values, lower: lo, upper: hi, q1: q1, q3: q3, iqr: spread, k: k }
    end function

    ' --- Shared geometry helpers ---

    ' Squared Euclidean distance between two equal-length points.
    function _dist2(a, b)
        s = 0
        i = 0
        m = len(a)
        while i < m
            d = a[i] - b[i]
            s = s + d * d
            i = i + 1
        end while
        return s
    end function

    ' Euclidean distance.
    function _euclid(a, b)
        return sqrt(_dist2(a, b))
    end function

    ' Element-by-element copy of a point (defensive — gBASIC lists copy on
    ' assignment, but centroid rows are mutated in place during Lloyd steps).
    function _copy_point(pt)
        r = []
        i = 0
        while i < len(pt)
            append(r, pt[i])
            i = i + 1
        end while
        return r
    end function

    ' Concatenate two lists into a new list.
    function _concat(a, b)
        r = []
        i = 0
        while i < len(a)
            append(r, a[i])
            i = i + 1
        end while
        i = 0
        while i < len(b)
            append(r, b[i])
            i = i + 1
        end while
        return r
    end function

    ' --- k-means (Lloyd's algorithm, k-means++ seeding) ---
    ' `data` is a list of points; `k` the cluster count; `max_iter` the
    ' assignment-recompute cap. Seeding draws from the RNG, so call seed()
    ' first for reproducible runs. Returns the per-point `labels`, the final
    ' `centroids`, the `inertia` (sum of squared point-to-centroid distances),
    ' the iteration count, and whether assignments converged. `unknown` if
    ' k is out of range.
    function kmeans(data, k, max_iter)
        n = len(data)
        if n = 0 then
            return unknown
        end if
        if k < 1 then
            return unknown
        end if
        if k > n then
            return unknown
        end if
        p = len(data[0])

        ' k-means++ seeding: first centre uniform, each next chosen with
        ' probability proportional to squared distance from the nearest centre.
        centroids = []
        append(centroids, _copy_point(data[random_int(0, n - 1)]))
        c = 1
        while c < k
            d2 = []
            total = 0
            i = 0
            while i < n
                best = _dist2(data[i], centroids[0])
                j = 1
                while j < c
                    dd = _dist2(data[i], centroids[j])
                    if dd < best then
                        best = dd
                    end if
                    j = j + 1
                end while
                append(d2, best)
                total = total + best
                i = i + 1
            end while
            chosen = n - 1
            if total <= 0 then
                chosen = random_int(0, n - 1)
            else
                target = random() * total
                cum = 0
                i = 0
                picked = false
                while i < n and picked = false
                    cum = cum + d2[i]
                    if cum >= target then
                        chosen = i
                        picked = true
                    end if
                    i = i + 1
                end while
            end if
            append(centroids, _copy_point(data[chosen]))
            c = c + 1
        end while

        ' Lloyd iterations: assign, then recompute means.
        labels = []
        i = 0
        while i < n
            append(labels, 0)
            i = i + 1
        end while
        iter = 0
        converged = false
        while iter < max_iter and converged = false
            changed = false
            i = 0
            while i < n
                best = _dist2(data[i], centroids[0])
                bestj = 0
                j = 1
                while j < k
                    dd = _dist2(data[i], centroids[j])
                    if dd < best then
                        best = dd
                        bestj = j
                    end if
                    j = j + 1
                end while
                if labels[i] != bestj then
                    changed = true
                end if
                labels[i] = bestj
                i = i + 1
            end while
            ' accumulate per-cluster sums and counts
            sums = []
            counts = []
            j = 0
            while j < k
                row = []
                d = 0
                while d < p
                    append(row, 0)
                    d = d + 1
                end while
                append(sums, row)
                append(counts, 0)
                j = j + 1
            end while
            i = 0
            while i < n
                lbl = labels[i]
                counts[lbl] = counts[lbl] + 1
                d = 0
                while d < p
                    sums[lbl][d] = sums[lbl][d] + data[i][d]
                    d = d + 1
                end while
                i = i + 1
            end while
            j = 0
            while j < k
                if counts[j] > 0 then
                    d = 0
                    while d < p
                        centroids[j][d] = sums[j][d] / counts[j]
                        d = d + 1
                    end while
                end if
                j = j + 1
            end while
            iter = iter + 1
            if changed = false then
                converged = true
            end if
        end while

        inertia = 0
        i = 0
        while i < n
            inertia = inertia + _dist2(data[i], centroids[labels[i]])
            i = i + 1
        end while
        return { labels: labels, centroids: centroids, inertia: inertia, iterations: iter, converged: converged, k: k }
    end function

    ' --- Agglomerative hierarchical clustering ---
    ' Distance between two clusters under `method`: "single" (nearest pair),
    ' "complete" (farthest pair), or "average" (UPGMA, mean of all cross
    ' pairs). `pd` is the precomputed point distance matrix.
    function _link_dist(ca, cb, pd, method)
        first = true
        acc = 0
        cnt = 0
        i = 0
        while i < len(ca)
            j = 0
            while j < len(cb)
                d = pd[ca[i]][cb[j]]
                if method = "single" then
                    if first then
                        acc = d
                        first = false
                    else
                        if d < acc then
                            acc = d
                        end if
                    end if
                else
                    if method = "complete" then
                        if first then
                            acc = d
                            first = false
                        else
                            if d > acc then
                                acc = d
                            end if
                        end if
                    else
                        acc = acc + d
                        cnt = cnt + 1
                    end if
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        if method = "average" then
            if cnt > 0 then
                return acc / cnt
            end if
            return 0
        end if
        return acc
    end function

    ' Build the merge tree. Returns `merges`, a list of [id1, id2, distance,
    ' size] rows in the scipy linkage Z format: leaves are 0..n-1, and the
    ' m-th merge creates cluster id n+m. Within a row the smaller id comes
    ' first. Feed the result to cut_tree() to get flat cluster labels.
    function hierarchical(data, method)
        n = len(data)
        if n < 1 then
            return unknown
        end if
        if method != "single" and method != "complete" and method != "average" then
            return unknown
        end if
        ' precompute the point distance matrix
        pd = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < n
                append(row, _euclid(data[i], data[j]))
                j = j + 1
            end while
            append(pd, row)
            i = i + 1
        end while
        ' active clusters: members[t] is a list of original point indices,
        ' ids[t] the cluster's linkage id.
        members = []
        ids = []
        i = 0
        while i < n
            append(members, [i])
            append(ids, i)
            i = i + 1
        end while
        merges = []
        next_id = n
        while len(members) > 1
            bi = 0
            bj = 1
            bd = _link_dist(members[0], members[1], pd, method)
            a = 0
            while a < len(members)
                b = a + 1
                while b < len(members)
                    dd = _link_dist(members[a], members[b], pd, method)
                    if dd < bd then
                        bd = dd
                        bi = a
                        bj = b
                    end if
                    b = b + 1
                end while
                a = a + 1
            end while
            id1 = ids[bi]
            id2 = ids[bj]
            lo = id1
            hi = id2
            if id2 < id1 then
                lo = id2
                hi = id1
            end if
            sz = len(members[bi]) + len(members[bj])
            append(merges, [lo, hi, bd, sz])
            merged = _concat(members[bi], members[bj])
            newmembers = []
            newids = []
            t = 0
            while t < len(members)
                if t != bi and t != bj then
                    append(newmembers, members[t])
                    append(newids, ids[t])
                end if
                t = t + 1
            end while
            append(newmembers, merged)
            append(newids, next_id)
            members = newmembers
            ids = newids
            next_id = next_id + 1
        end while
        return { merges: merges, n: n, method: method }
    end function

    ' Cut a hierarchical model into `k` flat clusters by replaying its first
    ' n-k merges, then numbering the surviving clusters 0..k-1 in id order.
    function cut_tree(model, k)
        n = model.n
        merges = model.merges
        if k < 1 then
            return unknown
        end if
        if k > n then
            return unknown
        end if
        total_ids = 2 * n - 1
        members = []
        i = 0
        while i < total_ids
            append(members, [])
            i = i + 1
        end while
        i = 0
        while i < n
            members[i] = [i]
            i = i + 1
        end while
        nmerge = n - k
        m = 0
        while m < nmerge
            row = merges[m]
            a = row[0]
            b = row[1]
            members[n + m] = _concat(members[a], members[b])
            members[a] = []
            members[b] = []
            m = m + 1
        end while
        labels = []
        i = 0
        while i < n
            append(labels, 0)
            i = i + 1
        end while
        lbl = 0
        id = 0
        while id < total_ids
            if len(members[id]) > 0 then
                t = 0
                while t < len(members[id])
                    labels[members[id][t]] = lbl
                    t = t + 1
                end while
                lbl = lbl + 1
            end if
            id = id + 1
        end while
        return labels
    end function

    ' --- Symmetric eigensolver (cyclic Jacobi rotations) ---
    ' Diagonalises a real symmetric matrix `a` (list of rows). Returns
    ' { values, vectors } where values[i] is an eigenvalue and vectors[i] its
    ' unit eigenvector. Trig-free rotation (Numerical Recipes form: only
    ' sqrt), so it needs no trig builtins. This is the PCA workhorse and the
    ' §6 candidate to be reimplemented in C should profiling demand it.
    function _jacobi_eigen(a)
        n = len(a)
        s = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < n
                append(row, a[i][j])
                j = j + 1
            end while
            append(s, row)
            i = i + 1
        end while
        v = mat_identity(n)
        sweep = 0
        converged = false
        while sweep < 100 and converged = false
            off = 0
            p = 0
            while p < n - 1
                q = p + 1
                while q < n
                    off = off + abs(s[p][q])
                    q = q + 1
                end while
                p = p + 1
            end while
            if off < 0.00000000000001 then
                converged = true
            else
                p = 0
                while p < n - 1
                    q = p + 1
                    while q < n
                        apq = s[p][q]
                        if abs(apq) > 0 then
                            app = s[p][p]
                            aqq = s[q][q]
                            theta = (aqq - app) / (2 * apq)
                            t = 0
                            if theta >= 0 then
                                t = 1 / (theta + sqrt(theta * theta + 1))
                            else
                                t = 0 - 1 / (0 - theta + sqrt(theta * theta + 1))
                            end if
                            cs = 1 / sqrt(t * t + 1)
                            sn = t * cs
                            ' right rotation: update columns p, q for every row
                            r = 0
                            while r < n
                                srp = s[r][p]
                                srq = s[r][q]
                                s[r][p] = cs * srp - sn * srq
                                s[r][q] = sn * srp + cs * srq
                                r = r + 1
                            end while
                            ' left rotation: update rows p, q for every column
                            r = 0
                            while r < n
                                spr = s[p][r]
                                sqr = s[q][r]
                                s[p][r] = cs * spr - sn * sqr
                                s[q][r] = sn * spr + cs * sqr
                                r = r + 1
                            end while
                            ' accumulate eigenvectors
                            r = 0
                            while r < n
                                vrp = v[r][p]
                                vrq = v[r][q]
                                v[r][p] = cs * vrp - sn * vrq
                                v[r][q] = sn * vrp + cs * vrq
                                r = r + 1
                            end while
                        end if
                        q = q + 1
                    end while
                    p = p + 1
                end while
            end if
            sweep = sweep + 1
        end while
        values = []
        i = 0
        while i < n
            append(values, s[i][i])
            i = i + 1
        end while
        vectors = []
        i = 0
        while i < n
            col = []
            r = 0
            while r < n
                append(col, v[r][i])
                r = r + 1
            end while
            append(vectors, col)
            i = i + 1
        end while
        return { values: values, vectors: vectors }
    end function

    ' --- Principal component analysis ---
    ' `data` is a list of points; `ncomp` the number of components to keep.
    ' Centres the data, forms the sample covariance (ddof=1, matching
    ' scikit-learn's explained_variance_), eigendecomposes it, and returns the
    ' top `ncomp` `components` (each a unit loading vector), their
    ' `explained_variance` (eigenvalues) and `explained_variance_ratio`, the
    ' projected `scores`, and the feature `mean`. Each component is sign-fixed
    ' so its largest-magnitude loading is positive (deterministic across runs
    ' and architectures). `unknown` if ncomp is out of range.
    function pca(data, ncomp)
        n = len(data)
        if n < 2 then
            return unknown
        end if
        p = len(data[0])
        if ncomp < 1 then
            return unknown
        end if
        if ncomp > p then
            return unknown
        end if
        means = []
        j = 0
        while j < p
            sj = 0
            i = 0
            while i < n
                sj = sj + data[i][j]
                i = i + 1
            end while
            append(means, sj / n)
            j = j + 1
        end while
        centered = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < p
                append(row, data[i][j] - means[j])
                j = j + 1
            end while
            append(centered, row)
            i = i + 1
        end while
        cov = []
        a = 0
        while a < p
            row = []
            b = 0
            while b < p
                sab = 0
                i = 0
                while i < n
                    sab = sab + centered[i][a] * centered[i][b]
                    i = i + 1
                end while
                append(row, sab / (n - 1))
                b = b + 1
            end while
            append(cov, row)
            a = a + 1
        end while
        eig = _jacobi_eigen(cov)
        vals = eig.values
        vecs = eig.vectors
        ' order indices by descending eigenvalue
        order = []
        i = 0
        while i < p
            append(order, i)
            i = i + 1
        end while
        a = 0
        while a < p
            bmax = a
            b = a + 1
            while b < p
                if vals[order[b]] > vals[order[bmax]] then
                    bmax = b
                end if
                b = b + 1
            end while
            tmp = order[a]
            order[a] = order[bmax]
            order[bmax] = tmp
            a = a + 1
        end while
        totvar = 0
        j = 0
        while j < p
            totvar = totvar + vals[j]
            j = j + 1
        end while
        components = []
        explained = []
        ratio = []
        c = 0
        while c < ncomp
            idx = order[c]
            vec = vecs[idx]
            ' sign convention: largest-magnitude loading positive
            bigabs = 0
            bigval = 0
            j = 0
            while j < p
                if abs(vec[j]) > bigabs then
                    bigabs = abs(vec[j])
                    bigval = vec[j]
                end if
                j = j + 1
            end while
            sgn = 1
            if bigval < 0 then
                sgn = 0 - 1
            end if
            comp = []
            j = 0
            while j < p
                append(comp, sgn * vec[j])
                j = j + 1
            end while
            append(components, comp)
            append(explained, vals[idx])
            if totvar > 0 then
                append(ratio, vals[idx] / totvar)
            else
                append(ratio, 0)
            end if
            c = c + 1
        end while
        scores = []
        i = 0
        while i < n
            row = []
            c = 0
            while c < ncomp
                sc = 0
                j = 0
                while j < p
                    sc = sc + centered[i][j] * components[c][j]
                    j = j + 1
                end while
                append(row, sc)
                c = c + 1
            end while
            append(scores, row)
            i = i + 1
        end while
        return { components: components, explained_variance: explained, explained_variance_ratio: ratio, scores: scores, mean: means, n: n }
    end function

    ' ===================================================================
    ' Phase 4 — time series (statistics_design.md §8 Phase 4).
    '
    ' Temporal methods composed in pure gBASIC: moving averages, the
    ' autocorrelation / partial-autocorrelation functions, and the
    ' exponential-smoothing family (simple, Holt's linear trend, additive
    ' Holt-Winters). A series is a flat list of numbers in time order.
    ' Values verified against pandas / statsmodels; the smoothing recursions
    ' follow the standard Hyndman state-space form (parameters are supplied,
    ' not optimized — the MLE optimizer for ARIMA/GARCH is a later phase).
    ' ===================================================================

    ' Mean of count elements of xs starting at lo. Helper for seasonal init.
    function _range_mean(xs, lo, count)
        s = 0
        i = 0
        while i < count
            s = s + xs[lo + i]
            i = i + 1
        end while
        return s / count
    end function

    ' Simple moving average over a trailing window. Returns a list the same
    ' length as xs; the first window-1 positions are `unknown` (no full
    ' window yet), matching pandas Series.rolling(window).mean().
    function sma(xs, window)
        n = len(xs)
        if window < 1 then
            return unknown
        end if
        if window > n then
            return unknown
        end if
        out = []
        i = 0
        while i < n
            if i < window - 1 then
                append(out, unknown)
            else
                s = 0
                j = 0
                while j < window
                    s = s + xs[i - j]
                    j = j + 1
                end while
                append(out, s / window)
            end if
            i = i + 1
        end while
        return out
    end function

    ' Exponentially weighted moving average, recursive form (adjust=False):
    ' s[0] = x[0]; s[t] = alpha*x[t] + (1-alpha)*s[t-1]. Matches pandas
    ' Series.ewm(alpha=alpha, adjust=False).mean(). alpha in (0,1].
    function ewma(xs, alpha)
        n = len(xs)
        if n < 1 then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha > 1 then
            return unknown
        end if
        out = [xs[0]]
        s = xs[0]
        t = 1
        while t < n
            s = alpha * xs[t] + (1 - alpha) * s
            append(out, s)
            t = t + 1
        end while
        return out
    end function

    ' d-th order differencing: each pass replaces the series with its
    ' consecutive differences, so the result has len(xs)-d elements.
    ' d = 0 returns a copy. Useful for de-trending before acf/pacf.
    function diff(xs, d)
        if d < 0 then
            return unknown
        end if
        cur = []
        i = 0
        while i < len(xs)
            append(cur, xs[i])
            i = i + 1
        end while
        pass = 0
        while pass < d
            if len(cur) < 2 then
                return []
            end if
            nxt = []
            i = 1
            while i < len(cur)
                append(nxt, cur[i] - cur[i - 1])
                i = i + 1
            end while
            cur = nxt
            pass = pass + 1
        end while
        return cur
    end function

    ' Autocorrelation function for lags 0..nlags (so nlags+1 values, the
    ' first always 1). Biased estimator (divides by n), demeaned by the full
    ' series mean — matches statsmodels.tsa.stattools.acf(adjusted=False).
    function acf(xs, nlags)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        if nlags < 0 then
            return unknown
        end if
        if nlags >= n then
            return unknown
        end if
        xbar = mean(xs)
        c0 = 0
        i = 0
        while i < n
            d = xs[i] - xbar
            c0 = c0 + d * d
            i = i + 1
        end while
        if c0 = 0 then
            return unknown
        end if
        out = [1]
        k = 1
        while k <= nlags
            ck = 0
            t = 0
            while t < n - k
                ck = ck + (xs[t] - xbar) * (xs[t + k] - xbar)
                t = t + 1
            end while
            append(out, ck / c0)
            k = k + 1
        end while
        return out
    end function

    ' Partial autocorrelation for lags 0..nlags via the Durbin-Levinson
    ' recursion over the biased acf (Yule-Walker). Matches statsmodels
    ' pacf(method="ywm"). pacf[0] = 1.
    function pacf(xs, nlags)
        r = acf(xs, nlags)
        if is_unknown(r) then
            return unknown
        end if
        out = [1]
        phi = []
        k = 1
        while k <= nlags
            if k = 1 then
                pkk = r[1]
                phi = [pkk]
            else
                num = r[k]
                j = 1
                while j <= k - 1
                    num = num - phi[j - 1] * r[k - j]
                    j = j + 1
                end while
                den = 1
                j = 1
                while j <= k - 1
                    den = den - phi[j - 1] * r[j]
                    j = j + 1
                end while
                if den = 0 then
                    return unknown
                end if
                pkk = num / den
                newphi = []
                j = 1
                while j <= k - 1
                    append(newphi, phi[j - 1] - pkk * phi[k - 1 - j])
                    j = j + 1
                end while
                append(newphi, pkk)
                phi = newphi
            end if
            append(out, pkk)
            k = k + 1
        end while
        return out
    end function

    ' Simple exponential smoothing (no trend, no seasonality).
    ' level[0] = x[0]; level[t] = alpha*x[t] + (1-alpha)*level[t-1].
    ' The one-step-ahead fitted value at t is level[t-1] (so fitted[0] is
    ' unknown). `forecast` is flat at the last level over horizon h. alpha in
    ' (0,1]. Equivalent to ewma for the level path.
    function ses(xs, alpha, h)
        n = len(xs)
        if n < 1 then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha > 1 then
            return unknown
        end if
        if h < 0 then
            return unknown
        end if
        level = [xs[0]]
        fitted = [unknown]
        sse = 0
        t = 1
        while t < n
            f = level[t - 1]
            append(fitted, f)
            e = xs[t] - f
            sse = sse + e * e
            append(level, alpha * xs[t] + (1 - alpha) * level[t - 1])
            t = t + 1
        end while
        last = level[n - 1]
        forecast = []
        i = 0
        while i < h
            append(forecast, last)
            i = i + 1
        end while
        return { level: level, fitted: fitted, forecast: forecast, sse: sse, alpha: alpha }
    end function

    ' Holt's linear-trend (double exponential) smoothing, additive trend.
    ' level[0] = x[0]; trend[0] = x[1]-x[0];
    ' level[t] = alpha*x[t] + (1-alpha)*(level[t-1]+trend[t-1]);
    ' trend[t] = beta*(level[t]-level[t-1]) + (1-beta)*trend[t-1].
    ' One-step fitted[t] = level[t-1]+trend[t-1]; forecast h steps ahead is
    ' level_last + k*trend_last. Needs at least two observations.
    function holt(xs, alpha, beta, h)
        n = len(xs)
        if n < 2 then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha > 1 then
            return unknown
        end if
        if beta < 0 then
            return unknown
        end if
        if beta > 1 then
            return unknown
        end if
        if h < 0 then
            return unknown
        end if
        level = [xs[0]]
        trend = [xs[1] - xs[0]]
        fitted = [unknown]
        sse = 0
        t = 1
        while t < n
            pl = level[t - 1]
            pb = trend[t - 1]
            f = pl + pb
            append(fitted, f)
            e = xs[t] - f
            sse = sse + e * e
            nl = alpha * xs[t] + (1 - alpha) * (pl + pb)
            nb = beta * (nl - pl) + (1 - beta) * pb
            append(level, nl)
            append(trend, nb)
            t = t + 1
        end while
        ll = level[n - 1]
        lb = trend[n - 1]
        forecast = []
        k = 1
        while k <= h
            append(forecast, ll + k * lb)
            k = k + 1
        end while
        return { level: level, trend: trend, fitted: fitted, forecast: forecast, sse: sse }
    end function

    ' Additive Holt-Winters (triple exponential): level + linear trend +
    ' additive seasonality of the given period. Seasonal init from the first
    ' cycle, trend init from the difference of the first two cycle means, so
    ' the series needs at least two full periods. Recursion starts at t=period;
    ' forecast k steps ahead = level_last + k*trend_last + season[n-period +
    ' ((k-1) mod period)].
    function holt_winters(xs, alpha, beta, gamma, period, h)
        n = len(xs)
        if period < 1 then
            return unknown
        end if
        if n < 2 * period then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha > 1 then
            return unknown
        end if
        if beta < 0 then
            return unknown
        end if
        if beta > 1 then
            return unknown
        end if
        if gamma < 0 then
            return unknown
        end if
        if gamma > 1 then
            return unknown
        end if
        if h < 0 then
            return unknown
        end if
        l0 = _range_mean(xs, 0, period)
        b0 = (_range_mean(xs, period, period) - l0) / period
        season = []
        i = 0
        while i < period
            append(season, xs[i] - l0)
            i = i + 1
        end while
        levelL = l0
        trendB = b0
        fitted = []
        i = 0
        while i < period
            append(fitted, unknown)
            i = i + 1
        end while
        t = period
        while t < n
            sp = season[t - period]
            pl = levelL
            pb = trendB
            append(fitted, pl + pb + sp)
            levelL = alpha * (xs[t] - sp) + (1 - alpha) * (pl + pb)
            trendB = beta * (levelL - pl) + (1 - beta) * pb
            append(season, gamma * (xs[t] - levelL) + (1 - gamma) * sp)
            t = t + 1
        end while
        forecast = []
        k = 1
        while k <= h
            kk = k - 1
            mm = kk - period * floor(kk / period)
            append(forecast, levelL + k * trendB + season[n - period + mm])
            k = k + 1
        end while
        return { level: levelL, trend: trendB, season: season, fitted: fitted, forecast: forecast, period: period }
    end function

    ' ===================================================================
    ' Phase 5 — correlation family & effect sizes
    ' (docs/statistics_scientist_plan.md). Thin compositions over the Pearson
    ' `correlation` builtin, the `_rank` helper, and the existing distribution
    ' CDFs. Verified against scipy.stats; tie-free data is used where a test's
    ' tie handling would otherwise diverge from the asymptotic reference.
    ' ===================================================================

    ' Total tied-pair count: sum over tie groups of t*(t-1)/2. Used for the
    ' tau-b denominator.
    function _tie_pairs(xs)
        r = _rank(xs)
        ' ranks are equal within a tie group; count via sorted rank multiplicities
        sorted = sort(r)
        total = 0
        i = 0
        n = len(sorted)
        while i < n
            j = i
            while j < n and sorted[j] = sorted[i]
                j = j + 1
            end while
            g = j - i
            total = total + g * (g - 1) / 2
            i = j
        end while
        return total
    end function

    ' Two-sided p from a correlation coefficient via the t approximation,
    ' df degrees of freedom.
    function _corr_p(r, df)
        denom = 1 - r * r
        if denom <= 0 then
            return 0
        end if
        t = r * sqrt(df / denom)
        return 2 * t_cdf(0 - abs(t), df)
    end function

    ' Spearman rank correlation (Pearson on average ranks); t-approx p, df=n-2.
    function spearman(x, y)
        n = len(x)
        if n != len(y) then
            return unknown
        end if
        if n < 3 then
            return unknown
        end if
        rho = correlation(_rank(x), _rank(y))
        if is_unknown(rho) then
            return unknown
        end if
        return { rho: rho, p_value: _corr_p(rho, n - 2), n: n }
    end function

    ' Kendall's tau-b (tie-corrected), asymptotic normal p (no continuity
    ' correction, no-tie variance) — matches scipy.stats.kendalltau(method=
    ' "asymptotic") on tie-free data.
    function kendall_tau(x, y)
        n = len(x)
        if n != len(y) then
            return unknown
        end if
        if n < 2 then
            return unknown
        end if
        cc = 0
        dd = 0
        i = 0
        while i < n
            j = i + 1
            while j < n
                prod = (x[j] - x[i]) * (y[j] - y[i])
                if prod > 0 then
                    cc = cc + 1
                else
                    if prod < 0 then
                        dd = dd + 1
                    end if
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        n0 = n * (n - 1) / 2
        denom = sqrt((n0 - _tie_pairs(x)) * (n0 - _tie_pairs(y)))
        if denom <= 0 then
            return unknown
        end if
        tau = (cc - dd) / denom
        z = (cc - dd) / sqrt(n * (n - 1) * (2 * n + 5) / 18)
        return { tau: tau, p_value: 2 * _norm_cdf_std(0 - abs(z)), n: n }
    end function

    ' Partial correlation of x and y controlling for z; t-approx p, df=n-3.
    function partial_correlation(x, y, z)
        n = len(x)
        if n != len(y) or n != len(z) then
            return unknown
        end if
        if n < 4 then
            return unknown
        end if
        rxy = correlation(x, y)
        rxz = correlation(x, z)
        ryz = correlation(y, z)
        denom = sqrt((1 - rxz * rxz) * (1 - ryz * ryz))
        if denom <= 0 then
            return unknown
        end if
        r = (rxy - rxz * ryz) / denom
        return { r: r, p_value: _corr_p(r, n - 3), n: n }
    end function

    ' Point-biserial correlation between a binary (0/1) variable and a continuous
    ' one (Pearson correlation); t-approx p, df=n-2.
    function point_biserial(binary, x)
        n = len(binary)
        if n != len(x) then
            return unknown
        end if
        if n < 3 then
            return unknown
        end if
        r = correlation(binary, x)
        if is_unknown(r) then
            return unknown
        end if
        return { r: r, p_value: _corr_p(r, n - 2) }
    end function

    ' Cramér's V (uncorrected) from a contingency table.
    function cramers_v(table)
        res = chi_square_independence(table)
        if is_unknown(res) then
            return unknown
        end if
        rows = len(table)
        cols = len(table[0])
        total = 0
        i = 0
        while i < rows
            j = 0
            while j < cols
                total = total + table[i][j]
                j = j + 1
            end while
            i = i + 1
        end while
        k = min([rows, cols]) - 1
        if total <= 0 or k < 1 then
            return unknown
        end if
        return { v: sqrt(res.statistic / (total * k)), chi2: res.statistic, dof: res.df }
    end function

    ' Hedges' g: bias-corrected standardized mean difference (pooled SD).
    function hedges_g(a, b)
        n1 = len(a)
        n2 = len(b)
        if n1 < 2 or n2 < 2 then
            return unknown
        end if
        sp = sqrt(((n1 - 1) * variance(a) + (n2 - 1) * variance(b)) / (n1 + n2 - 2))
        if sp <= 0 then
            return unknown
        end if
        d = (mean(a) - mean(b)) / sp
        jj = 1 - 3 / (4 * (n1 + n2) - 9)
        return { g: d * jj, d: d }
    end function

    ' Sample odds ratio for a 2x2 table [[a,b],[c,d]] with a Woolf log-normal
    ' 95% confidence interval.
    function odds_ratio(table)
        if len(table) != 2 or len(table[0]) != 2 or len(table[1]) != 2 then
            return unknown
        end if
        a = table[0][0]
        b = table[0][1]
        c = table[1][0]
        d = table[1][1]
        if a <= 0 or b <= 0 or c <= 0 or d <= 0 then
            return unknown
        end if
        orr = (a * d) / (b * c)
        se = sqrt(1 / a + 1 / b + 1 / c + 1 / d)
        z = 1.959963984540054
        lo = exp(log(orr) - z * se)
        hi = exp(log(orr) + z * se)
        return { odds_ratio: orr, ci_low: lo, ci_high: hi, log_or_se: se }
    end function

    ' Eta-squared and omega-squared effect sizes for a one-way design.
    function eta_squared(groups)
        res = anova_oneway(groups)
        if is_unknown(res) then
            return unknown
        end if
        ssb = res.ss_between
        ssw = res.ss_within
        msw = res.ms_within
        sst = ssb + ssw
        if sst <= 0 then
            return unknown
        end if
        k = len(groups)
        eta = ssb / sst
        omega = (ssb - (k - 1) * msw) / (sst + msw)
        return { eta_squared: eta, omega_squared: omega }
    end function

    ' ------------------------------------------------------------------
    ' Phase 6 — distribution expansion (statistics_scientist_plan.md §6).
    ' Each family provides *_pdf / *_cdf / *_quantile, built on the same
    ' incomplete-gamma (_gammp) / incomplete-beta (_betai) / standard-normal
    ' engines the Phase 1 distributions use. Parameterizations match scipy:
    '   uniform(a, b)      -> bounds a < b            (scipy loc=a, scale=b-a)
    '   expon(rate)        -> rate = 1/scale
    '   gamma(shape, rate) -> shape=a, rate=1/scale
    '   beta(a, b)         -> support [0, 1]
    '   lognormal(mu, sigma) -> log-space mean/sd     (scipy s=sigma, scale=exp(mu))
    '   weibull(shape, scale) -> scipy weibull_min c=shape, scale=scale
    '   negbinom(r, p)     -> failures before r-th success (scipy nbinom n=r, p=p)
    ' ------------------------------------------------------------------

    ' --- Uniform distribution on [a, b] ---

    function uniform_pdf(x, a, b)
        if b <= a then
            return unknown
        end if
        if x < a then
            return 0
        end if
        if x > b then
            return 0
        end if
        return 1 / (b - a)
    end function

    function uniform_cdf(x, a, b)
        if b <= a then
            return unknown
        end if
        if x < a then
            return 0
        end if
        if x > b then
            return 1
        end if
        return (x - a) / (b - a)
    end function

    function uniform_quantile(p, a, b)
        if b <= a then
            return unknown
        end if
        if p < 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        return a + p * (b - a)
    end function

    ' --- Exponential distribution (rate lambda) ---

    function expon_pdf(x, rate)
        if rate <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        return rate * exp(0 - rate * x)
    end function

    function expon_cdf(x, rate)
        if rate <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        return 1 - exp(0 - rate * x)
    end function

    function expon_quantile(p, rate)
        if rate <= 0 then
            return unknown
        end if
        if p < 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        return (0 - log(1 - p)) / rate
    end function

    ' --- Gamma distribution (shape k, rate beta) ---

    function gamma_pdf(x, shape, rate)
        if shape <= 0 then
            return unknown
        end if
        if rate <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        if x = 0 then
            if shape < 1 then
                return unknown
            end if
            if shape = 1 then
                return rate
            end if
            return 0
        end if
        return exp(shape * log(rate) + (shape - 1) * log(x) - rate * x - lgamma(shape))
    end function

    function gamma_cdf(x, shape, rate)
        if shape <= 0 then
            return unknown
        end if
        if rate <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        return _gammp(shape, rate * x)
    end function

    function gamma_quantile(p, shape, rate)
        if shape <= 0 then
            return unknown
        end if
        if rate <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        tol = pow(10, -10)
        hi = 1 / rate
        while gamma_cdf(hi, shape, rate) < p
            hi = hi * 2
        end while
        lo = 0
        i = 0
        while i < 300
            mid = (lo + hi) / 2
            if gamma_cdf(mid, shape, rate) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < tol then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' --- Beta distribution (shape a, shape b) on [0, 1] ---

    function beta_pdf(x, a, b)
        if a <= 0 then
            return unknown
        end if
        if b <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        if x > 1 then
            return 0
        end if
        if x = 0 then
            if a < 1 then
                return unknown
            end if
            if a = 1 then
                return exp(lgamma(a + b) - lgamma(a) - lgamma(b) + (b - 1) * log(1))
            end if
            return 0
        end if
        if x = 1 then
            if b < 1 then
                return unknown
            end if
            if b = 1 then
                return exp(lgamma(a + b) - lgamma(a) - lgamma(b))
            end if
            return 0
        end if
        return exp(lgamma(a + b) - lgamma(a) - lgamma(b) + (a - 1) * log(x) + (b - 1) * log(1 - x))
    end function

    function beta_cdf(x, a, b)
        if a <= 0 then
            return unknown
        end if
        if b <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        if x >= 1 then
            return 1
        end if
        return _betai(a, b, x)
    end function

    function beta_quantile(p, a, b)
        if a <= 0 then
            return unknown
        end if
        if b <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        tol = pow(10, -12)
        lo = 0
        hi = 1
        i = 0
        while i < 300
            mid = (lo + hi) / 2
            if beta_cdf(mid, a, b) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < tol then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' --- Log-normal distribution (log-space mu, sigma) ---

    function lognormal_pdf(x, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        z = (log(x) - mu) / sigma
        return exp(0 - z * z / 2) / (x * sigma * _sqrt2pi())
    end function

    function lognormal_cdf(x, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        return _norm_cdf_std((log(x) - mu) / sigma)
    end function

    function lognormal_quantile(p, mu, sigma)
        if sigma <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        z = _inv_norm_std(p)
        if is_unknown(z) then
            return unknown
        end if
        return exp(mu + sigma * z)
    end function

    ' --- Weibull distribution (shape k, scale lambda) ---

    function weibull_pdf(x, shape, scale)
        if shape <= 0 then
            return unknown
        end if
        if scale <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        if x = 0 then
            if shape < 1 then
                return unknown
            end if
            if shape = 1 then
                return 1 / scale
            end if
            return 0
        end if
        z = x / scale
        return (shape / scale) * pow(z, shape - 1) * exp(0 - pow(z, shape))
    end function

    function weibull_cdf(x, shape, scale)
        if shape <= 0 then
            return unknown
        end if
        if scale <= 0 then
            return unknown
        end if
        if x < 0 then
            return 0
        end if
        return 1 - exp(0 - pow(x / scale, shape))
    end function

    function weibull_quantile(p, shape, scale)
        if shape <= 0 then
            return unknown
        end if
        if scale <= 0 then
            return unknown
        end if
        if p < 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        return scale * pow(0 - log(1 - p), 1 / shape)
    end function

    ' --- Negative binomial (r successes, success prob p; counts failures) ---

    function negbinom_pmf(k, r, p)
        if r <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        if k < 0 then
            return 0
        end if
        kk = floor(k)
        return exp(lgamma(kk + r) - lgamma(r) - lgamma(kk + 1) + r * log(p) + kk * log(1 - p))
    end function

    ' P(X <= k) = I_p(r, k+1) (regularized incomplete beta), the same identity
    ' the binomial CDF uses.
    function negbinom_cdf(k, r, p)
        if r <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        kk = floor(k)
        if kk < 0 then
            return 0
        end if
        return _betai(r, kk + 1, p)
    end function

    function negbinom_quantile(prob, r, p)
        if r <= 0 then
            return unknown
        end if
        if p <= 0 then
            return unknown
        end if
        if p > 1 then
            return unknown
        end if
        if prob <= 0 then
            return unknown
        end if
        if prob >= 1 then
            return unknown
        end if
        k = 0
        while k < 1000000
            if negbinom_cdf(k, r, p) >= prob then
                return k
            end if
            k = k + 1
        end while
        return k
    end function

    ' ------------------------------------------------------------------
    ' Phase 7 — experimental-design tests (statistics_scientist_plan.md §7):
    ' two-way ANOVA, repeated-measures ANOVA, Friedman, Tukey HSD. All pure
    ' gBASIC. The one research piece is the studentized-range distribution
    ' (_ptukey / _qtukey) needed for Tukey HSD p-values and CIs — computed by
    ' nested Simpson quadrature of the classic range-CDF integral, verified to
    ' 6 decimals against scipy.stats.studentized_range.
    ' ------------------------------------------------------------------

    ' Inner integral: G(w) = P(range of k iid N(0,1) <= w)
    '   = k * integral phi(z) * (Phi(z) - Phi(z-w))^(k-1) dz  over z.
    ' Composite Simpson over z in [-7.5, 7.5], 160 panels.
    function _range_cdf(w, k)
        if w <= 0 then
            return 0
        end if
        nz = 160
        zb = 7.5
        h = (2 * zb) / nz
        km1 = k - 1
        sp = _sqrt2pi()
        z = 0 - zb
        d = _norm_cdf_std(z) - _norm_cdf_std(z - w)
        total = (exp(0 - z * z / 2) / sp) * pow(d, km1)
        z = zb
        d = _norm_cdf_std(z) - _norm_cdf_std(z - w)
        total = total + (exp(0 - z * z / 2) / sp) * pow(d, km1)
        coef = 4
        i = 1
        while i < nz
            z = (0 - zb) + i * h
            d = _norm_cdf_std(z) - _norm_cdf_std(z - w)
            term = (exp(0 - z * z / 2) / sp) * pow(d, km1)
            total = total + coef * term
            coef = 6 - coef
            i = i + 1
        end while
        return k * total * h / 3
    end function

    ' Studentized-range CDF P(Q <= q; k groups, v error df)
    '   = integral f_v(s) * G(q*s) ds, where v*s^2 ~ chi-squared_v.
    ' Composite Simpson over s in [0, smax], 80 panels (s=0 term is 0).
    function _ptukey(q, k, v)
        if q <= 0 then
            return 0
        end if
        if v <= 0 then
            return unknown
        end if
        c = v / 2
        lc = c * log(v) - (c - 1) * log(2) - lgamma(c)
        smax = sqrt(chi2_quantile(0.999999, v) / v)
        ns = 80
        h = smax / ns
        sv = smax
        total = exp(lc + (v - 1) * log(sv) - v * sv * sv / 2) * _range_cdf(q * sv, k)
        coef = 4
        i = 1
        while i < ns
            sv = i * h
            f = exp(lc + (v - 1) * log(sv) - v * sv * sv / 2)
            total = total + coef * f * _range_cdf(q * sv, k)
            coef = 6 - coef
            i = i + 1
        end while
        return total * h / 3
    end function

    ' Studentized-range quantile (inverse of _ptukey) by bisection.
    function _qtukey(p, k, v)
        if p <= 0 then
            return unknown
        end if
        if p >= 1 then
            return unknown
        end if
        lo = 0
        hi = 100
        i = 0
        while i < 60
            mid = (lo + hi) / 2
            if _ptukey(mid, k, v) < p then
                lo = mid
            else
                hi = mid
            end if
            if hi - lo < 0.0001 then
                break
            end if
            i = i + 1
        end while
        return (lo + hi) / 2
    end function

    ' Two-way ANOVA, balanced design (Type I = II = III when balanced).
    ' cells[i][j] is the list of replicate observations for factor-A level i,
    ' factor-B level j; every cell must hold the same number (n >= 2) of values.
    ' Returns per-factor and interaction {ss, df, ms, statistic, p_value} plus
    ' the error line, or unknown on malformed / unbalanced input.
    function anova_twoway(cells)
        a = len(cells)
        if a < 2 then
            return unknown
        end if
        b = len(cells[0])
        if b < 2 then
            return unknown
        end if
        n = len(cells[0][0])
        if n < 2 then
            return unknown
        end if
        grand = 0
        ntot = 0
        i = 0
        while i < a
            if len(cells[i]) != b then
                return unknown
            end if
            j = 0
            while j < b
                if len(cells[i][j]) != n then
                    return unknown
                end if
                r = 0
                while r < n
                    grand = grand + cells[i][j][r]
                    ntot = ntot + 1
                    r = r + 1
                end while
                j = j + 1
            end while
            i = i + 1
        end while
        grand = grand / ntot
        ' A-marginal means
        amean = []
        i = 0
        while i < a
            s = 0
            j = 0
            while j < b
                r = 0
                while r < n
                    s = s + cells[i][j][r]
                    r = r + 1
                end while
                j = j + 1
            end while
            append(amean, s / (b * n))
            i = i + 1
        end while
        ' B-marginal means
        bmean = []
        j = 0
        while j < b
            s = 0
            i = 0
            while i < a
                r = 0
                while r < n
                    s = s + cells[i][j][r]
                    r = r + 1
                end while
                i = i + 1
            end while
            append(bmean, s / (a * n))
            j = j + 1
        end while
        ssa = 0
        i = 0
        while i < a
            ssa = ssa + (amean[i] - grand) * (amean[i] - grand)
            i = i + 1
        end while
        ssa = b * n * ssa
        ssb = 0
        j = 0
        while j < b
            ssb = ssb + (bmean[j] - grand) * (bmean[j] - grand)
            j = j + 1
        end while
        ssb = a * n * ssb
        ssab = 0
        sse = 0
        i = 0
        while i < a
            j = 0
            while j < b
                cm = 0
                r = 0
                while r < n
                    cm = cm + cells[i][j][r]
                    r = r + 1
                end while
                cm = cm / n
                inter = cm - amean[i] - bmean[j] + grand
                ssab = ssab + inter * inter
                r = 0
                while r < n
                    d = cells[i][j][r] - cm
                    sse = sse + d * d
                    r = r + 1
                end while
                j = j + 1
            end while
            i = i + 1
        end while
        ssab = n * ssab
        dfa = a - 1
        dfb = b - 1
        dfab = (a - 1) * (b - 1)
        dfe = a * b * (n - 1)
        if dfe < 1 then
            return unknown
        end if
        mse = sse / dfe
        if mse <= 0 then
            return unknown
        end if
        msa = ssa / dfa
        msb = ssb / dfb
        msab = ssab / dfab
        fa = msa / mse
        fb = msb / mse
        fab = msab / mse
        ra = { ss: ssa, df: dfa, ms: msa, statistic: fa, p_value: 1 - f_cdf(fa, dfa, dfe) }
        rb = { ss: ssb, df: dfb, ms: msb, statistic: fb, p_value: 1 - f_cdf(fb, dfb, dfe) }
        rab = { ss: ssab, df: dfab, ms: msab, statistic: fab, p_value: 1 - f_cdf(fab, dfab, dfe) }
        re = { ss: sse, df: dfe, ms: mse }
        return { a: ra, b: rb, interaction: rab, residual: re }
    end function

    ' One-way repeated-measures ANOVA. data[s][c] is subject s's value under
    ' condition c (all subjects measured under all k conditions). Returns
    ' {statistic, df1, df2, p_value, partial_eta2} or unknown.
    function anova_repeated(data)
        n = len(data)
        if n < 2 then
            return unknown
        end if
        k = len(data[0])
        if k < 2 then
            return unknown
        end if
        grand = 0
        s = 0
        while s < n
            if len(data[s]) != k then
                return unknown
            end if
            c = 0
            while c < k
                grand = grand + data[s][c]
                c = c + 1
            end while
            s = s + 1
        end while
        grand = grand / (n * k)
        ' condition means
        sscond = 0
        c = 0
        while c < k
            cm = 0
            s = 0
            while s < n
                cm = cm + data[s][c]
                s = s + 1
            end while
            cm = cm / n
            sscond = sscond + (cm - grand) * (cm - grand)
            c = c + 1
        end while
        sscond = n * sscond
        ' subject means
        sssubj = 0
        s = 0
        while s < n
            sm = 0
            c = 0
            while c < k
                sm = sm + data[s][c]
                c = c + 1
            end while
            sm = sm / k
            sssubj = sssubj + (sm - grand) * (sm - grand)
            s = s + 1
        end while
        sssubj = k * sssubj
        sstot = 0
        s = 0
        while s < n
            c = 0
            while c < k
                d = data[s][c] - grand
                sstot = sstot + d * d
                c = c + 1
            end while
            s = s + 1
        end while
        sserr = sstot - sscond - sssubj
        df1 = k - 1
        df2 = (n - 1) * (k - 1)
        if sserr <= 0 then
            return unknown
        end if
        mscond = sscond / df1
        mserr = sserr / df2
        fstat = mscond / mserr
        peta = sscond / (sscond + sserr)
        return { statistic: fstat, df1: df1, df2: df2, p_value: 1 - f_cdf(fstat, df1, df2), partial_eta2: peta }
    end function

    ' Friedman test (nonparametric repeated measures). data[s][c] as in
    ' anova_repeated. Ties handled with the standard correction, matching
    ' scipy.stats.friedmanchisquare. Returns {statistic, df, p_value}.
    function friedman(data)
        n = len(data)
        if n < 2 then
            return unknown
        end if
        k = len(data[0])
        if k < 2 then
            return unknown
        end if
        colsum = []
        c = 0
        while c < k
            append(colsum, 0)
            c = c + 1
        end while
        ties = 0
        s = 0
        while s < n
            if len(data[s]) != k then
                return unknown
            end if
            ranks = _rank(data[s])
            c = 0
            while c < k
                colsum[c] = colsum[c] + ranks[c]
                c = c + 1
            end while
            ties = ties + _tie_term(data[s])
            s = s + 1
        end while
        ssbn = 0
        c = 0
        while c < k
            ssbn = ssbn + colsum[c] * colsum[c]
            c = c + 1
        end while
        corr = 1 - ties / (k * (k * k - 1) * n)
        stat = (12 / (k * n * (k + 1)) * ssbn - 3 * n * (k + 1)) / corr
        df = k - 1
        return { statistic: stat, df: df, p_value: 1 - chi2_cdf(stat, df) }
    end function

    ' Tukey HSD all-pairs comparison (alpha = 0.05). groups is a list of
    ' groups (each a list of numbers); unequal sizes use the Tukey-Kramer SE.
    ' Returns a list of {group1, group2, mean_diff, p_adj, ci_low, ci_high,
    ' reject}, one per unordered pair, or unknown on malformed input.
    function tukey_hsd(groups)
        k = len(groups)
        if k < 2 then
            return unknown
        end if
        means = []
        ns = []
        ntot = 0
        i = 0
        while i < k
            ng = len(groups[i])
            if ng < 2 then
                return unknown
            end if
            append(means, mean(groups[i]))
            append(ns, ng)
            ntot = ntot + ng
            i = i + 1
        end while
        dfw = ntot - k
        if dfw < 1 then
            return unknown
        end if
        ssw = 0
        i = 0
        while i < k
            gm = means[i]
            j = 0
            while j < ns[i]
                d = groups[i][j] - gm
                ssw = ssw + d * d
                j = j + 1
            end while
            i = i + 1
        end while
        mse = ssw / dfw
        if mse <= 0 then
            return unknown
        end if
        qcrit = _qtukey(0.95, k, dfw)
        out = []
        i = 0
        while i < k
            j = i + 1
            while j < k
                md = means[i] - means[j]
                se = sqrt(mse / 2 * (1 / ns[i] + 1 / ns[j]))
                q = abs(md) / se
                padj = 1 - _ptukey(q, k, dfw)
                hw = qcrit * se
                rej = false
                if padj < 0.05 then
                    rej = true
                end if
                append(out, { group1: i, group2: j, mean_diff: md, p_adj: padj, ci_low: md - hw, ci_high: md + hw, reject: rej })
                j = j + 1
            end while
            i = i + 1
        end while
        return out
    end function

    ' --- Power analysis (docs/statistics_scientist_plan.md, cross-cutting) ---
    ' Needs the noncentral t and noncentral F distributions. Both are built in
    ' pure gBASIC over the existing incomplete-beta engine (_betai) and the
    ' standard normal (_norm_cdf_std): no new C primitive. Verified against
    ' scipy.stats.nct / scipy.stats.ncf and statsmodels.stats.power to ~1e-10.

    ' Noncentral t CDF P(T <= x) with df degrees of freedom and noncentrality
    ' ncp. Lenth (1989) AS 243 series. Returns unknown for df <= 0.
    function nct_cdf(x, df, ncp)
        if df <= 0 then
            return unknown
        end if
        if x < 0 then
            return 1 - nct_cdf(0 - x, df, 0 - ncp)
        end if
        xx = x * x / (x * x + df)
        if xx <= 0 then
            return _norm_cdf_std(0 - ncp)
        end if
        lambda = ncp * ncp
        p = 0.5 * exp(-0.5 * lambda)
        q = (2 / _sqrt2pi()) * p * ncp
        s = 0.5 - p
        a = 0.5
        b = 0.5 * df
        rxb = pow(1 - xx, b)
        albeta = lgamma(a) + lgamma(b) - lgamma(a + b)
        xodd = _betai(a, b, xx)
        godd = 2 * rxb * exp(a * log(xx) - albeta)
        xeven = 1 - rxb
        geven = b * xx * rxb
        tnc = p * xodd + q * xeven
        it = 1
        errbd = 1
        while it <= 1000 and errbd > pow(10, -12)
            a = a + 1
            xodd = xodd - godd
            xeven = xeven - geven
            godd = godd * xx * (a + b - 1) / a
            geven = geven * xx * (a + b - 0.5) / (a + 0.5)
            p = p * lambda / (2 * it)
            q = q * lambda / (2 * it + 1)
            s = s - p
            tnc = tnc + p * xodd + q * xeven
            errbd = 2 * s * (xodd - godd)
            it = it + 1
        end while
        tnc = tnc + _norm_cdf_std(0 - ncp)
        if tnc < 0 then
            return 0
        end if
        if tnc > 1 then
            return 1
        end if
        return tnc
    end function

    ' Noncentral F CDF P(F <= x) with df1, df2 and noncentrality ncp, as a
    ' Poisson-weighted mixture of central incomplete-beta terms. df1/df2 > 0.
    function ncf_cdf(x, df1, df2, ncp)
        if df1 <= 0 then
            return unknown
        end if
        if df2 <= 0 then
            return unknown
        end if
        if x <= 0 then
            return 0
        end if
        xx = df1 * x / (df1 * x + df2)
        half = ncp / 2
        w = exp(0 - half)
        cum = 0
        s = 0
        j = 0
        while j <= 2000
            ib = _betai(df1 / 2 + j, df2 / 2, xx)
            s = s + w * ib
            cum = cum + w
            if j > half and 1 - cum < pow(10, -12) then
                break
            end if
            w = w * half / (j + 1)
            j = j + 1
        end while
        return s
    end function

    ' Power of a two-sample (independent, equal n per group) t-test.
    ' effect_size = Cohen's d, n = per-group sample size, sided = 1 or 2.
    function power_ttest(effect_size, n, alpha, sided)
        if n < 2 then
            return unknown
        end if
        df = 2 * n - 2
        nc = effect_size * sqrt(n / 2)
        if sided = 2 then
            crit = t_quantile(1 - alpha / 2, df)
            return 1 - nct_cdf(crit, df, nc) + nct_cdf(0 - crit, df, nc)
        end if
        crit = t_quantile(1 - alpha, df)
        return 1 - nct_cdf(crit, df, nc)
    end function

    ' Power of a one-sample / paired t-test. effect_size = Cohen's d,
    ' n = number of observations (or pairs), sided = 1 or 2.
    function power_ttest_paired(effect_size, n, alpha, sided)
        if n < 2 then
            return unknown
        end if
        df = n - 1
        nc = effect_size * sqrt(n)
        if sided = 2 then
            crit = t_quantile(1 - alpha / 2, df)
            return 1 - nct_cdf(crit, df, nc) + nct_cdf(0 - crit, df, nc)
        end if
        crit = t_quantile(1 - alpha, df)
        return 1 - nct_cdf(crit, df, nc)
    end function

    ' Power of a one-way ANOVA. k = number of groups, n = per-group sample
    ' size, effect_size = Cohen's f. Total N = k*n, df1 = k-1, df2 = N-k,
    ' noncentrality = f^2 * N.
    function power_anova(k, n, effect_size, alpha)
        if k < 2 then
            return unknown
        end if
        if n < 2 then
            return unknown
        end if
        nt = k * n
        df1 = k - 1
        df2 = nt - k
        nc = effect_size * effect_size * nt
        crit = f_quantile(1 - alpha, df1, df2)
        return 1 - ncf_cdf(crit, df1, df2, nc)
    end function

    ' Smallest per-group sample size for a two-sample t-test to reach the
    ' target power. Searches upward; returns unknown if none up to n=100000.
    function sample_size_ttest(effect_size, power, alpha, sided)
        if effect_size <= 0 then
            return unknown
        end if
        n = 2
        while n <= 100000
            if power_ttest(effect_size, n, alpha, sided) >= power then
                return n
            end if
            n = n + 1
        end while
        return unknown
    end function

    ' --- Reliability & agreement (docs/statistics_scientist_plan.md §8) ---
    ' All pure gBASIC variance-ratio / coincidence compositions; no new C
    ' primitive. Verified: cohens_kappa vs statsmodels; icc vs published
    ' Shrout & Fleiss (1979) values; krippendorff_alpha vs the standard
    ' worked example (nominal 0.743, interval 0.849).

    ' Cronbach's alpha. data = list of subjects, each a list of item scores
    ' (rows = subjects, columns = items). Sample variances (ddof=1), matching
    ' pingouin. Returns {alpha, n_items, n_subjects} or unknown.
    function cronbach_alpha(data)
        n = len(data)
        if n < 2 then
            return unknown
        end if
        k = len(data[0])
        if k < 2 then
            return unknown
        end if
        itemvar = 0
        j = 0
        while j < k
            col = []
            i = 0
            while i < n
                append(col, data[i][j])
                i = i + 1
            end while
            itemvar = itemvar + variance(col)
            j = j + 1
        end while
        totals = []
        i = 0
        while i < n
            s = 0
            j = 0
            while j < k
                s = s + data[i][j]
                j = j + 1
            end while
            append(totals, s)
            i = i + 1
        end while
        tv = variance(totals)
        if tv <= 0 then
            return unknown
        end if
        alpha = (k / (k - 1)) * (1 - itemvar / tv)
        return { alpha: alpha, n_items: k, n_subjects: n }
    end function

    ' Cohen's kappa for two raters. r1, r2 = equal-length lists of category
    ' labels (numbers). Returns {kappa, po, pe} or unknown.
    function cohens_kappa(r1, r2)
        n = len(r1)
        if n < 1 then
            return unknown
        end if
        if len(r2) != n then
            return unknown
        end if
        cats = []
        i = 0
        while i < n
            if not contains(cats, r1[i]) then
                append(cats, r1[i])
            end if
            if not contains(cats, r2[i]) then
                append(cats, r2[i])
            end if
            i = i + 1
        end while
        agree = 0
        i = 0
        while i < n
            if r1[i] = r2[i] then
                agree = agree + 1
            end if
            i = i + 1
        end while
        po = agree / n
        pe = 0
        c = 0
        while c < len(cats)
            cat = cats[c]
            c1 = 0
            c2 = 0
            i = 0
            while i < n
                if r1[i] = cat then
                    c1 = c1 + 1
                end if
                if r2[i] = cat then
                    c2 = c2 + 1
                end if
                i = i + 1
            end while
            pe = pe + (c1 / n) * (c2 / n)
            c = c + 1
        end while
        if pe >= 1 then
            return unknown
        end if
        return { kappa: (po - pe) / (1 - pe), po: po, pe: pe }
    end function

    ' Intraclass correlation. data = list of subjects, each a list of the
    ' same raters' scores (rows = subjects, columns = raters). Returns the six
    ' Shrout & Fleiss (1979) coefficients {icc1, icc2, icc3, icc1k, icc2k,
    ' icc3k} (1/2/3 = one-way / two-way random / two-way mixed; *k = the
    ' average-of-k-raters forms) or unknown.
    function icc(data)
        n = len(data)
        if n < 2 then
            return unknown
        end if
        k = len(data[0])
        if k < 2 then
            return unknown
        end if
        grand = 0
        i = 0
        while i < n
            j = 0
            while j < k
                grand = grand + data[i][j]
                j = j + 1
            end while
            i = i + 1
        end while
        grand = grand / (n * k)
        ssr = 0
        i = 0
        while i < n
            rm = 0
            j = 0
            while j < k
                rm = rm + data[i][j]
                j = j + 1
            end while
            rm = rm / k
            d = rm - grand
            ssr = ssr + d * d
            i = i + 1
        end while
        ssr = k * ssr
        ssc = 0
        j = 0
        while j < k
            cm = 0
            i = 0
            while i < n
                cm = cm + data[i][j]
                i = i + 1
            end while
            cm = cm / n
            d = cm - grand
            ssc = ssc + d * d
            j = j + 1
        end while
        ssc = n * ssc
        sst = 0
        i = 0
        while i < n
            j = 0
            while j < k
                d = data[i][j] - grand
                sst = sst + d * d
                j = j + 1
            end while
            i = i + 1
        end while
        sse = sst - ssr - ssc
        msr = ssr / (n - 1)
        msc = ssc / (k - 1)
        mse = sse / ((n - 1) * (k - 1))
        msw = (ssc + sse) / (n * (k - 1))
        icc1 = (msr - msw) / (msr + (k - 1) * msw)
        icc2 = (msr - mse) / (msr + (k - 1) * mse + k * (msc - mse) / n)
        icc3 = (msr - mse) / (msr + (k - 1) * mse)
        icc1k = (msr - msw) / msr
        icc2k = (msr - mse) / (msr + (msc - mse) / n)
        icc3k = (msr - mse) / msr
        return { icc1: icc1, icc2: icc2, icc3: icc3, icc1k: icc1k, icc2k: icc2k, icc3k: icc3k }
    end function

    ' Linear scan for the position of v in lst (helper for krippendorff).
    function _index_in(lst, v)
        i = 0
        while i < len(lst)
            if lst[i] = v then
                return i
            end if
            i = i + 1
        end while
        return -1
    end function

    ' Squared-difference metric between value indices a and b for
    ' krippendorff_alpha; vals is the sorted value list, nc the coincidence
    ' row sums (needed by the ordinal metric).
    function _kripp_delta2(vals, nc, a, b, level)
        if level = "nominal" then
            if a = b then
                return 0
            end if
            return 1
        end if
        if level = "interval" then
            d = vals[a] - vals[b]
            return d * d
        end if
        if level = "ordinal" then
            lo = a
            hi = b
            if b < a then
                lo = b
                hi = a
            end if
            s = 0
            g = lo
            while g <= hi
                s = s + nc[g]
                g = g + 1
            end while
            s = s - (nc[a] + nc[b]) / 2
            return s * s
        end if
        return unknown
    end function

    ' Krippendorff's alpha. data = list of observers, each a list of ratings
    ' over the same units (rows = observers, columns = units); use unknown for
    ' a missing rating. level = "nominal", "interval", or "ordinal". Returns
    ' {alpha} or unknown.
    function krippendorff_alpha(data, level)
        nobs = len(data)
        if nobs < 1 then
            return unknown
        end if
        nunits = len(data[0])
        if nunits < 1 then
            return unknown
        end if
        vals = []
        r = 0
        while r < nobs
            c = 0
            while c < nunits
                v = data[r][c]
                if not is_unknown(v) then
                    if not contains(vals, v) then
                        append(vals, v)
                    end if
                end if
                c = c + 1
            end while
            r = r + 1
        end while
        vals = sort(vals)
        vv = len(vals)
        if vv < 2 then
            return unknown
        end if
        o = []
        a = 0
        while a < vv
            row = []
            b = 0
            while b < vv
                append(row, 0)
                b = b + 1
            end while
            append(o, row)
            a = a + 1
        end while
        c = 0
        while c < nunits
            present = []
            r = 0
            while r < nobs
                v = data[r][c]
                if not is_unknown(v) then
                    append(present, v)
                end if
                r = r + 1
            end while
            mu = len(present)
            if mu >= 2 then
                i = 0
                while i < mu
                    j = 0
                    while j < mu
                        if i != j then
                            ai = _index_in(vals, present[i])
                            bj = _index_in(vals, present[j])
                            o[ai][bj] = o[ai][bj] + 1 / (mu - 1)
                        end if
                        j = j + 1
                    end while
                    i = i + 1
                end while
            end if
            c = c + 1
        end while
        nc = []
        a = 0
        while a < vv
            s = 0
            b = 0
            while b < vv
                s = s + o[a][b]
                b = b + 1
            end while
            append(nc, s)
            a = a + 1
        end while
        ntot = sum(nc)
        if ntot <= 1 then
            return unknown
        end if
        dobs = 0
        dexp = 0
        a = 0
        while a < vv
            b = 0
            while b < vv
                d2 = _kripp_delta2(vals, nc, a, b, level)
                dobs = dobs + o[a][b] * d2
                dexp = dexp + nc[a] * nc[b] * d2
                b = b + 1
            end while
            a = a + 1
        end while
        dexp = dexp / (ntot - 1)
        if dexp <= 0 then
            return unknown
        end if
        return { alpha: 1 - dobs / dexp }
    end function

    ' --- Proportions & the business lens (docs/statistics_scientist_plan.md
    ' §9) --- Pure gBASIC over the standard normal. The proportion z-tests are
    ' verified against statsmodels.stats.proportion.proportions_ztest; the
    ' business conveniences (ab_test/rfm/funnel/cohort_retention) are
    ' deterministic compositions over them and the base builtins.

    ' One-sample proportion z-test. x successes in n trials against null p0.
    ' Test statistic uses the null SE (matching statsmodels prop_var=p0); the
    ' CI is the Wald normal interval on the observed proportion.
    ' Returns {z, p_value, phat, ci_low, ci_high} or unknown.
    function prop_test_1(x, n, p0)
        if n < 1 then
            return unknown
        end if
        if p0 <= 0 then
            return unknown
        end if
        if p0 >= 1 then
            return unknown
        end if
        phat = x / n
        z = (phat - p0) / sqrt(p0 * (1 - p0) / n)
        pv = 2 * (1 - _norm_cdf_std(abs(z)))
        zc = _inv_norm_std(0.975)
        se = sqrt(phat * (1 - phat) / n)
        return { z: z, p_value: pv, phat: phat, ci_low: phat - zc * se, ci_high: phat + zc * se }
    end function

    ' Two-sample proportion z-test (pooled SE for the statistic, unpooled Wald
    ' SE for the CI of the difference p1 - p2). Returns {z, p_value, diff,
    ' ci_low, ci_high} or unknown.
    function prop_test_2(s1, n1, s2, n2)
        if n1 < 1 then
            return unknown
        end if
        if n2 < 1 then
            return unknown
        end if
        p1 = s1 / n1
        p2 = s2 / n2
        diff = p1 - p2
        pp = (s1 + s2) / (n1 + n2)
        denom = pp * (1 - pp) * (1 / n1 + 1 / n2)
        if denom <= 0 then
            return unknown
        end if
        z = diff / sqrt(denom)
        pv = 2 * (1 - _norm_cdf_std(abs(z)))
        zc = _inv_norm_std(0.975)
        se = sqrt(p1 * (1 - p1) / n1 + p2 * (1 - p2) / n2)
        return { z: z, p_value: pv, diff: diff, ci_low: diff - zc * se, ci_high: diff + zc * se }
    end function

    ' A/B test convenience. control and variant are records {successes, n}.
    ' Reports the variant-minus-control absolute difference, the relative lift
    ' over control, the two-sample z-test p-value and 95% CI, and whether it is
    ' significant at 0.05. Returns unknown on bad input.
    function ab_test(control, variant)
        r = prop_test_2(variant.successes, variant.n, control.successes, control.n)
        if is_unknown(r) then
            return unknown
        end if
        pc = control.successes / control.n
        if pc <= 0 then
            return unknown
        end if
        pv = variant.successes / variant.n
        sig = false
        if r.p_value < 0.05 then
            sig = true
        end if
        return { lift: (pv - pc) / pc, diff: r.diff, p_value: r.p_value, ci_low: r.ci_low, ci_high: r.ci_high, significant: sig }
    end function

    ' Score helper for rfm: 1 if v <= lo, 2 if v <= hi, else 3.
    function _rfm_score(v, lo, hi)
        if v <= lo then
            return 1
        end if
        if v <= hi then
            return 2
        end if
        return 3
    end function

    ' RFM analysis. transactions is a list of records {customer, day, amount}
    ' (day is an integer index; larger = more recent). Per customer: recency =
    ' most-recent day counted back from the latest day seen, frequency = number
    ' of transactions, monetary = total amount. Each dimension is scored 1..3
    ' by tertiles across customers (recency inverted so recent = high), and
    ' combined into rfm = r*100 + f*10 + m. Returns a list of records ordered
    ' by customer, or unknown if empty.
    function rfm(transactions)
        nt = len(transactions)
        if nt < 1 then
            return unknown
        end if
        custs = []
        days = []
        i = 0
        while i < nt
            c = transactions[i].customer
            if not contains(custs, c) then
                append(custs, c)
            end if
            append(days, transactions[i].day)
            i = i + 1
        end while
        custs = sort(custs)
        now = max(days)
        recs = []
        freqs = []
        mons = []
        ci = 0
        while ci < len(custs)
            cust = custs[ci]
            lastday = 0 - 1000000000
            fq = 0
            mo = 0
            i = 0
            while i < nt
                if transactions[i].customer = cust then
                    fq = fq + 1
                    mo = mo + transactions[i].amount
                    if transactions[i].day > lastday then
                        lastday = transactions[i].day
                    end if
                end if
                i = i + 1
            end while
            append(recs, now - lastday)
            append(freqs, fq)
            append(mons, mo)
            ci = ci + 1
        end while
        rsorted = recs
        rsorted = sort(rsorted)
        fsorted = freqs
        fsorted = sort(fsorted)
        msorted = mons
        msorted = sort(msorted)
        r33 = quantile(rsorted, 1 / 3)
        r67 = quantile(rsorted, 2 / 3)
        f33 = quantile(fsorted, 1 / 3)
        f67 = quantile(fsorted, 2 / 3)
        m33 = quantile(msorted, 1 / 3)
        m67 = quantile(msorted, 2 / 3)
        out = []
        ci = 0
        while ci < len(custs)
            rs = 4 - _rfm_score(recs[ci], r33, r67)
            fs = _rfm_score(freqs[ci], f33, f67)
            ms = _rfm_score(mons[ci], m33, m67)
            append(out, { customer: custs[ci], recency: recs[ci], frequency: freqs[ci], monetary: mons[ci], r_score: rs, f_score: fs, m_score: ms, rfm: rs * 100 + fs * 10 + ms })
            ci = ci + 1
        end while
        return out
    end function

    ' Conversion funnel. steps is a list of counts at each stage. Returns a
    ' list of records {stage, count, conversion (from previous step),
    ' cumulative (from the first step), dropoff (from previous step)}.
    function funnel(steps)
        ns = len(steps)
        if ns < 1 then
            return unknown
        end if
        first = steps[0]
        out = []
        i = 0
        while i < ns
            prev = steps[i]
            if i > 0 then
                prev = steps[i - 1]
            end if
            conv = 1
            drop = 0
            if i > 0 then
                if prev > 0 then
                    conv = steps[i] / prev
                    drop = (prev - steps[i]) / prev
                else
                    conv = 0
                end if
            end if
            cum = 0
            if first > 0 then
                cum = steps[i] / first
            end if
            append(out, { stage: i, count: steps[i], conversion: conv, cumulative: cum, dropoff: drop })
            i = i + 1
        end while
        return out
    end function

    ' Distinct-customer count over events matching cohort co and period p.
    function _cohort_active(events, co, p)
        seen = []
        i = 0
        while i < len(events)
            if events[i].cohort = co then
                if events[i].period = p then
                    if not contains(seen, events[i].customer) then
                        append(seen, events[i].customer)
                    end if
                end if
            end if
            i = i + 1
        end while
        return len(seen)
    end function

    ' Cohort retention. events is a list of records {customer, cohort, period}
    ' (period 0 = acquisition). For each cohort, size = distinct customers at
    ' period 0 and retention[p] = distinct-active / size across the sorted
    ' periods present. Returns a list of {cohort, size, retention} or unknown.
    function cohort_retention(events)
        ne = len(events)
        if ne < 1 then
            return unknown
        end if
        cohorts = []
        periods = []
        i = 0
        while i < ne
            if not contains(cohorts, events[i].cohort) then
                append(cohorts, events[i].cohort)
            end if
            if not contains(periods, events[i].period) then
                append(periods, events[i].period)
            end if
            i = i + 1
        end while
        cohorts = sort(cohorts)
        periods = sort(periods)
        out = []
        ci = 0
        while ci < len(cohorts)
            co = cohorts[ci]
            size = _cohort_active(events, co, 0)
            ret = []
            pi = 0
            while pi < len(periods)
                active = _cohort_active(events, co, periods[pi])
                if size > 0 then
                    append(ret, active / size)
                else
                    append(ret, 0)
                end if
                pi = pi + 1
            end while
            append(out, { cohort: co, size: size, retention: ret })
            ci = ci + 1
        end while
        return out
    end function

    ' --- Optimizer keystone (docs/statistics_scientist_plan.md §10) ---
    ' Derivative-free Nelder-Mead simplex in pure gBASIC, using function
    ' values (the objective is passed as a value). No new C primitive.
    ' curve_fit rides on it for nonlinear least squares. Verified against
    ' scipy.optimize (Rosenbrock -> (1,1); exponential/logistic fits recover
    ' their true parameters).

    ' Centroid of every simplex vertex except index `skip` (n = dimension;
    ' the simplex has n+1 vertices, so the centroid averages n of them).
    function _nm_centroid(simplex, skip, n)
        c = []
        d = 0
        while d < n
            append(c, 0)
            d = d + 1
        end while
        np1 = len(simplex)
        i = 0
        while i < np1
            if i != skip then
                d = 0
                while d < n
                    c[d] = c[d] + simplex[i][d]
                    d = d + 1
                end while
            end if
            i = i + 1
        end while
        d = 0
        while d < n
            c[d] = c[d] / n
            d = d + 1
        end while
        return c
    end function

    ' Minimize objective(params, ctx) with Nelder-Mead. initial is the starting
    ' parameter list; opts is a record (optional fields max_iter, tol) or
    ' unknown; ctx is arbitrary user data forwarded to every objective call (or
    ' unknown). Returns {params, value, iterations, converged} or unknown.
    function optimize(objective, initial, opts, ctx)
        n = len(initial)
        if n < 1 then
            return unknown
        end if
        maxiter = 400 * n
        tol = pow(10, -12)
        astep = 0.05
        if not is_unknown(opts) then
            ok = keys(opts)
            if contains(ok, "max_iter") then
                maxiter = opts.max_iter
            end if
            if contains(ok, "tol") then
                tol = opts.tol
            end if
        end if
        simplex = []
        fvals = []
        v = initial
        append(simplex, v)
        append(fvals, objective(v, ctx))
        i = 0
        while i < n
            v = initial
            if v[i] != 0 then
                v[i] = v[i] * (1 + astep)
            else
                v[i] = astep
            end if
            append(simplex, v)
            append(fvals, objective(v, ctx))
            i = i + 1
        end while
        np1 = n + 1
        iter = 0
        converged = false
        while iter < maxiter
            lo = 0
            hi = 0
            j = 1
            while j < np1
                if fvals[j] < fvals[lo] then
                    lo = j
                end if
                if fvals[j] > fvals[hi] then
                    hi = j
                end if
                j = j + 1
            end while
            hi2 = lo
            j = 0
            while j < np1
                if j != hi then
                    if fvals[j] > fvals[hi2] then
                        hi2 = j
                    end if
                end if
                j = j + 1
            end while
            if abs(fvals[hi] - fvals[lo]) <= tol * (abs(fvals[lo]) + tol) then
                converged = true
                break
            end if
            cen = _nm_centroid(simplex, hi, n)
            xr = []
            d = 0
            while d < n
                append(xr, cen[d] + (cen[d] - simplex[hi][d]))
                d = d + 1
            end while
            fr = objective(xr, ctx)
            if fr < fvals[lo] then
                xe = []
                d = 0
                while d < n
                    append(xe, cen[d] + 2 * (xr[d] - cen[d]))
                    d = d + 1
                end while
                fe = objective(xe, ctx)
                if fe < fr then
                    simplex[hi] = xe
                    fvals[hi] = fe
                else
                    simplex[hi] = xr
                    fvals[hi] = fr
                end if
            else
                if fr < fvals[hi2] then
                    simplex[hi] = xr
                    fvals[hi] = fr
                else
                    xc = []
                    d = 0
                    while d < n
                        append(xc, cen[d] + 0.5 * (simplex[hi][d] - cen[d]))
                        d = d + 1
                    end while
                    fc = objective(xc, ctx)
                    if fc < fvals[hi] then
                        simplex[hi] = xc
                        fvals[hi] = fc
                    else
                        i = 0
                        while i < np1
                            if i != lo then
                                d = 0
                                while d < n
                                    simplex[i][d] = simplex[lo][d] + 0.5 * (simplex[i][d] - simplex[lo][d])
                                    d = d + 1
                                end while
                                fvals[i] = objective(simplex[i], ctx)
                            end if
                            i = i + 1
                        end while
                    end if
                end if
            end if
            iter = iter + 1
        end while
        lo = 0
        j = 1
        while j < np1
            if fvals[j] < fvals[lo] then
                lo = j
            end if
            j = j + 1
        end while
        return { params: simplex[lo], value: fvals[lo], iterations: iter, converged: converged }
    end function

    ' Sum of squared residuals objective for curve_fit; ctx = {f, xs, ys}.
    function _curve_sse(params, ctx)
        f = ctx.f
        xs = ctx.xs
        ys = ctx.ys
        total = 0
        i = 0
        while i < len(xs)
            d = ys[i] - f(xs[i], params)
            total = total + d * d
            i = i + 1
        end while
        return total
    end function

    ' Nonlinear least-squares fit. f is a model function value f(x, params);
    ' xs/ys are the data; initial is the starting parameter list. Minimizes the
    ' SSE via optimize. Returns {params, residuals, sse, r_squared, iterations,
    ' converged} or unknown.
    function curve_fit(f, xs, ys, initial)
        n = len(xs)
        if n < 1 then
            return unknown
        end if
        if len(ys) != n then
            return unknown
        end if
        ctx = { f: f, xs: xs, ys: ys }
        res = optimize(_curve_sse, initial, unknown, ctx)
        if is_unknown(res) then
            return unknown
        end if
        params = res.params
        ybar = mean(ys)
        sse = 0
        sst = 0
        resid = []
        i = 0
        while i < n
            pred = f(xs[i], params)
            d = ys[i] - pred
            append(resid, d)
            sse = sse + d * d
            e = ys[i] - ybar
            sst = sst + e * e
            i = i + 1
        end while
        r2 = 1
        if sst > 0 then
            r2 = 1 - sse / sst
        end if
        return { params: params, residuals: resid, sse: sse, r_squared: r2, iterations: res.iterations, converged: res.converged }
    end function

    ' --- ARIMA-family time-series modeling (unblocked by the optimizer;
    ' docs/statistics_scientist_plan.md §10 follow-on) --- The autoregressive
    ' and differencing pieces are exact conditional MLE and match statsmodels
    ' AutoReg to machine precision; the moving-average piece uses conditional
    ' least squares (CSS) via `optimize` (statsmodels' exact state-space MLE
    ' would need a Kalman filter — a future increment). All pure gBASIC.

    ' Fit an AR(p) model with intercept by OLS (= conditional MLE), reusing
    ' matrix.bas. Returns {const, phi (list of p), sigma2, aic, bic, llf, n}
    ' where n is the effective sample size (len(xs) - p). Matches statsmodels
    ' AutoReg(trend='c'). Returns unknown on bad input.
    function ar_fit(xs, p)
        n = len(xs)
        if p < 1 then
            return unknown
        end if
        if n < p + 2 then
            return unknown
        end if
        bigx = []
        y = []
        t = p
        while t < n
            row = [1]
            j = 0
            while j < p
                append(row, xs[t - 1 - j])
                j = j + 1
            end while
            append(bigx, row)
            append(y, xs[t])
            t = t + 1
        end while
        xt = mat_transpose(bigx)
        xtxinv = mat_inverse(mat_mul(xt, bigx))
        beta = mat_vec(xtxinv, mat_vec(xt, y))
        ne = len(y)
        fitted = mat_vec(bigx, beta)
        sse = 0
        i = 0
        while i < ne
            r = y[i] - fitted[i]
            sse = sse + r * r
            i = i + 1
        end while
        sigma2 = sse / ne
        llf = -0.5 * ne * (log(2 * _pi()) + log(sigma2) + 1)
        kparams = p + 2
        aic = -2 * llf + 2 * kparams
        bic = -2 * llf + log(ne) * kparams
        phi = []
        i = 1
        while i <= p
            append(phi, beta[i])
            i = i + 1
        end while
        return { const: beta[0], phi: phi, sigma2: sigma2, aic: aic, bic: bic, llf: llf, n: ne }
    end function

    ' Recursive h-step forecast from an AR model (any record with .const and a
    ' .phi list), seeded by the tail of xs. Returns a list of h values.
    function ar_forecast(model, xs, h)
        p = len(model.phi)
        n = len(xs)
        if n < p then
            return unknown
        end if
        hist = []
        i = 0
        while i < n
            append(hist, xs[i])
            i = i + 1
        end while
        out = []
        s = 0
        while s < h
            pred = model.const
            j = 0
            while j < p
                pred = pred + model.phi[j] * hist[len(hist) - 1 - j]
                j = j + 1
            end while
            append(out, pred)
            append(hist, pred)
            s = s + 1
        end while
        return out
    end function

    ' Conditional-sum-of-squares objective for arma_css_fit. ctx = {xs, p, q};
    ' params = [const, phi_1..phi_p, theta_1..theta_q].
    function _arma_css_obj(params, ctx)
        xs = ctx.xs
        p = ctx.p
        q = ctx.q
        n = len(xs)
        e = []
        i = 0
        while i < n
            append(e, 0)
            i = i + 1
        end while
        start = p
        if q > start then
            start = q
        end if
        sse = 0
        t = start
        while t < n
            pred = params[0]
            j = 0
            while j < p
                pred = pred + params[1 + j] * xs[t - 1 - j]
                j = j + 1
            end while
            j = 0
            while j < q
                pred = pred + params[1 + p + j] * e[t - 1 - j]
                j = j + 1
            end while
            e[t] = xs[t] - pred
            sse = sse + e[t] * e[t]
            t = t + 1
        end while
        return sse
    end function

    ' Fit ARMA(p, q) by conditional least squares via `optimize`. Returns
    ' {const, phi (list), theta (list), sse, iterations, converged} or unknown.
    ' NOTE: CSS is best-effort and, unlike ar_fit, does not match statsmodels'
    ' exact state-space MLE; moving-average terms need a long series to be well
    ' identified.
    function arma_css_fit(xs, p, q)
        n = len(xs)
        if n < p + q + 2 then
            return unknown
        end if
        ctx = { xs: xs, p: p, q: q }
        init = []
        append(init, mean(xs) * 0.4)
        i = 0
        while i < p + q
            append(init, 0.3)
            i = i + 1
        end while
        res = optimize(_arma_css_obj, init, { max_iter: 20000, tol: pow(10, -12) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        pr = res.params
        phi = []
        i = 0
        while i < p
            append(phi, pr[1 + i])
            i = i + 1
        end while
        theta = []
        i = 0
        while i < q
            append(theta, pr[1 + p + i])
            i = i + 1
        end while
        return { const: pr[0], phi: phi, theta: theta, sse: res.value, iterations: res.iterations, converged: res.converged }
    end function

    ' Solve the discrete Lyapunov equation P = T P T' + RRt for the stationary
    ' initial state covariance, via vec(P) = (I - T⊗T)^-1 vec(RRt) (row-major
    ' vec, matching numpy). Returns the r×r P0 or unknown if singular.
    function _arma_lyap(tmat, rrt, r)
        r2 = r * r
        m = []
        rhs = []
        a = 0
        while a < r2
            row = []
            b = 0
            while b < r2
                append(row, 0)
                b = b + 1
            end while
            append(m, row)
            append(rhs, 0)
            a = a + 1
        end while
        i = 0
        while i < r
            k = 0
            while k < r
                mm = i * r + k
                rhs[mm] = rrt[i][k]
                j = 0
                while j < r
                    l = 0
                    while l < r
                        nn = j * r + l
                        val = tmat[i][j] * tmat[k][l]
                        if mm = nn then
                            m[mm][nn] = 1 - val
                        else
                            m[mm][nn] = 0 - val
                        end if
                        l = l + 1
                    end while
                    j = j + 1
                end while
                k = k + 1
            end while
            i = i + 1
        end while
        minv = mat_inverse(m)
        if is_unknown(minv) then
            return unknown
        end if
        x = mat_vec(minv, rhs)
        p0 = []
        i = 0
        while i < r
            row = []
            k = 0
            while k < r
                append(row, x[i * r + k])
                k = k + 1
            end while
            append(p0, row)
            i = i + 1
        end while
        return p0
    end function

    ' Exact Gaussian log-likelihood of ARMA(p, q) via the Kalman filter over the
    ' Harvey state-space form, with sigma2 concentrated out. ctx = {y, p, q};
    ' params = [mu, phi_1..phi_p, theta_1..theta_q] (mu = process mean). Returns
    ' {llf, sigma2} or unknown (non-stationary / degenerate parameters).
    function _arma_ll(params, ctx)
        y = ctx.y
        p = ctx.p
        q = ctx.q
        mu = params[0]
        r = p
        if q + 1 > r then
            r = q + 1
        end if
        tmat = []
        i = 0
        while i < r
            row = []
            j = 0
            while j < r
                append(row, 0)
                j = j + 1
            end while
            append(tmat, row)
            i = i + 1
        end while
        i = 0
        while i < r
            if i < p then
                tmat[i][0] = params[1 + i]
            end if
            i = i + 1
        end while
        i = 0
        while i < r - 1
            tmat[i][i + 1] = 1
            i = i + 1
        end while
        rvec = []
        i = 0
        while i < r
            append(rvec, 0)
            i = i + 1
        end while
        rvec[0] = 1
        i = 0
        while i < q
            rvec[1 + i] = params[1 + p + i]
            i = i + 1
        end while
        rrt = []
        i = 0
        while i < r
            row = []
            j = 0
            while j < r
                append(row, rvec[i] * rvec[j])
                j = j + 1
            end while
            append(rrt, row)
            i = i + 1
        end while
        bigp = _arma_lyap(tmat, rrt, r)
        if is_unknown(bigp) then
            return unknown
        end if
        a = []
        i = 0
        while i < r
            append(a, 0)
            i = i + 1
        end while
        n = len(y)
        ssum = 0
        ldet = 0
        t = 0
        while t < n
            v = (y[t] - mu) - a[0]
            f = bigp[0][0]
            if f <= 0 then
                return unknown
            end if
            ssum = ssum + v * v / f
            ldet = ldet + log(f)
            kvec = []
            i = 0
            while i < r
                append(kvec, bigp[i][0] / f)
                i = i + 1
            end while
            i = 0
            while i < r
                a[i] = a[i] + kvec[i] * v
                i = i + 1
            end while
            p0 = []
            j = 0
            while j < r
                append(p0, bigp[0][j])
                j = j + 1
            end while
            i = 0
            while i < r
                j = 0
                while j < r
                    bigp[i][j] = bigp[i][j] - kvec[i] * p0[j]
                    j = j + 1
                end while
                i = i + 1
            end while
            na = []
            i = 0
            while i < r
                s = 0
                j = 0
                while j < r
                    s = s + tmat[i][j] * a[j]
                    j = j + 1
                end while
                append(na, s)
                i = i + 1
            end while
            a = na
            tp = []
            i = 0
            while i < r
                row = []
                j = 0
                while j < r
                    s = 0
                    kk = 0
                    while kk < r
                        s = s + tmat[i][kk] * bigp[kk][j]
                        kk = kk + 1
                    end while
                    append(row, s)
                    j = j + 1
                end while
                append(tp, row)
                i = i + 1
            end while
            newp = []
            i = 0
            while i < r
                row = []
                j = 0
                while j < r
                    s = 0
                    kk = 0
                    while kk < r
                        s = s + tp[i][kk] * tmat[j][kk]
                        kk = kk + 1
                    end while
                    append(row, s + rrt[i][j])
                    j = j + 1
                end while
                append(newp, row)
                i = i + 1
            end while
            bigp = newp
            t = t + 1
        end while
        sigma2 = ssum / n
        llf = -0.5 * (n * log(2 * _pi()) + n * log(sigma2) + n + ldet)
        ' a is now the one-step-ahead predicted state a_{n|n-1}, the seed for
        ' forecasting; tmat/r let the caller propagate it forward.
        return { llf: llf, sigma2: sigma2, astate: a, tmat: tmat, r: r }
    end function

    ' Negative-log-likelihood objective for arma_fit (optimize minimizes).
    function _arma_negll(params, ctx)
        res = _arma_ll(params, ctx)
        if is_unknown(res) then
            return pow(10, 12)
        end if
        return 0 - res.llf
    end function

    ' Fit ARMA(p, q) by EXACT Gaussian maximum likelihood (Kalman filter) via
    ' `optimize`. Returns {const (= process mean), phi, theta, sigma2, llf, aic,
    ' bic} or unknown. Matches statsmodels ARIMA(order=(p,0,q), trend='c').
    function arma_fit(xs, p, q)
        n = len(xs)
        if n < p + q + 3 then
            return unknown
        end if
        ctx = { y: xs, p: p, q: q }
        init = []
        append(init, mean(xs))
        i = 0
        while i < p + q
            append(init, 0.1)
            i = i + 1
        end while
        res = optimize(_arma_negll, init, { max_iter: 4000, tol: pow(10, -11) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        pr = res.params
        ll = _arma_ll(pr, ctx)
        if is_unknown(ll) then
            return unknown
        end if
        phi = []
        i = 0
        while i < p
            append(phi, pr[1 + i])
            i = i + 1
        end while
        theta = []
        i = 0
        while i < q
            append(theta, pr[1 + p + i])
            i = i + 1
        end while
        kpar = p + q + 2
        aic = -2 * ll.llf + 2 * kpar
        bic = -2 * ll.llf + log(n) * kpar
        return { const: pr[0], phi: phi, theta: theta, sigma2: ll.sigma2, llf: ll.llf, aic: aic, bic: bic }
    end function

    ' Fit an ARIMA(p, d, q) model: difference d times, then fit AR(p) by OLS
    ' when q = 0 (exact) or ARMA(p, q) by exact Kalman MLE otherwise. Returns a
    ' record {p, d, q, const, phi, theta, sigma2, aic, bic, ...}. Returns
    ' unknown on bad input.
    function arima_fit(xs, p, d, q)
        series = xs
        if d > 0 then
            series = diff(xs, d)
        end if
        if q = 0 then
            m = ar_fit(series, p)
            if is_unknown(m) then
                return unknown
            end if
            return { p: p, d: d, q: q, const: m.const, phi: m.phi, theta: [], sigma2: m.sigma2, aic: m.aic, bic: m.bic }
        end if
        m = arma_fit(series, p, q)
        if is_unknown(m) then
            return unknown
        end if
        return { p: p, d: d, q: q, const: m.const, phi: m.phi, theta: m.theta, sigma2: m.sigma2, aic: m.aic, bic: m.bic }
    end function

    ' Forecast h steps from a stationary ARMA model (record with .const = mean,
    ' .phi and .theta lists). Runs the Kalman filter over xs to the terminal
    ' predicted state, then propagates it forward with the transition matrix
    ' (future innovations have zero expectation). Matches statsmodels
    ' ARIMA.forecast. Returns a list of h values, or unknown.
    function arma_forecast(model, xs, h)
        p = len(model.phi)
        q = len(model.theta)
        params = []
        append(params, model.const)
        i = 0
        while i < p
            append(params, model.phi[i])
            i = i + 1
        end while
        i = 0
        while i < q
            append(params, model.theta[i])
            i = i + 1
        end while
        ctx = { y: xs, p: p, q: q }
        res = _arma_ll(params, ctx)
        if is_unknown(res) then
            return unknown
        end if
        a = res.astate
        tmat = res.tmat
        r = res.r
        mu = model.const
        out = []
        s = 0
        while s < h
            append(out, mu + a[0])
            na = []
            i = 0
            while i < r
                acc = 0
                j = 0
                while j < r
                    acc = acc + tmat[i][j] * a[j]
                    j = j + 1
                end while
                append(na, acc)
                i = i + 1
            end while
            a = na
            s = s + 1
        end while
        return out
    end function

    ' Forecast h steps from an arima_fit model, for d in {0, 1} and any q. The
    ' stationary series is forecast by the AR recursion (q = 0) or the ARMA
    ' state-space (q > 0); for d = 1 the differenced-series forecasts are
    ' integrated from the last observed level. Returns a list of h values, or
    ' unknown for the unsupported d > 1 case.
    function arima_forecast(model, xs, h)
        if model.d = 0 then
            if len(model.theta) > 0 then
                return arma_forecast(model, xs, h)
            end if
            arm = { const: model.const, phi: model.phi }
            return ar_forecast(arm, xs, h)
        end if
        if model.d != 1 then
            return unknown
        end if
        dser = diff(xs, 1)
        if len(model.theta) > 0 then
            dfc = arma_forecast(model, dser, h)
        else
            arm = { const: model.const, phi: model.phi }
            dfc = ar_forecast(arm, dser, h)
        end if
        if is_unknown(dfc) then
            return unknown
        end if
        out = []
        last = xs[len(xs) - 1]
        i = 0
        while i < h
            last = last + dfc[i]
            append(out, last)
            i = i + 1
        end while
        return out
    end function

    ' --- GARCH(1,1) volatility model (optimizer follow-on) --- Gaussian MLE of
    ' a constant-mean GARCH(1,1) via `optimize`. Matches the `arch` package
    ' (Sheppard) to ~3 decimals, including its exponentially-weighted backcast
    ' initialization of the conditional variance. Pure gBASIC.

    ' arch-style variance backcast: normalized 0.94^i weighting of the first
    ' min(75, n) squared residuals.
    function _garch_backcast(resid)
        n = len(resid)
        tau = 75
        if n < tau then
            tau = n
        end if
        wsum = 0
        acc = 0
        i = 0
        while i < tau
            w = pow(0.94, i)
            wsum = wsum + w
            acc = acc + w * resid[i] * resid[i]
            i = i + 1
        end while
        return acc / wsum
    end function

    ' Negative Gaussian log-likelihood of a constant-mean GARCH(1,1).
    ' ctx = {r}; params = [mu, omega, alpha, beta]. Non-admissible parameters
    ' (omega<=0, alpha<0, beta<0, alpha+beta>=1) return a large penalty.
    function _garch_negll(params, ctx)
        r = ctx.r
        mu = params[0]
        omega = params[1]
        alpha = params[2]
        beta = params[3]
        if omega <= 0 then
            return pow(10, 12)
        end if
        if alpha < 0 then
            return pow(10, 12)
        end if
        if beta < 0 then
            return pow(10, 12)
        end if
        if alpha + beta >= 1 then
            return pow(10, 12)
        end if
        n = len(r)
        resid = []
        i = 0
        while i < n
            append(resid, r[i] - mu)
            i = i + 1
        end while
        bc = _garch_backcast(resid)
        h = omega + alpha * bc + beta * bc
        ll = 0
        t = 0
        while t < n
            if t > 0 then
                h = omega + alpha * resid[t - 1] * resid[t - 1] + beta * h
            end if
            if h <= 0 then
                return pow(10, 12)
            end if
            ll = ll - 0.5 * (log(2 * _pi()) + log(h) + resid[t] * resid[t] / h)
            t = t + 1
        end while
        return 0 - ll
    end function

    ' Fit a constant-mean GARCH(1,1) by Gaussian MLE. r is a return series.
    ' Returns {mu, omega, alpha, beta, persistence (alpha+beta), llf, aic, bic}
    ' or unknown. Matches the `arch` package to ~3 decimals.
    function garch_fit(r)
        n = len(r)
        if n < 8 then
            return unknown
        end if
        ctx = { r: r }
        init = [mean(r), 0.1, 0.1, 0.8]
        res = optimize(_garch_negll, init, { max_iter: 8000, tol: pow(10, -10) }, ctx)
        if is_unknown(res) then
            return unknown
        end if
        pr = res.params
        llf = 0 - res.value
        kpar = 4
        aic = -2 * llf + 2 * kpar
        bic = -2 * llf + log(n) * kpar
        return { mu: pr[0], omega: pr[1], alpha: pr[2], beta: pr[3], persistence: pr[2] + pr[3], llf: llf, aic: aic, bic: bic }
    end function

    ' ================================================================
    ' Econometric diagnostics & finance metrics
    '
    ' The tests below are the gate-keepers of applied time-series work:
    ' before an ARIMA/GARCH model is trusted, the series is checked for a
    ' unit root (adf_test), the residuals for leftover autocorrelation
    ' (ljung_box, durbin_watson) and for conditional heteroskedasticity
    ' (arch_lm, breusch_pagan), and inference is made robust to serial
    ' correlation (newey_west). All statistics match statsmodels.
    ' ================================================================

    ' Plain OLS over a prebuilt design matrix (rows already include any
    ' intercept/trend columns). Returns the pieces the diagnostics need:
    ' beta, (X'X)^-1, residuals, rss. unknown if singular or under-determined.
    function _ols_design(bigx, y)
        n = len(bigx)
        if n = 0 then
            return unknown
        end if
        k = len(bigx[0])
        if n <= k then
            return unknown
        end if
        xt = mat_transpose(bigx)
        xtxinv = mat_inverse(mat_mul(xt, bigx))
        if is_unknown(xtxinv) then
            return unknown
        end if
        beta = mat_vec(xtxinv, mat_vec(xt, y))
        fitted = mat_vec(bigx, beta)
        resid = []
        rss = 0
        i = 0
        while i < n
            e = y[i] - fitted[i]
            append(resid, e)
            rss = rss + e * e
            i = i + 1
        end while
        return { beta: beta, xtxinv: xtxinv, resid: resid, fitted: fitted, rss: rss, n: n, k: k }
    end function

    ' Durbin-Watson statistic for a residual series: sum of squared first
    ' differences over the residual sum of squares. ~2 means no first-order
    ' autocorrelation; <2 positive, >2 negative. Matches
    ' statsmodels.stats.stattools.durbin_watson.
    function durbin_watson(resid)
        n = len(resid)
        if n < 2 then
            return unknown
        end if
        num = 0
        den = resid[0] * resid[0]
        i = 1
        while i < n
            d = resid[i] - resid[i - 1]
            num = num + d * d
            den = den + resid[i] * resid[i]
            i = i + 1
        end while
        if den = 0 then
            return unknown
        end if
        return num / den
    end function

    ' Ljung-Box portmanteau test for autocorrelation up to `lags`:
    ' Q = n(n+2) sum_{k=1..h} r_k^2 / (n-k), r_k the biased acf. Q ~ chi2(h)
    ' under the no-autocorrelation null. Matches
    ' statsmodels.stats.diagnostic.acorr_ljungbox (default settings).
    function ljung_box(xs, lags)
        n = len(xs)
        if lags < 1 then
            return unknown
        end if
        if lags >= n then
            return unknown
        end if
        r = acf(xs, lags)
        if is_unknown(r) then
            return unknown
        end if
        q = 0
        k = 1
        while k <= lags
            q = q + r[k] * r[k] / (n - k)
            k = k + 1
        end while
        q = n * (n + 2) * q
        return { statistic: q, df: lags, p_value: 1 - chi2_cdf(q, lags) }
    end function

    ' Engle's ARCH-LM test for conditional heteroskedasticity: regress the
    ' squared residuals on `lags` of their own past (plus a constant); the
    ' LM statistic n*R^2 ~ chi2(lags). Run this on model residuals before
    ' reaching for garch_fit. Matches statsmodels het_arch (LM form).
    function arch_lm(resid, lags)
        n = len(resid)
        if lags < 1 then
            return unknown
        end if
        if n <= 2 * lags + 1 then
            return unknown
        end if
        e2 = []
        i = 0
        while i < n
            append(e2, resid[i] * resid[i])
            i = i + 1
        end while
        nobs = n - lags
        dep = []
        bigx = []
        r = 0
        while r < nobs
            t = lags + r
            append(dep, e2[t])
            row = [1]
            j = 1
            while j <= lags
                append(row, e2[t - j])
                j = j + 1
            end while
            append(bigx, row)
            r = r + 1
        end while
        fit = _ols_design(bigx, dep)
        if is_unknown(fit) then
            return unknown
        end if
        dbar = mean(dep)
        tss = 0
        i = 0
        while i < nobs
            d = dep[i] - dbar
            tss = tss + d * d
            i = i + 1
        end while
        if tss = 0 then
            return unknown
        end if
        r2 = 1 - fit.rss / tss
        lm = nobs * r2
        return { statistic: lm, df: lags, p_value: 1 - chi2_cdf(lm, lags) }
    end function

    ' Breusch-Pagan test (Koenker studentized form) for heteroskedasticity:
    ' regress squared residuals on the model's predictors; LM = n*R^2 ~
    ' chi2(#predictors). `xs` is the same predictor list passed to ols.
    ' Matches statsmodels het_breuschpagan (the n*R^2 LM statistic).
    function breusch_pagan(resid, xs)
        n = len(resid)
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        e2 = []
        i = 0
        while i < n
            append(e2, resid[i] * resid[i])
            i = i + 1
        end while
        bigx = _design(cols, n)
        fit = _ols_design(bigx, e2)
        if is_unknown(fit) then
            return unknown
        end if
        ebar = mean(e2)
        tss = 0
        i = 0
        while i < n
            d = e2[i] - ebar
            tss = tss + d * d
            i = i + 1
        end while
        if tss = 0 then
            return unknown
        end if
        df = len(cols)
        lm = n * (1 - fit.rss / tss)
        return { statistic: lm, df: df, p_value: 1 - chi2_cdf(lm, df) }
    end function

    ' MacKinnon (1994) approximate p-value for an ADF t-statistic; trend is
    ' "n" (none), "c" (constant), or "ct" (constant + trend). Ported from
    ' statsmodels' mackinnonp coefficient tables (N=1).
    function _mackinnonp(stat, trend)
        maxs = 0
        mins = 0
        star = 0
        small = []
        large = []
        if trend = "n" then
            maxs = pow(10, 30)
            mins = -19.04
            star = -1.04
            small = [0.6344, 1.2378, 0.032496]
            large = [0.4797, 0.93557, -0.06999, 0.033066]
        end if
        if trend = "c" then
            maxs = 2.74
            mins = -18.83
            star = -1.61
            small = [2.1659, 1.4412, 0.038269]
            large = [1.7339, 0.93202, -0.12745, -0.010368]
        end if
        if trend = "ct" then
            maxs = 0.7
            mins = -16.18
            star = -2.89
            small = [3.2512, 1.6047, 0.049588]
            large = [2.5261, 0.61654, -0.37956, -0.060285]
        end if
        if len(small) = 0 then
            return unknown
        end if
        if stat > maxs then
            return 1
        end if
        if stat < mins then
            return 0
        end if
        coef = large
        if stat <= star then
            coef = small
        end if
        val = 0
        xp = 1
        p = 0
        while p < len(coef)
            val = val + coef[p] * xp
            xp = xp * stat
            p = p + 1
        end while
        return _norm_cdf_std(val)
    end function

    ' MacKinnon (2010) critical value for the ADF test at level index idx
    ' (0=1%, 1=5%, 2=10%) given the regression trend and sample size nobs.
    ' crit = b0 + b1/T + b2/T^2 + b3/T^3.
    function _mackinnoncrit(trend, nobs, idx)
        rows = []
        if trend = "n" then
            rows = [[-2.56574, -2.2358, -3.627, 0], [-1.941, -0.2686, -3.365, 31.223], [-1.61682, 0.2656, -2.714, 25.364]]
        end if
        if trend = "c" then
            rows = [[-3.43035, -6.5393, -16.786, -79.433], [-2.86154, -2.8903, -4.234, -40.04], [-2.56677, -1.5384, -2.809, 0]]
        end if
        if trend = "ct" then
            rows = [[-3.95877, -9.0531, -28.428, -134.155], [-3.41049, -4.3904, -9.036, -45.374], [-3.12705, -2.5856, -3.925, -22.38]]
        end if
        if len(rows) = 0 then
            return unknown
        end if
        b = rows[idx]
        t = nobs
        return b[0] + b[1] / t + b[2] / (t * t) + b[3] / (t * t * t)
    end function

    ' Augmented Dickey-Fuller unit-root test. Regresses the first difference
    ' on the lagged level (the gamma coefficient tested), `lags` lagged
    ' differences, and the trend terms selected by `trend` ("n"/"c"/"ct").
    ' A statistic below the critical value (small p) rejects the unit-root
    ' null -> the series is stationary. Statistic, p-value and critical
    ' values match statsmodels.tsa.stattools.adfuller(autolag=None).
    function adf_test(xs, lags, trend)
        n = len(xs)
        if lags < 0 then
            return unknown
        end if
        dy = diff(xs, 1)
        m = len(dy)
        nobs = m - lags
        if nobs < 3 then
            return unknown
        end if
        dep = []
        bigx = []
        r = 0
        while r < nobs
            t = lags + r
            row = [xs[lags + r]]
            append(dep, dy[t])
            j = 1
            while j <= lags
                append(row, dy[t - j])
                j = j + 1
            end while
            if trend = "c" then
                append(row, 1)
            end if
            if trend = "ct" then
                append(row, 1)
                append(row, r + 1)
            end if
            append(bigx, row)
            r = r + 1
        end while
        fit = _ols_design(bigx, dep)
        if is_unknown(fit) then
            return unknown
        end if
        dof = nobs - fit.k
        if dof <= 0 then
            return unknown
        end if
        sigma2 = fit.rss / dof
        v = sigma2 * fit.xtxinv[0][0]
        if v <= 0 then
            return unknown
        end if
        tstat = fit.beta[0] / sqrt(v)
        return { statistic: tstat, p_value: _mackinnonp(tstat, trend), lags: lags, nobs: nobs, trend: trend, crit_1: _mackinnoncrit(trend, nobs, 0), crit_5: _mackinnoncrit(trend, nobs, 1), crit_10: _mackinnoncrit(trend, nobs, 2) }
    end function

    ' OLS with Newey-West HAC standard errors (heteroskedasticity- and
    ' autocorrelation-consistent). Bartlett kernel, weights 1 - l/(L+1) for
    ' lag l up to maxlags. Use when regressing on time-series data where
    ' residuals may be serially correlated. cov, SEs and z-tests match
    ' statsmodels OLS(...).fit(cov_type="HAC", maxlags=L, use_correction=False).
    function newey_west(y, xs, maxlags)
        n = len(y)
        cols = _norm_cols(xs, n)
        if is_unknown(cols) then
            return unknown
        end if
        p = len(cols) + 1
        if n <= p then
            return unknown
        end if
        bigx = _design(cols, n)
        fit = _ols_design(bigx, y)
        if is_unknown(fit) then
            return unknown
        end if
        e = fit.resid
        meat = []
        a = 0
        while a < p
            row = []
            b = 0
            while b < p
                append(row, 0)
                b = b + 1
            end while
            append(meat, row)
            a = a + 1
        end while
        ' Gamma_0
        i = 0
        while i < n
            a = 0
            while a < p
                b = 0
                while b < p
                    meat[a][b] = meat[a][b] + bigx[i][a] * bigx[i][b] * e[i] * e[i]
                    b = b + 1
                end while
                a = a + 1
            end while
            i = i + 1
        end while
        ' Weighted lagged autocovariances Gamma_l + Gamma_l'
        l = 1
        while l <= maxlags
            w = 1 - l / (maxlags + 1)
            t = l
            while t < n
                a = 0
                while a < p
                    b = 0
                    while b < p
                        meat[a][b] = meat[a][b] + w * (bigx[t][a] * bigx[t - l][b] + bigx[t - l][a] * bigx[t][b]) * e[t] * e[t - l]
                        b = b + 1
                    end while
                    a = a + 1
                end while
                t = t + 1
            end while
            l = l + 1
        end while
        xtxinv = fit.xtxinv
        cov = mat_mul(mat_mul(xtxinv, meat), xtxinv)
        beta = fit.beta
        ses = []
        zvals = []
        pvals = []
        j = 0
        while j < p
            vv = cov[j][j]
            se = unknown
            zv = unknown
            pv = unknown
            if vv >= 0 then
                se = sqrt(vv)
                if se > 0 then
                    zv = beta[j] / se
                    pv = 2 * (1 - _norm_cdf_std(abs(zv)))
                end if
            end if
            append(ses, se)
            append(zvals, zv)
            append(pvals, pv)
            j = j + 1
        end while
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, cov: cov, cov_type: "HAC", maxlags: maxlags, fitted: fit.fitted, residuals: e, n: n, df: n - p }
    end function

    ' --- Finance return metrics ---

    ' Simple (arithmetic) returns from a price series: r_t = P_t/P_{t-1} - 1.
    ' Returns a list of length n-1; unknown on a zero price or too few points.
    function simple_returns(prices)
        n = len(prices)
        if n < 2 then
            return unknown
        end if
        out = []
        i = 1
        while i < n
            if prices[i - 1] = 0 then
                return unknown
            end if
            append(out, prices[i] / prices[i - 1] - 1)
            i = i + 1
        end while
        return out
    end function

    ' Log (continuously compounded) returns: r_t = ln(P_t/P_{t-1}).
    function log_returns(prices)
        n = len(prices)
        if n < 2 then
            return unknown
        end if
        out = []
        i = 1
        while i < n
            if prices[i - 1] <= 0 then
                return unknown
            end if
            if prices[i] <= 0 then
                return unknown
            end if
            append(out, log(prices[i] / prices[i - 1]))
            i = i + 1
        end while
        return out
    end function

    ' Total compounded return from a series of simple returns:
    ' prod(1 + r) - 1.
    function cumulative_return(rets)
        n = len(rets)
        if n < 1 then
            return unknown
        end if
        acc = 1
        i = 0
        while i < n
            acc = acc * (1 + rets[i])
            i = i + 1
        end while
        return acc - 1
    end function

    ' Annualized Sharpe ratio: mean excess return over its (sample) standard
    ' deviation, scaled by sqrt(periods) (e.g. 252 daily, 12 monthly). `rf`
    ' is the per-period risk-free rate.
    function sharpe_ratio(rets, rf, periods)
        n = len(rets)
        if n < 2 then
            return unknown
        end if
        ex = []
        i = 0
        while i < n
            append(ex, rets[i] - rf)
            i = i + 1
        end while
        sd = stdev(ex)
        if sd <= 0 then
            return unknown
        end if
        return mean(ex) / sd * sqrt(periods)
    end function

    ' Annualized Sortino ratio: like Sharpe but the denominator is downside
    ' deviation, sqrt(mean(min(0, excess)^2)) over all n periods (target =
    ' rf). Penalizes only harmful volatility.
    function sortino_ratio(rets, rf, periods)
        n = len(rets)
        if n < 2 then
            return unknown
        end if
        tot = 0
        dsum = 0
        i = 0
        while i < n
            ex = rets[i] - rf
            tot = tot + ex
            if ex < 0 then
                dsum = dsum + ex * ex
            end if
            i = i + 1
        end while
        dd = sqrt(dsum / n)
        if dd <= 0 then
            return unknown
        end if
        return (tot / n) / dd * sqrt(periods)
    end function

    ' Maximum drawdown of a price (or cumulative-value) series: the largest
    ' peak-to-trough fractional decline, returned as a NEGATIVE number, with
    ' the indices of the peak and trough that produced it.
    function max_drawdown(prices)
        n = len(prices)
        if n < 1 then
            return unknown
        end if
        peak = prices[0]
        peaki = 0
        maxdd = 0
        ddpeak = 0
        ddtrough = 0
        i = 0
        while i < n
            if prices[i] > peak then
                peak = prices[i]
                peaki = i
            end if
            if peak != 0 then
                dd = (prices[i] - peak) / peak
                if dd < maxdd then
                    maxdd = dd
                    ddpeak = peaki
                    ddtrough = i
                end if
            end if
            i = i + 1
        end while
        return { max_drawdown: maxdd, peak: ddpeak, trough: ddtrough }
    end function

    ' Value at Risk at tail probability alpha (e.g. 0.05 for 95% VaR),
    ' returned as a POSITIVE loss magnitude. method "historical" uses the
    ' empirical quantile; "normal" uses a Gaussian fit (mean + z*sd).
    function value_at_risk(rets, alpha, method)
        n = len(rets)
        if n < 2 then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha >= 1 then
            return unknown
        end if
        if method = "normal" then
            z = _inv_norm_std(alpha)
            return 0 - (mean(rets) + z * stdev(rets))
        end if
        s = rets
        s = sort(s)
        return 0 - quantile(s, alpha)
    end function

    ' Conditional VaR / Expected Shortfall at tail probability alpha: the
    ' average of all returns at or below the empirical alpha-quantile,
    ' returned as a POSITIVE loss magnitude.
    function cvar(rets, alpha)
        n = len(rets)
        if n < 2 then
            return unknown
        end if
        if alpha <= 0 then
            return unknown
        end if
        if alpha >= 1 then
            return unknown
        end if
        s = rets
        s = sort(s)
        thr = quantile(s, alpha)
        tot = 0
        cnt = 0
        i = 0
        while i < n
            if s[i] <= thr then
                tot = tot + s[i]
                cnt = cnt + 1
            end if
            i = i + 1
        end while
        if cnt = 0 then
            return unknown
        end if
        return 0 - tot / cnt
    end function

    ' CAPM market-model regression of excess asset returns on excess market
    ' returns: alpha (Jensen's alpha), beta (systematic risk), their SEs /
    ' t / p, and R^2. `rf` is the per-period risk-free rate.
    function capm(asset, market, rf)
        n = len(asset)
        if n != len(market) then
            return unknown
        end if
        if n < 3 then
            return unknown
        end if
        ea = []
        em = []
        i = 0
        while i < n
            append(ea, asset[i] - rf)
            append(em, market[i] - rf)
            i = i + 1
        end while
        fit = ols(ea, [em])
        if is_unknown(fit) then
            return unknown
        end if
        return { alpha: fit.coefficients[0], beta: fit.coefficients[1], alpha_se: fit.std_errors[0], beta_se: fit.std_errors[1], alpha_t: fit.t_values[0], beta_t: fit.t_values[1], alpha_p: fit.p_values[0], beta_p: fit.p_values[1], r_squared: fit.r_squared, n: n }
    end function

    ' --- Event studies -------------------------------------------------------
    '
    ' "Did something happen to this stock when that happened to this company?"
    ' -- the standard tool of empirical finance, and the one that turns EDGAR's
    ' filing dates into a testable claim: Form 4 insider purchases, earnings
    ' dates, 8-K events. The method is old and settled (Fama, Fisher, Jensen &
    ' Roll 1969); what follows is the textbook procedure with its traps refused
    ' rather than left to the caller.
    '
    ' The shape:
    '
    '   1. estimate a NORMAL-RETURN model over a window that ends BEFORE the
    '      event -- the market model R_i = alpha + beta*R_m + e is the default;
    '   2. an ABNORMAL return is the residual over the event window,
    '      AR_t = R_it - (alpha + beta*R_mt);
    '   3. CAR is their sum over that window, one number per event;
    '   4. across many events, CAAR is the mean CAR and a t-test asks whether
    '      it differs from zero.
    '
    ' FOUR TRAPS, each of which yields a plausible NUMBER rather than an error,
    ' and each refused here by name:
    '
    '   * TRADING DAYS, NOT CALENDAR DAYS. A "5-day window" counted in calendar
    '     days spans a different number of observations depending on which
    '     weekday the event fell on, and silently includes fewer returns over a
    '     holiday. `event_window` indexes into the dates the series ACTUALLY
    '     has, so a window is always the requested number of observations.
    '   * AN EVENT ON A NON-TRADING DAY. News breaks on Saturdays. The event is
    '     mapped to the NEXT available trading day and the result says which,
    '     rather than silently missing or picking the day before.
    '   * LOOK-AHEAD. The estimation window must end before the event window
    '     starts. Overlap them and the "normal" return is fitted partly on the
    '     event being measured, which biases the abnormal return toward zero.
    '     Refused, with a `gap` between them that defaults to 0 but is there to
    '     be used.
    '   * AGGREGATING UNEQUAL WINDOWS. Averaging a 5-day CAR with an 11-day CAR
    '     produces a number with no interpretation. `event_study` refuses a set
    '     whose windows differ.

    ' Locate an event in a date series, on TRADING-DAY terms.
    '
    ' `dates` must be ascending (market.daily guarantees this). Returns the
    ' index of the event day and the inclusive window bounds:
    '
    '   { ok, index, actual, shifted, start, last_ix, message }
    '
    ' `shifted` is true when `event_date` was not itself in the series and the
    ' next trading day was used -- the caller can then report how many events
    ' moved, which is a normal thing to disclose in this method.
    function event_window(dates, event_date, pre, post)
        n = len(dates)
        if n = 0 then
            return { ok: false, message: "event_window: no dates" }
        end if
        if pre < 0 or post < 0 then
            return { ok: false, message: "event_window: pre and post must be >= 0" }
        end if

        ' First trading day on or after the event. A linear scan: event sets
        ' are small beside the price series, and a wrong binary search here
        ' would be a silent off-by-one in the thing being measured.
        idx = unknown
        i = 0
        while i < n
            if dates[i] >= event_date then
                idx = i
                i = n
            end if
            i = i + 1
        end while
        if is_unknown(idx) then
            return { ok: false, message: "event_window: the event is after the last date in the series" }
        end if

        shifted = dates[idx] != event_date
        start = idx - pre
        last_ix = idx + post
        if start < 0 then
            return { ok: false, message: "event_window: the series starts too late for a " + string(pre) + "-day pre-window" }
        end if
        if last_ix > n - 1 then
            return { ok: false, message: "event_window: the series ends too early for a " + string(post) + "-day post-window" }
        end if
        return { ok: true, index: idx, actual: dates[idx], shifted: shifted,
                 start: start, last_ix: last_ix, message: "" }
    end function

    ' Abnormal returns for one event.
    '
    ' `spec` fields: event (index), pre, post, estimation (observations),
    ' gap (observations between the estimation and event windows, default 0),
    ' model ("market" | "market_adjusted" | "mean", default "market").
    function abnormal_returns(asset_r, market_r, spec)
        n = len(asset_r)
        if n != len(market_r) then
            return { ok: false, message: "abnormal_returns: asset and market series differ in length" }
        end if

        evt = spec.event
        pre = 0
        if has(spec, "pre") then
            pre = spec.pre
        end if
        post = 0
        if has(spec, "post") then
            post = spec.post
        end if
        gap = 0
        if has(spec, "gap") then
            gap = spec.gap
        end if
        est = 120
        if has(spec, "estimation") then
            est = spec.estimation
        end if
        model = "market"
        if has(spec, "model") then
            model = spec.model
        end if

        evt_start = evt - pre
        evt_stop = evt + post
        if evt_start < 0 or evt_stop > n - 1 then
            return { ok: false, message: "abnormal_returns: the event window falls outside the series" }
        end if

        est_stop = evt_start - gap - 1
        est_start = est_stop - est + 1
        if est_start < 0 then
            return { ok: false, message: "abnormal_returns: only " + string(est_stop + 1) + " observations before the event window, need " + string(est) + " for estimation" }
        end if

        ' Refused rather than shrugged at: a market model fitted on a handful
        ' of points produces a beta that is noise, and every abnormal return
        ' downstream inherits it while looking perfectly ordinary.
        if model = "market" and est < 30 then
            return { ok: false, message: "abnormal_returns: an estimation window of " + string(est) + " is too short to fit a market model (30 minimum)" }
        end if

        ' Slice the estimation window.
        ey = []
        ex = []
        i = est_start
        while i <= est_stop
            append(ey, asset_r[i])
            append(ex, market_r[i])
            i = i + 1
        end while

        alpha = 0
        beta = 1
        if model = "market" then
            fit = ols(ey, ex)
            if is_unknown(fit) then
                return { ok: false, message: "abnormal_returns: the market model did not fit" }
            end if
            alpha = fit.coefficients[0]
            beta = fit.coefficients[1]
        else
            if model = "mean" then
                alpha = mean(ey)
                beta = 0
            else
                if model = "market_adjusted" then
                    alpha = 0
                    beta = 1
                else
                    return { ok: false, message: "abnormal_returns: unknown model '" + string(model) + "' (market, market_adjusted, mean)" }
                end if
            end if
        end if

        ' Residual standard deviation over the ESTIMATION window is what the
        ' single-event t-statistic is scaled by -- not the event window, whose
        ' whole point is that it may be unusual.
        resid = []
        i = 0
        while i < len(ey)
            append(resid, ey[i] - (alpha + beta * ex[i]))
            i = i + 1
        end while
        sigma = stdev(resid)

        ar = []
        i = evt_start
        while i <= evt_stop
            append(ar, asset_r[i] - (alpha + beta * market_r[i]))
            i = i + 1
        end while

        car = 0
        for each a in ar
            car = car + a
        next a

        t = unknown
        if sigma > 0 then
            t = car / (sigma * sqrt(len(ar)))
        end if

        return { ok: true, ar: ar, car: car, alpha: alpha, beta: beta,
                 sigma: sigma, t: t, window: len(ar), model: model,
                 estimation: len(ey), est_from: est_start, est_to: est_stop,
                 evt_from: evt_start, evt_to: evt_stop, event: evt, message: "" }
    end function

    ' Aggregate a set of `abnormal_returns` results into CAAR and its test.
    function event_study(studies)
        if not is_array(studies) then
            return { ok: false, message: "event_study expects an array of abnormal_returns results" }
        end if
        cars = []
        width = unknown
        for each st in studies
            if is_record(st) then
                if st.ok then
                    if is_unknown(width) then
                        width = st.window
                    end if
                    ' Averaging a 5-day CAR with an 11-day CAR yields a number
                    ' with no interpretation, and nothing about it looks wrong.
                    if st.window != width then
                        return { ok: false, message: "event_study: event windows differ (" + string(width) + " and " + string(st.window) + "); a CAAR over unequal windows has no meaning" }
                    end if
                    append(cars, st.car)
                end if
            end if
        next st

        k = len(cars)
        if k < 2 then
            return { ok: false, message: "event_study: needs at least 2 successful events, got " + string(k) }
        end if

        ' CONTAMINATED ESTIMATION WINDOWS. When events cluster, one event's
        ' estimation window can contain ANOTHER event -- so the "normal"
        ' return it fits is partly the abnormal behaviour of its neighbour, and
        ' every abnormal return computed from it is biased. Measured on a
        ' constructed pair whose true CAAR is exactly 0.025, contamination
        ' produced 0.02455: close enough to read as ordinary noise, which is
        ' precisely why it is reported rather than left to be noticed.
        '
        ' Reported, not refused: clustering is sometimes unavoidable (an
        ' industry-wide event), and the literature's answer is to disclose it
        ' and consider a portfolio approach, not to discard the study.
        contaminated = 0
        oi = 0
        while oi < len(studies)
            a = studies[oi]
            if is_record(a) then
                if a.ok then
                    oj = 0
                    while oj < len(studies)
                        b = studies[oj]
                        if oj != oi and is_record(b) then
                            if b.ok then
                                if b.event >= a.est_from and b.event <= a.est_to then
                                    contaminated = contaminated + 1
                                    oj = len(studies)
                                end if
                            end if
                        end if
                        oj = oj + 1
                    end while
                end if
            end if
            oi = oi + 1
        end while

        caar = mean(cars)
        sd = stdev(cars)
        t = unknown
        p = unknown
        if sd > 0 then
            t = caar / (sd / sqrt(k))
            p = 2 * (1 - t_cdf(abs(t), k - 1))
        end if
        note = ""
        if contaminated > 0 then
            note = string(contaminated) + " of " + string(k) + " events have another event inside their estimation window; their normal-return models are fitted on contaminated data"
        end if
        return { ok: true, n: k, caar: caar, cars: cars, sd: sd, t: t, p: p,
                 window: width, df: k - 1, contaminated: contaminated,
                 message: "", note: note }
    end function

    ' --- Meta-analysis -------------------------------------------------------
    '
    ' Combining findings ACROSS studies -- medicine's central quantitative tool
    ' and increasingly social science's. It needs almost nothing new here: the
    ' effect sizes it pools (`cohens_d`, `hedges_g`, `odds_ratio`) already
    ' exist, and what was missing was the pooling itself and the heterogeneity
    ' that decides whether pooling was defensible at all.
    '
    ' A study enters as an ESTIMATE and its VARIANCE. `odds_ratio` already
    ' returns `log_or_se`, whose square is the variance on the log scale;
    ' `smd_variance` below supplies it for a standardized mean difference,
    ' which is what a paper reports as d or g.
    '
    ' THE TRAP THAT MATTERS MOST: RATIO MEASURES POOL ON THE LOG SCALE. An odds
    ' ratio, a risk ratio and a hazard ratio are multiplicative -- 0.5 and 2.0
    ' are the same size of effect in opposite directions, and their arithmetic
    ' mean is 1.25, which reads as a modest harm where the truth is none at
    ' all. Pooling raw ratios produces a number that is wrong and looks
    ' entirely ordinary. There is no way to detect the mistake from the values
    ' (a set of ratios and a set of raw differences are both just numbers), so
    ' `scale: "ratio"` is offered explicitly: it pools the LOGS and
    ' back-transforms the estimate and its interval.

    ' Variance of a standardized mean difference (Cohen's d or Hedges' g), from
    ' the effect and the two group sizes -- which is what a paper reports.
    function smd_variance(d, n1, n2)
        if n1 < 1 or n2 < 1 then
            return unknown
        end if
        return (n1 + n2) / (n1 * n2) + (d * d) / (2 * (n1 + n2))
    end function

    ' `studies`: an array of records carrying `effect` and either `variance` or
    ' `se`. `spec`: { model: "fixed" | "random", scale: "identity" | "ratio" }.
    '
    ' Returns the pooled estimate with its interval and test, ALWAYS beside the
    ' heterogeneity statistics -- Q, its df, I-squared and tau-squared -- because
    ' a pooled estimate over wildly heterogeneous studies is a precise summary
    ' of nothing, and reporting it without them invites exactly that reading.
    function meta_analysis(studies, spec)
        if not is_array(studies) then
            return { ok: false, message: "meta_analysis expects an array of study records" }
        end if
        model = "random"
        if has(spec, "model") then
            model = spec.model
        end if
        if model != "fixed" and model != "random" then
            return { ok: false, message: "meta_analysis: unknown model '" + string(model) + "' (fixed, random)" }
        end if
        scale = "identity"
        if has(spec, "scale") then
            scale = spec.scale
        end if
        if scale != "identity" and scale != "ratio" then
            return { ok: false, message: "meta_analysis: unknown scale '" + string(scale) + "' (identity, ratio)" }
        end if

        ys = []
        vs = []
        idx = 0
        for each st in studies
            if not is_record(st) then
                return { ok: false, message: "meta_analysis: study " + string(idx) + " is not a record" }
            end if
            if not has(st, "effect") then
                return { ok: false, message: "meta_analysis: study " + string(idx) + " has no effect" }
            end if
            v = unknown
            if has(st, "variance") then
                v = st.variance
            else
                if has(st, "se") then
                    v = st.se * st.se
                end if
            end if
            if is_unknown(v) then
                return { ok: false, message: "meta_analysis: study " + string(idx) + " has neither variance nor se" }
            end if
            ' A zero or negative variance is infinite weight: that one study
            ' would silently become the entire result.
            if v <= 0 then
                return { ok: false, message: "meta_analysis: study " + string(idx) + " has a variance of " + string(v) + "; a non-positive variance is infinite weight" }
            end if
            e = st.effect
            if scale = "ratio" then
                if e <= 0 then
                    return { ok: false, message: "meta_analysis: study " + string(idx) + " has a ratio effect of " + string(e) + "; a ratio must be positive to be pooled on the log scale" }
                end if
                e = log(e)
            end if
            append(ys, e)
            append(vs, v)
            idx = idx + 1
        next st

        k = len(ys)
        if k < 2 then
            return { ok: false, message: "meta_analysis: needs at least 2 studies, got " + string(k) }
        end if

        ' --- fixed-effect (inverse variance), which the heterogeneity needs
        sw = 0
        swy = 0
        sw2 = 0
        i = 0
        while i < k
            w = 1 / vs[i]
            sw = sw + w
            swy = swy + w * ys[i]
            sw2 = sw2 + w * w
            i = i + 1
        end while
        fixed = swy / sw

        ' --- heterogeneity: Cochran's Q, I-squared, DerSimonian-Laird tau-squared
        q = 0
        i = 0
        while i < k
            w = 1 / vs[i]
            dlt = ys[i] - fixed
            q = q + w * dlt * dlt
            i = i + 1
        end while
        df = k - 1
        isq = 0
        if q > df and q > 0 then
            isq = (q - df) / q * 100
        end if
        cc = sw - sw2 / sw
        tau2 = 0
        if cc > 0 and q > df then
            tau2 = (q - df) / cc
        end if

        ' --- the requested model
        if model = "fixed" then
            est = fixed
            se = sqrt(1 / sw)
            weights = []
            i = 0
            while i < k
                append(weights, (1 / vs[i]) / sw * 100)
                i = i + 1
            end while
        else
            rw = 0
            rwy = 0
            i = 0
            while i < k
                w = 1 / (vs[i] + tau2)
                rw = rw + w
                rwy = rwy + w * ys[i]
                i = i + 1
            end while
            est = rwy / rw
            se = sqrt(1 / rw)
            weights = []
            i = 0
            while i < k
                append(weights, (1 / (vs[i] + tau2)) / rw * 100)
                i = i + 1
            end while
        end if

        z = 1.959963984540054
        lo = est - z * se
        hi = est + z * se
        zstat = est / se
        pv = 2 * (1 - normal_cdf(abs(zstat), 0, 1))

        ' Q is tested against chi-square on k-1 df. Reported, and deliberately
        ' NOT used to pick the model: choosing fixed-versus-random after seeing
        ' the heterogeneity is a decision about the data made from the data.
        qp = 1 - chi2_cdf(q, df)

        out_est = est
        out_lo = lo
        out_hi = hi
        if scale = "ratio" then
            out_est = exp(est)
            out_lo = exp(lo)
            out_hi = exp(hi)
        end if

        return { ok: true, model: model, scale: scale, k: k,
                 estimate: out_est, ci_low: out_lo, ci_high: out_hi,
                 log_estimate: est, se: se, z: zstat, p: pv,
                 q: q, df: df, q_p: qp, i_squared: isq, tau_squared: tau2,
                 weights: weights, message: "" }
    end function

    ' Egger's test for funnel-plot asymmetry -- the usual small-study /
    ' publication-bias check. Regresses the standardized effect on its
    ' precision; an intercept far from zero is the asymmetry.
    '
    ' Reported, never interpreted for you: asymmetry has several causes besides
    ' publication bias (true small-study effects, poor methods in small trials),
    ' and the test is weak below about ten studies -- which it says.
    function eggers_test(studies)
        ys = []
        ses = []
        for each st in studies
            if is_record(st) then
                v = unknown
                if has(st, "variance") then
                    v = st.variance
                else
                    if has(st, "se") then
                        v = st.se * st.se
                    end if
                end if
                if not is_unknown(v) and v > 0 then
                    append(ys, st.effect)
                    append(ses, sqrt(v))
                end if
            end if
        next st
        k = len(ys)
        if k < 3 then
            return { ok: false, message: "eggers_test: needs at least 3 studies, got " + string(k) }
        end if
        snd = []
        prec = []
        i = 0
        while i < k
            append(snd, ys[i] / ses[i])
            append(prec, 1 / ses[i])
            i = i + 1
        end while
        fit = ols(snd, prec)
        if is_unknown(fit) then
            return { ok: false, message: "eggers_test: the regression did not fit" }
        end if
        note = ""
        if k < 10 then
            note = "with " + string(k) + " studies this test has little power; below about 10 it is not informative"
        end if
        return { ok: true, k: k, intercept: fit.coefficients[0],
                 se: fit.std_errors[0], t: fit.t_values[0], p: fit.p_values[0],
                 slope: fit.coefficients[1], note: note, message: "" }
    end function

    ' --- Survival analysis ---------------------------------------------------
    '
    ' Time until something happens, when for some subjects it has not happened
    ' YET. That last clause is the whole subject: medicine (death, relapse),
    ' engineering (failure), business (churn, default) all share it, and none
    ' of the ordinary tools handle it.
    '
    ' CENSORING IS THE POINT, AND BOTH WAYS OF IGNORING IT ARE WRONG. A subject
    ' still alive at the end of the study has not survived "35 weeks and then
    ' died" -- only "at least 35 weeks". Drop those subjects and the estimate
    ' is pessimistic (you keep only the ones who failed); count them as events
    ' and it is worse (you invent failures that never happened). Kaplan-Meier
    ' exists to use exactly what is known: they were at risk up to their
    ' censoring time and contribute nothing after it.
    '
    ' So `events` is not optional and not inferable. Every function here takes
    ' durations WITH an event indicator -- 1 or true where the event happened,
    ' 0 or false where the subject was censored -- and refuses a length
    ' mismatch rather than assuming the tail is one or the other.
    '
    ' TIES BETWEEN AN EVENT AND A CENSORING AT THE SAME TIME follow the usual
    ' convention: a subject censored at time t is counted as AT RISK for the
    ' event at t. It is a real choice, it changes the estimate, and it is
    ' stated rather than left in the code.

    function _surv_check(times, events)
        if not is_array(times) or not is_array(events) then
            return "survival: times and events must be arrays"
        end if
        if len(times) != len(events) then
            return "survival: " + string(len(times)) + " times but " + string(len(events)) + " event flags"
        end if
        if len(times) = 0 then
            return "survival: no observations"
        end if
        i = 0
        while i < len(times)
            if times[i] < 0 then
                return "survival: observation " + string(i) + " has a negative time"
            end if
            i = i + 1
        end while
        return ""
    end function

    function _is_event(v)
        if is_boolean(v) then
            return v
        end if
        return v != 0
    end function

    ' The distinct times at which an EVENT occurred, ascending.
    function _event_times(times, events)
        seen = []
        i = 0
        while i < len(times)
            if _is_event(events[i]) then
                if not contains(seen, times[i]) then
                    append(seen, times[i])
                end if
            end if
            i = i + 1
        end while
        return sort(seen)
    end function

    ' Kaplan-Meier product-limit estimator.
    '
    '   { ok, times[], survival[], at_risk[], events[], censored[], se[],
    '     lower[], upper[], median, median_reached, n, n_events, message }
    '
    ' `median` is `unknown` when the curve never reaches 0.5, and
    ' `median_reached` says so. Reporting the largest observed time instead --
    ' which is what happens if you take the last row and call it the median --
    ' understates survival by however long the study happened to run, and looks
    ' like a perfectly ordinary number.
    function kaplan_meier(times, events)
        why = _surv_check(times, events)
        if why != "" then
            return { ok: false, message: why }
        end if

        n = len(times)
        ets = _event_times(times, events)
        surv = 1
        out_t = []
        out_s = []
        out_risk = []
        out_d = []
        out_c = []
        out_se = []
        out_lo = []
        out_hi = []
        gw = 0
        total_events = 0

        for each t in ets
            ' At risk: everyone whose time is >= t. A subject censored exactly
            ' at t IS at risk for the event at t -- the stated convention.
            at_risk = 0
            d = 0
            cens = 0
            i = 0
            while i < n
                if times[i] >= t then
                    at_risk = at_risk + 1
                end if
                if times[i] = t then
                    if _is_event(events[i]) then
                        d = d + 1
                    else
                        cens = cens + 1
                    end if
                end if
                i = i + 1
            end while

            surv = surv * (1 - d / at_risk)
            total_events = total_events + d

            ' Greenwood's formula for the variance of the estimate.
            if at_risk > d then
                gw = gw + d / (at_risk * (at_risk - d))
            end if
            se = surv * sqrt(gw)

            z = 1.959963984540054
            lo = surv - z * se
            hi = surv + z * se
            if lo < 0 then
                lo = 0
            end if
            if hi > 1 then
                hi = 1
            end if

            append(out_t, t)
            append(out_s, surv)
            append(out_risk, at_risk)
            append(out_d, d)
            append(out_c, cens)
            append(out_se, se)
            append(out_lo, lo)
            append(out_hi, hi)
        next t

        ' Median: the FIRST time at which survival drops to 0.5 or below. If
        ' the curve never gets there the median does not exist, and saying so
        ' is the only honest answer.
        med = unknown
        reached = false
        i = 0
        while i < len(out_s)
            if out_s[i] <= 0.5 and not reached then
                med = out_t[i]
                reached = true
            end if
            i = i + 1
        end while

        return { ok: true, times: out_t, survival: out_s, at_risk: out_risk,
                 events: out_d, censored: out_c, se: out_se,
                 lower: out_lo, upper: out_hi,
                 median: med, median_reached: reached,
                 n: n, n_events: total_events, message: "" }
    end function

    ' Survival at an arbitrary time: the step function, read at t. Before the
    ' first event it is 1; between events it holds its last value.
    function survival_at(km, t)
        if not km.ok then
            return unknown
        end if
        s = 1
        i = 0
        while i < len(km.times)
            if km.times[i] <= t then
                s = km.survival[i]
            end if
            i = i + 1
        end while
        return s
    end function

    ' Log-rank test: do two groups have the same survival?
    '
    ' At each event time it compares the events OBSERVED in group A against
    ' those EXPECTED under the null that both groups share one hazard, and
    ' accumulates. The pooled risk sets are what make censored subjects count
    ' for exactly as long as they were observed.
    '
    ' ASSUMES PROPORTIONAL HAZARDS. Where the curves CROSS -- one group better
    ' early and worse later -- the differences cancel and the test can report
    ' no difference between two survival experiences that are nothing alike.
    ' That is a property of the test, not a bug, and it is the reason to look
    ' at the curves as well as the p-value.
    function logrank(times_a, events_a, times_b, events_b)
        why = _surv_check(times_a, events_a)
        if why != "" then
            return { ok: false, message: "logrank group A: " + why }
        end if
        why = _surv_check(times_b, events_b)
        if why != "" then
            return { ok: false, message: "logrank group B: " + why }
        end if

        all_t = []
        for each t in _event_times(times_a, events_a)
            if not contains(all_t, t) then
                append(all_t, t)
            end if
        next t
        for each t in _event_times(times_b, events_b)
            if not contains(all_t, t) then
                append(all_t, t)
            end if
        next t
        all_t = sort(all_t)

        obs_a = 0
        exp_a = 0
        var_sum = 0
        obs_b = 0

        for each t in all_t
            na = 0
            da = 0
            i = 0
            while i < len(times_a)
                if times_a[i] >= t then
                    na = na + 1
                end if
                if times_a[i] = t and _is_event(events_a[i]) then
                    da = da + 1
                end if
                i = i + 1
            end while
            nb = 0
            db = 0
            i = 0
            while i < len(times_b)
                if times_b[i] >= t then
                    nb = nb + 1
                end if
                if times_b[i] = t and _is_event(events_b[i]) then
                    db = db + 1
                end if
                i = i + 1
            end while

            nt = na + nb
            dt = da + db
            if nt > 1 and dt > 0 then
                obs_a = obs_a + da
                obs_b = obs_b + db
                exp_a = exp_a + na * dt / nt
                var_sum = var_sum + (na * nb * dt * (nt - dt)) / (nt * nt * (nt - 1))
            end if
        next t

        if var_sum <= 0 then
            return { ok: false, message: "logrank: no comparable event times in the two groups" }
        end if

        chi = (obs_a - exp_a) * (obs_a - exp_a) / var_sum
        pv = 1 - chi2_cdf(chi, 1)
        return { ok: true, chi_squared: chi, df: 1, p: pv,
                 observed_a: obs_a, expected_a: exp_a,
                 observed_b: obs_b, expected_b: obs_a + obs_b - exp_a,
                 variance: var_sum, message: "" }
    end function

    ' --- Cox proportional hazards --------------------------------------------
    '
    ' Kaplan-Meier DESCRIBES survival; Cox MODELS it. `h(t|x) = h0(t)exp(b'x)`
    ' asks how a covariate multiplies the hazard, and the trick that makes it
    ' famous is that the baseline hazard h0 never has to be estimated: the
    ' PARTIAL likelihood compares, at each event time, the subject who failed
    ' against everyone still at risk, and h0 cancels out of that ratio.
    '
    ' So the answer is a HAZARD RATIO -- exp(beta) -- meaning "this covariate
    ' multiplies the instantaneous risk by this much, per unit". Two things
    ' about that sentence carry the traps:
    '
    '   PER UNIT. A covariate measured in dollars gives a hazard ratio per
    '   dollar, which for any realistic income is 1.0000-something and reads as
    '   no effect at all. Nothing is wrong with the arithmetic; the covariate is
    '   scaled wrongly. `hr_per` reports the ratio over a stated interval so
    '   the number can be made legible without re-fitting.
    '
    '   PROPORTIONAL. The whole model assumes the ratio is CONSTANT over time.
    '   Where hazards cross -- a treatment that helps early and harms later --
    '   a single hazard ratio averages them into a number describing neither.
    '   Cox cannot detect this for you and neither can any single p-value; it
    '   is why the Kaplan-Meier curves are always looked at as well.
    '
    ' Ties are handled by BRESLOW's approximation, which is stated rather than
    ' silent: it is the simplest and it degrades when many events share a time.
    ' Efron's is more accurate under heavy ties and is not implemented.

    function _cox_negll(params, ctx)
        ' Partial log-likelihood, negated for the minimiser.
        total = 0
        ei = 0
        while ei < len(ctx.etimes)
            t = ctx.etimes[ei]
            ' Sum of covariates over the subjects failing at t, and the log of
            ' the risk set's summed exp(b'x).
            dsum = 0
            d = 0
            rsum = 0
            i = 0
            while i < len(ctx.times)
                lp = 0
                k = 0
                while k < len(params)
                    lp = lp + params[k] * ctx.covars[k][i]
                    k = k + 1
                end while
                if ctx.times[i] >= t then
                    rsum = rsum + exp(lp)
                end if
                if ctx.times[i] = t and ctx.events[i] then
                    dsum = dsum + lp
                    d = d + 1
                end if
                i = i + 1
            end while
            if rsum > 0 then
                total = total + dsum - d * log(rsum)
            end if
            ei = ei + 1
        end while
        return 0 - total
    end function

    ' `covars` is an array of columns (one array per covariate), each the same
    ' length as `times`. A single covariate may be passed as a bare array.
    function cox_ph(times, events, covars)
        why = _surv_check(times, events)
        if why != "" then
            return { ok: false, message: "cox_ph: " + why }
        end if

        cols = covars
        if len(covars) > 0 then
            if not is_array(covars[0]) then
                cols = [covars]
            end if
        end if
        if len(cols) = 0 then
            return { ok: false, message: "cox_ph: no covariates" }
        end if
        for each c in cols
            if len(c) != len(times) then
                return { ok: false, message: "cox_ph: a covariate has " + string(len(c)) + " values but there are " + string(len(times)) + " subjects" }
            end if
        next c

        flags = []
        for each e in events
            append(flags, _is_event(e))
        next e
        ets = _event_times(times, events)
        if len(ets) = 0 then
            return { ok: false, message: "cox_ph: no events; a partial likelihood needs at least one failure" }
        end if

        ctx = { times: times, events: flags, etimes: ets, covars: cols }
        init = []
        i = 0
        while i < len(cols)
            append(init, 0)
            i = i + 1
        end while

        res = optimize(_cox_negll, init, { max_iter: 20000, tol: pow(10, -12) }, ctx)
        if is_unknown(res) then
            return { ok: false, message: "cox_ph: the optimiser failed" }
        end if
        beta = res.params

        ' Standard errors from the observed information -- the second
        ' derivative of the partial log-likelihood, taken numerically because
        ' the optimiser is derivative-free. Diagonal only: this reports each
        ' coefficient's own error, not the full covariance.
        ses = []
        hrs = []
        zs = []
        ps = []
        los = []
        his = []
        i = 0
        while i < len(beta)
            h = 0.0001
            up = beta
            dn = beta
            up[i] = beta[i] + h
            dn[i] = beta[i] - h
            f0 = _cox_negll(beta, ctx)
            fu = _cox_negll(up, ctx)
            fd = _cox_negll(dn, ctx)
            ' negll is the NEGATIVE log-likelihood, so its second difference is
            ' already the observed information.
            info = (fu - 2 * f0 + fd) / (h * h)
            se = unknown
            z = unknown
            pv = unknown
            lo = unknown
            hi = unknown
            if info > 0 then
                se = sqrt(1 / info)
                z = beta[i] / se
                pv = 2 * (1 - normal_cdf(abs(z), 0, 1))
                lo = exp(beta[i] - 1.959963984540054 * se)
                hi = exp(beta[i] + 1.959963984540054 * se)
            end if
            append(ses, se)
            append(zs, z)
            append(ps, pv)
            append(los, lo)
            append(his, hi)
            append(hrs, exp(beta[i]))
            i = i + 1
        end while

        n_ev = 0
        for each f in flags
            if f then
                n_ev = n_ev + 1
            end if
        next f

        return { ok: true, coefficients: beta, hazard_ratios: hrs,
                 std_errors: ses, z_values: zs, p_values: ps,
                 ci_low: los, ci_high: his,
                 log_likelihood: 0 - res.value, n: len(times), n_events: n_ev,
                 converged: res.converged, iterations: res.iterations,
                 ties: "breslow", message: "" }
    end function

    ' The hazard ratio for a stated CHANGE in a covariate, rather than per
    ' unit. A model fitted on dollars reports a ratio per dollar -- 1.00002,
    ' which reads as nothing; over $10,000 the same coefficient may be 1.22.
    ' Same fit, legible number, no re-scaling and re-fitting.
    function hr_per(fit, index, delta)
        if not fit.ok then
            return unknown
        end if
        if index < 0 or index >= len(fit.coefficients) then
            return unknown
        end if
        return exp(fit.coefficients[index] * delta)
    end function

    ' --- Exploratory factor analysis -----------------------------------------
    '
    ' FACTOR ANALYSIS IS NOT PCA, AND THE DIFFERENCE IS THE POINT. Both hand
    ' back a small number of dimensions from many correlated variables, both
    ' come out of an eigen-decomposition, and they are routinely used as
    ' though interchangeable. They answer opposite questions:
    '
    '   PCA asks "what combinations of these variables capture the most
    '   variance?" A component is a SUMMARY of the observed variables --
    '   defined by them, caused by nothing. It explains TOTAL variance,
    '   including whatever is unique to each variable and whatever is
    '   measurement error.
    '
    '   FACTOR ANALYSIS asks "what unobserved thing could have CAUSED these
    '   variables to correlate?" A factor is a latent cause, and the model
    '   explicitly sets aside the part of each variable that is unique or
    '   error. It explains COMMON variance only.
    '
    ' In the arithmetic that whole distinction is one diagonal: PCA decomposes
    ' a correlation matrix with 1s down the diagonal, factor analysis replaces
    ' them with COMMUNALITIES -- the share of each variable the factors
    ' account for -- and iterates until they settle. Everything else is shared,
    ' which is exactly why the two get confused.
    '
    ' If you want a composite score, PCA is right and this is not. If you
    ' believe an unmeasured construct is producing the correlations -- an
    ' ability, an attitude, a latent risk -- this is right and PCA overstates
    ' the loadings by folding each variable's own noise into the factor.

    ' Correlation matrix of a list of columns.
    function _corr_matrix(cols)
        k = len(cols)
        m = []
        i = 0
        while i < k
            row = []
            j = 0
            while j < k
                if i = j then
                    append(row, 1)
                else
                    append(row, correlation(cols[i], cols[j]))
                end if
                j = j + 1
            end while
            append(m, row)
            i = i + 1
        end while
        return m
    end function

    ' Principal-axis factoring with iterated communalities, then an optional
    ' varimax rotation.
    '
    '   { ok, loadings[][], communalities[], uniquenesses[], eigenvalues[],
    '     variance_explained[], n_factors, iterations, converged, rotated,
    '     heywood, message }
    '
    ' `loadings[v][f]` is variable v's loading on factor f.
    function factor_analysis(cols, spec)
        if not is_array(cols) or len(cols) = 0 then
            return { ok: false, message: "factor_analysis: no variables" }
        end if
        if not is_array(cols[0]) then
            return { ok: false, message: "factor_analysis expects an array of columns" }
        end if
        k = len(cols)
        n = len(cols[0])
        for each c in cols
            if len(c) != n then
                return { ok: false, message: "factor_analysis: columns differ in length" }
            end if
        next c
        if k < 3 then
            return { ok: false, message: "factor_analysis: needs at least 3 variables, got " + string(k) }
        end if
        if n < k then
            return { ok: false, message: "factor_analysis: " + string(n) + " observations for " + string(k) + " variables; a correlation matrix that size is not estimable" }
        end if

        nf = 1
        if has(spec, "factors") then
            nf = spec.factors
        end if
        if nf < 1 or nf >= k then
            return { ok: false, message: "factor_analysis: asked for " + string(nf) + " factors from " + string(k) + " variables; a factor model needs fewer factors than variables" }
        end if
        maxit = 50
        if has(spec, "max_iter") then
            maxit = spec.max_iter
        end if
        rotate = true
        if has(spec, "rotate") then
            rotate = spec.rotate
        end if

        r = _corr_matrix(cols)

        ' Initial communality estimate: the largest absolute correlation in the
        ' row. Crude but standard, and the iteration is what matters.
        comm = []
        i = 0
        while i < k
            best = 0
            j = 0
            while j < k
                if i != j then
                    a = abs(r[i][j])
                    if a > best then
                        best = a
                    end if
                end if
                j = j + 1
            end while
            append(comm, best)
            i = i + 1
        end while

        loadings = []
        evals = []
        it = 0
        converged = false
        while it < maxit and not converged
            ' The reduced correlation matrix: communalities on the diagonal.
            ' THIS LINE IS THE WHOLE DIFFERENCE FROM PCA.
            rr = []
            i = 0
            while i < k
                row = []
                j = 0
                while j < k
                    if i = j then
                        append(row, comm[i])
                    else
                        append(row, r[i][j])
                    end if
                    j = j + 1
                end while
                append(rr, row)
                i = i + 1
            end while

            eig = _jacobi_eigen(rr)
            ' Order by descending eigenvalue.
            order = []
            i = 0
            while i < k
                append(order, i)
                i = i + 1
            end while
            i = 1
            while i < k
                key = order[i]
                kv = eig.values[key]
                j = i - 1
                while j >= 0 and eig.values[order[j]] < kv
                    order[j + 1] = order[j]
                    j = j - 1
                end while
                order[j + 1] = key
                i = i + 1
            end while

            loadings = []
            evals = []
            i = 0
            while i < k
                append(loadings, [])
                i = i + 1
            end while
            f = 0
            while f < nf
                ev = eig.values[order[f]]
                append(evals, ev)
                sc = 0
                if ev > 0 then
                    sc = sqrt(ev)
                end if
                v = 0
                while v < k
                    loadings[v] = loadings[v]
                    ' vectors is indexed EIGENVECTOR-FIRST: eig.vectors[j] is
                    ' the whole j-th eigenvector, as `pca` reads it. Indexing
                    ' it the other way transposes the solution, which still
                    ' produces loadings and communalities -- they are simply
                    ' wrong, with Heywood cases everywhere.
                    append(loadings[v], eig.vectors[order[f]][v] * sc)
                    v = v + 1
                end while
                f = f + 1
            end while

            ' New communalities: how much of each variable the factors carry.
            newc = []
            delta = 0
            v = 0
            while v < k
                h = 0
                f = 0
                while f < nf
                    h = h + loadings[v][f] * loadings[v][f]
                    f = f + 1
                end while
                append(newc, h)
                d = abs(h - comm[v])
                if d > delta then
                    delta = d
                end if
                v = v + 1
            end while
            comm = newc
            if delta < 0.0001 then
                converged = true
            end if
            it = it + 1
        end while

        ' A HEYWOOD CASE: a communality at or above 1 means a variable has NO
        ' unique variance, which is impossible -- the model is misspecified,
        ' usually too many factors for too few variables. Reported rather than
        ' clamped, because a silently clamped solution still prints loadings
        ' that look fine.
        heywood = 0
        for each h in comm
            if h >= 1 then
                heywood = heywood + 1
            end if
        next h

        rotated = false
        if rotate and nf > 1 then
            loadings = _varimax(loadings, nf)
            rotated = true
        end if

        uniq = []
        varexp = []
        f = 0
        while f < nf
            ss = 0
            v = 0
            while v < k
                ss = ss + loadings[v][f] * loadings[v][f]
                v = v + 1
            end while
            append(varexp, ss / k * 100)
            f = f + 1
        end while
        v = 0
        while v < k
            append(uniq, 1 - comm[v])
            v = v + 1
        end while

        note = ""
        if heywood > 0 then
            note = string(heywood) + " variable(s) have a communality of 1 or more (a Heywood case): the model is misspecified, usually too many factors for too few variables"
        end if

        return { ok: true, loadings: loadings, communalities: comm,
                 uniquenesses: uniq, eigenvalues: evals,
                 variance_explained: varexp, n_factors: nf,
                 iterations: it, converged: converged, rotated: rotated,
                 heywood: heywood, note: note, message: "" }
    end function

    ' Varimax: rotate the factors so each variable loads strongly on few of
    ' them, which is easier to read.
    '
    ' ROTATION DOES NOT IMPROVE FIT. It cannot: any rotation reproduces the
    ' same correlation matrix and the same communalities. It changes only which
    ' arbitrary axes the same subspace is described by. A rotated solution that
    ' "looks better" is not a better model, and reporting the rotation as
    ' though it found something is the standard misreading.
    '
    ' NO TRIGONOMETRY IS USED, because gBASIC has none -- chart.bas had to
    ' write a Taylor sine for its pie slices. The rotation angle is
    ' `atan2(a, b) / 4`, and the pair (cos, sin) of a quarter-angle follows
    ' from (a, b) by applying the half-angle identities TWICE:
    '
    '     cos(t)   = b / sqrt(a^2 + b^2)
    '     cos(t/2) = sqrt((1 + cos t) / 2)        sin(t/2) = ±sqrt((1 - cos t) / 2)
    '
    ' t = atan2(a, b) lies in (-pi, pi], so t/4 lies in (-pi/4, pi/4]: its
    ' cosine is positive and its sine takes the sign of `a`. Exact, and only
    ' square roots.
    function _quarter_angle(a, b)
        r = sqrt(a * a + b * b)
        if r = 0 then
            return { c: 1, s: 0 }
        end if
        ct = b / r
        if ct > 1 then
            ct = 1
        end if
        if ct < -1 then
            ct = -1
        end if
        sgn = 1
        if a < 0 then
            sgn = -1
        end if
        c2 = sqrt((1 + ct) / 2)
        c4 = sqrt((1 + c2) / 2)
        s4 = sgn * sqrt((1 - c2) / 2)
        return { c: c4, s: s4 }
    end function

    function _varimax(loadings, nf)
        k = len(loadings)
        lam = loadings
        sweep = 0
        while sweep < 100
            moved = 0
            p = 0
            while p < nf - 1
                q = p + 1
                while q < nf
                    ' The varimax criterion for one pair of factors. With
                    ' u = x^2 - y^2 and t = 2xy per variable, FOUR sums are
                    ' needed and they are easy to conflate -- an earlier
                    ' version used A and B in place of C and D, which left
                    ' every loading at 0.7 (the least simple structure there
                    ' is) while still converging and reporting sane
                    ' communalities.
                    sa = 0
                    sb = 0
                    sc = 0
                    sd = 0
                    v = 0
                    while v < k
                        x = lam[v][p]
                        y = lam[v][q]
                        u = x * x - y * y
                        t = 2 * x * y
                        sa = sa + u
                        sb = sb + t
                        sc = sc + u * u - t * t
                        sd = sd + 2 * u * t
                        v = v + 1
                    end while
                    aa = sd - 2 * sa * sb / k
                    bb = sc - (sa * sa - sb * sb) / k
                    ang = _quarter_angle(aa, bb)
                    if abs(ang.s) > 0.000001 then
                        moved = moved + 1
                        v = 0
                        while v < k
                            x = lam[v][p]
                            y = lam[v][q]
                            lam[v][p] = x * ang.c + y * ang.s
                            lam[v][q] = 0 - x * ang.s + y * ang.c
                            v = v + 1
                        end while
                    end if
                    q = q + 1
                end while
                p = p + 1
            end while
            if moved = 0 then
                sweep = 100
            end if
            sweep = sweep + 1
        end while
        return lam
    end function

    ' --- Causal inference ----------------------------------------------------
    '
    ' Difference-in-differences and instrumental variables. Both estimators are
    ' easy to get right in the COEFFICIENT and wrong in the STANDARD ERROR, and
    ' in both cases the wrong standard error is the flattering one.
    '
    ' 2SLS run as two ordinary regressions -- fit x on z, then y on x-hat --
    ' produces the correct point estimate and a standard error computed from
    ' the SECOND STAGE's residuals. Those are not the model's residuals: the
    ' model is y = X*beta + u and u must be measured against the ORIGINAL X,
    ' not the fitted one. The second-stage residuals are smaller, so every
    ' standard error is too small, every t is too large, and nothing about the
    ' output looks wrong. `iv_2sls` computes u from X and reports the
    ' difference is real by construction; tests/causal_test.bas performs the
    ' naive version alongside and pins that the two coefficients AGREE while
    ' the two standard errors do not.
    '
    ' A difference-in-differences on panel data has the mirror problem: with
    ' outcomes correlated within a unit over time, conventional standard errors
    ' are badly understated (Bertrand, Duflo & Mullainathan 2004, "How Much
    ' Should We Trust Differences-in-Differences Estimates?"), which
    ' manufactures significance. `did` takes `cluster:` and computes CR1.

    ' An optional spec field, with a default. `unknown` counts as absent, so a
    ' caller can pass a field through without special-casing it.
    function _causal_opt(spec, name, dflt)
        if not is_record(spec) then
            return dflt
        end if
        if not has(spec, name) then
            return dflt
        end if
        v = spec[name]
        if is_unknown(v) then
            return dflt
        end if
        return v
    end function

    ' 0/1 from a number or a boolean; `unknown` for anything else. Deliberately
    ' strict: an indicator column that quietly coerces from something else is a
    ' design matrix nobody checked.
    function _binary01(v)
        if is_boolean(v) then
            if v then
                return 1
            end if
            return 0
        end if
        if not is_number(v) then
            return unknown
        end if
        if v = 1 then
            return 1
        end if
        if v = 0 then
            return 0
        end if
        return unknown
    end function

    ' A whole indicator column as 0/1; `unknown` if any element is neither.
    function _binary_col(xs, n)
        out = []
        i = 0
        while i < n
            b = _binary01(xs[i])
            if is_unknown(b) then
                return unknown
            end if
            append(out, b)
            i = i + 1
        end while
        return out
    end function

    ' Concatenate two column lists into a new one.
    function _cat_cols(a, b)
        out = []
        for each c in a
            append(out, c)
        next c
        for each c in b
            append(out, c)
        next c
        return out
    end function

    ' A p x p matrix of zeros.
    function _zeros_sq(p)
        m = []
        a = 0
        while a < p
            row = []
            b = 0
            while b < p
                append(row, 0)
                b = b + 1
            end while
            append(m, row)
            a = a + 1
        end while
        return m
    end function

    ' bread * meat * bread, both square p x p.
    function _sandwich(bread, meat)
        return mat_mul(mat_mul(bread, meat), bread)
    end function

    ' Heteroskedasticity-consistent meat: sum_i w_i x_i x_i'.
    function _meat_weighted(bigx, w, p, n)
        m = _zeros_sq(p)
        i = 0
        while i < n
            a = 0
            while a < p
                b = 0
                while b < p
                    m[a][b] = m[a][b] + w[i] * bigx[i][a] * bigx[i][b]
                    b = b + 1
                end while
                a = a + 1
            end while
            i = i + 1
        end while
        return m
    end function

    ' Per-observation weights for the HC0..HC3 sandwich variants.
    function _hc_weights(bigx, e, xtxinv, p, n, hc)
        if hc != "HC0" and hc != "HC1" and hc != "HC2" and hc != "HC3" then
            return unknown
        end if
        w = []
        i = 0
        while i < n
            ei2 = e[i] * e[i]
            om = ei2
            if hc = "HC1" then
                om = ei2 * n / (n - p)
            end if
            if hc = "HC2" or hc = "HC3" then
                v = mat_vec(xtxinv, bigx[i])
                hi = 0
                a = 0
                while a < p
                    hi = hi + bigx[i][a] * v[a]
                    a = a + 1
                end while
                if hc = "HC2" then
                    om = ei2 / (1 - hi)
                else
                    om = ei2 / ((1 - hi) * (1 - hi))
                end if
            end if
            append(w, om)
            i = i + 1
        end while
        return w
    end function

    ' Cluster-robust meat with the CR1 finite-sample correction:
    '   (G/(G-1)) * ((n-1)/(n-p)) * sum_g (X_g' e_g)(X_g' e_g)'
    ' With ONE OBSERVATION PER CLUSTER this reduces exactly to HC1 -- the
    ' correction becomes (n/(n-1))*((n-1)/(n-p)) = n/(n-p) and each cluster
    ' score is a single x_i e_i -- which is what tests/causal_test.bas checks
    ' this path against `ols_robust(..., "HC1")` with. Two implementations
    ' written from different formulas landing on the same digits is a stronger
    ' statement than either one's golden.
    function _meat_cluster(bigx, e, groups, p, n)
        slot = {}
        keys = []
        i = 0
        while i < n
            k = "g" + string(groups[i])
            if not has(slot, k) then
                slot[k] = len(keys)
                append(keys, k)
            end if
            i = i + 1
        end while
        g = len(keys)
        if g < 2 then
            return unknown
        end if
        scores = []
        j = 0
        while j < g
            v = []
            a = 0
            while a < p
                append(v, 0)
                a = a + 1
            end while
            append(scores, v)
            j = j + 1
        end while
        i = 0
        while i < n
            gi = slot["g" + string(groups[i])]
            a = 0
            while a < p
                scores[gi][a] = scores[gi][a] + bigx[i][a] * e[i]
                a = a + 1
            end while
            i = i + 1
        end while
        adj = (g / (g - 1)) * ((n - 1) / (n - p))
        m = _zeros_sq(p)
        j = 0
        while j < g
            a = 0
            while a < p
                b = 0
                while b < p
                    m[a][b] = m[a][b] + adj * scores[j][a] * scores[j][b]
                    b = b + 1
                end while
                a = a + 1
            end while
            j = j + 1
        end while
        return { meat: m, groups: g }
    end function

    ' OLS point estimates with a choice of covariance:
    '   groups given      CR1 cluster-robust, t on G-1 df
    '   hc = "HC0".."HC3" White sandwich, NORMAL reference (as `ols_robust`)
    '   otherwise         classic sigma^2 (X'X)^-1, t on n-p df
    ' The two reference distributions are not a slip: they are the choices the
    ' library already made in `ols` and `ols_robust`, and matching them beats
    ' being internally tidy and externally surprising.
    function _fit_cov(y, cols, n, hc, groups)
        p = len(cols) + 1
        if n <= p then
            return { ok: false, message: "not enough observations (" + string(n) + ") for " + string(p) + " parameters" }
        end if
        bigx = _design(cols, n)
        xt = mat_transpose(bigx)
        xtxinv = mat_inverse(mat_mul(xt, bigx))
        if is_unknown(xtxinv) then
            return { ok: false, message: "design matrix is singular: a column is collinear with the others" }
        end if
        beta = mat_vec(xtxinv, mat_vec(xt, y))
        fitted = mat_vec(bigx, beta)
        e = []
        rss = 0
        i = 0
        while i < n
            r = y[i] - fitted[i]
            append(e, r)
            rss = rss + r * r
            i = i + 1
        end while
        ybar = mean(y)
        tss = 0
        i = 0
        while i < n
            d = y[i] - ybar
            tss = tss + d * d
            i = i + 1
        end while
        r2 = unknown
        if tss > 0 then
            r2 = 1 - rss / tss
        end if

        dof = n - p
        ctype = "classic"
        ng = 0
        normal = false
        cov = unknown
        if not is_unknown(groups) then
            cm = _meat_cluster(bigx, e, groups, p, n)
            if is_unknown(cm) then
                return { ok: false, message: "clustering needs at least two clusters" }
            end if
            cov = _sandwich(xtxinv, cm.meat)
            ctype = "cluster"
            ng = cm.groups
            dof = ng - 1
        else
            if hc = "" then
                s2 = rss / (n - p)
                cov = _zeros_sq(p)
                a = 0
                while a < p
                    b = 0
                    while b < p
                        cov[a][b] = s2 * xtxinv[a][b]
                        b = b + 1
                    end while
                    a = a + 1
                end while
            else
                w = _hc_weights(bigx, e, xtxinv, p, n, hc)
                if is_unknown(w) then
                    return { ok: false, message: "unknown covariance option '" + string(hc) + "' (expected HC0, HC1, HC2 or HC3)" }
                end if
                cov = _sandwich(xtxinv, _meat_weighted(bigx, w, p, n))
                ctype = hc
                normal = true
            end if
        end if

        ses = []
        stats = []
        pvals = []
        j = 0
        while j < p
            v = cov[j][j]
            se = unknown
            sv = unknown
            pv = unknown
            if is_number(v) and v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    sv = beta[j] / se
                    if normal then
                        pv = 2 * (1 - _norm_cdf_std(abs(sv)))
                    else
                        pv = 2 * (1 - t_cdf(abs(sv), dof))
                    end if
                end if
            end if
            append(ses, se)
            append(stats, sv)
            append(pvals, pv)
            j = j + 1
        end while
        return { ok: true, coefficients: beta, std_errors: ses, stat_values: stats, p_values: pvals, fitted: fitted, residuals: e, rss: rss, r_squared: r2, cov: cov, cov_type: ctype, normal: normal, groups: ng, n: n, df: dof, params: p, message: "" }
    end function

    ' Joint F test that the LAST q columns of `cols` are all zero, by refitting
    ' without them. Both fits are classical -- an F built from two residual sums
    ' of squares has no robust meaning, and pretending otherwise by feeding it
    ' sandwich standard errors would be a Wald test wearing an F's name.
    function _f_drop(y, cols, n, q)
        full = _fit_cov(y, cols, n, "", unknown)
        if not full.ok then
            return full
        end if
        keep = []
        i = 0
        while i < len(cols) - q
            append(keep, cols[i])
            i = i + 1
        end while
        rest = _fit_cov(y, keep, n, "", unknown)
        if not rest.ok then
            return rest
        end if
        d2 = n - full.params
        if full.rss <= 0 or d2 <= 0 then
            return { ok: true, f_stat: unknown, df1: q, df2: d2, p_value: unknown, message: "the unrestricted fit has no residual variation" }
        end if
        f = ((rest.rss - full.rss) / q) / (full.rss / d2)
        pv = unknown
        if f >= 0 then
            pv = 1 - f_cdf(f, q, d2)
        end if
        return { ok: true, f_stat: f, df1: q, df2: d2, p_value: pv, r_squared: full.r_squared, message: "" }
    end function

    ' Difference-in-differences. `treated` and `post` are 0/1 (or boolean)
    ' indicators, one per observation; the estimate is the coefficient on their
    ' interaction:
    '   y = b0 + b1*treated + b2*post + ATT*(treated*post) + controls
    ' With no controls that coefficient is EXACTLY the four-cell arithmetic
    '   (treated_post - treated_pre) - (control_post - control_pre)
    ' and `means` carries the four cells so the caller can see the estimate is
    ' a difference of differences and not a black box. `saturated` says whether
    ' the identity holds for this call.
    '
    ' spec, all fields optional:
    '   covariates  extra control column(s)
    '   cluster     one cluster id per row -> CR1 standard errors
    '   hc          "HC0".."HC3" for heteroskedasticity-robust ones instead
    '   level       confidence level for the interval (default 0.95)
    '
    ' PARALLEL TRENDS IS AN ASSUMPTION AND IS NOT TESTED HERE, because it
    ' cannot be: it is a claim about what the treated group WOULD have done
    ' after treatment, and no data records that. What can be tested is whether
    ' the groups moved together BEFORE treatment -- a different and weaker
    ' question -- which is `pre_trends`.
    function did(y, treated, post, spec)
        n = len(y)
        if n = 0 then
            return { ok: false, message: "no observations" }
        end if
        if len(treated) != n then
            return { ok: false, message: "treated has " + string(len(treated)) + " rows, y has " + string(n) }
        end if
        if len(post) != n then
            return { ok: false, message: "post has " + string(len(post)) + " rows, y has " + string(n) }
        end if
        tcol = _binary_col(treated, n)
        if is_unknown(tcol) then
            return { ok: false, message: "treated must be 0/1 or true/false" }
        end if
        pcol = _binary_col(post, n)
        if is_unknown(pcol) then
            return { ok: false, message: "post must be 0/1 or true/false" }
        end if

        ' The four cells, and the refusal that matters: a cell with nothing in
        ' it is not a difference-in-differences at all, and the regression
        ' would answer anyway (or go singular and answer nothing useful).
        sums = [0, 0, 0, 0]
        cnts = [0, 0, 0, 0]
        inter = []
        i = 0
        while i < n
            if not is_number(y[i]) then
                return { ok: false, message: "y[" + string(i) + "] is not a number" }
            end if
            cell = tcol[i] * 2 + pcol[i]
            sums[cell] = sums[cell] + y[i]
            cnts[cell] = cnts[cell] + 1
            append(inter, tcol[i] * pcol[i])
            i = i + 1
        end while
        names = ["control_pre", "control_post", "treated_pre", "treated_post"]
        i = 0
        while i < 4
            if cnts[i] = 0 then
                return { ok: false, message: "no observations in the " + names[i] + " cell" }
            end if
            i = i + 1
        end while
        cells = {}
        i = 0
        while i < 4
            cells[names[i]] = sums[i] / cnts[i]
            i = i + 1
        end while
        raw = (cells.treated_post - cells.treated_pre) - (cells.control_post - cells.control_pre)

        covs = []
        cv = _causal_opt(spec, "covariates", unknown)
        if not is_unknown(cv) then
            covs = _norm_cols(cv, n)
            if is_unknown(covs) then
                return { ok: false, message: "a covariate column does not have " + string(n) + " rows" }
            end if
        end if
        cols = _cat_cols([tcol, pcol, inter], covs)

        groups = _causal_opt(spec, "cluster", unknown)
        if not is_unknown(groups) then
            if len(groups) != n then
                return { ok: false, message: "cluster has " + string(len(groups)) + " rows, y has " + string(n) }
            end if
        end if
        hc = _causal_opt(spec, "hc", "")
        fit = _fit_cov(y, cols, n, hc, groups)
        if not fit.ok then
            return fit
        end if

        level = _causal_opt(spec, "level", 0.95)
        att = fit.coefficients[3]
        se = fit.std_errors[3]
        lo = unknown
        hi = unknown
        if is_number(se) then
            if fit.normal then
                crit = normal_quantile(1 - (1 - level) / 2, 0, 1)
            else
                crit = t_quantile(1 - (1 - level) / 2, fit.df)
            end if
            lo = att - crit * se
            hi = att + crit * se
        end if
        return { ok: true, att: att, std_error: se, t_value: fit.stat_values[3], p_value: fit.p_values[3], conf_low: lo, conf_high: hi, level: level, means: cells, counts: { control_pre: cnts[0], control_post: cnts[1], treated_pre: cnts[2], treated_post: cnts[3] }, diff_in_means: raw, saturated: len(covs) = 0, coefficients: fit.coefficients, std_errors: fit.std_errors, residuals: fit.residuals, cov_type: fit.cov_type, clusters: fit.groups, r_squared: fit.r_squared, n: n, df: fit.df, message: "" }
    end function

    ' The testable half of the parallel-trends assumption: did the two groups
    ' move together BEFORE anyone was treated? Over the pre-treatment periods
    ' only, fits
    '   y = period effects + b*treated + sum_t lead_t * treated*1{period = t}
    ' with the LAST pre-period as the omitted reference, and tests the lead
    ' coefficients jointly with an F.
    '
    ' A LARGE p-VALUE HERE IS NOT EVIDENCE THAT PARALLEL TRENDS HOLDS. It is
    ' the absence of evidence against it over the periods that happen to be in
    ' the data, and the test is exactly as weak as the pre-period is short.
    ' `periods` and `df1` are reported so that weakness is visible rather than
    ' something the reader has to infer; `note` says it in words.
    function pre_trends(y, treated, period, treat_start)
        n = len(y)
        if n = 0 then
            return { ok: false, message: "no observations" }
        end if
        if len(treated) != n or len(period) != n then
            return { ok: false, message: "treated and period must both have " + string(n) + " rows" }
        end if
        ys = []
        ts = []
        ps = []
        seen = {}
        pre = []
        i = 0
        while i < n
            if not is_number(period[i]) then
                return { ok: false, message: "period[" + string(i) + "] is not a number" }
            end if
            if period[i] < treat_start then
                b = _binary01(treated[i])
                if is_unknown(b) then
                    return { ok: false, message: "treated must be 0/1 or true/false" }
                end if
                append(ys, y[i])
                append(ts, b)
                append(ps, period[i])
                k = "p" + string(period[i])
                if not has(seen, k) then
                    seen[k] = true
                    append(pre, period[i])
                end if
            end if
            i = i + 1
        end while
        m = len(ys)
        if m = 0 then
            return { ok: false, message: "no observations before period " + string(treat_start) }
        end if
        pre = sort(pre)
        np = len(pre)
        if np < 2 then
            return { ok: false, message: "a pre-trend test needs at least two pre-treatment periods, found " + string(np) }
        end if
        ref = pre[np - 1]

        ' treated, then a dummy per non-reference pre-period, then the leads.
        ' The leads go LAST so _f_drop can restrict them as a block.
        dummies = []
        leads = []
        j = 0
        while j < np - 1
            dcol = []
            lcol = []
            i = 0
            while i < m
                d = 0
                if ps[i] = pre[j] then
                    d = 1
                end if
                append(dcol, d)
                append(lcol, d * ts[i])
                i = i + 1
            end while
            append(dummies, dcol)
            append(leads, lcol)
            j = j + 1
        end while
        cols = _cat_cols(_cat_cols([ts], dummies), leads)
        q = len(leads)
        test = _f_drop(ys, cols, m, q)
        if not test.ok then
            return test
        end if
        fit = _fit_cov(ys, cols, m, "", unknown)
        if not fit.ok then
            return fit
        end if
        base = 1 + q
        out = []
        j = 0
        while j < q
            append(out, { period: pre[j], coefficient: fit.coefficients[base + 1 + j], std_error: fit.std_errors[base + 1 + j], t_value: fit.stat_values[base + 1 + j], p_value: fit.p_values[base + 1 + j] })
            j = j + 1
        end while
        note = "a large p-value is the absence of evidence against parallel trends over " + string(np) + " pre-period(s), not evidence for it"
        return { ok: true, f_stat: test.f_stat, df1: test.df1, df2: test.df2, p_value: test.p_value, leads: out, reference: ref, periods: np, n: m, note: note, message: "" }
    end function

    ' Two-stage least squares. `endog` is the endogenous regressor(s),
    ' `instruments` the EXCLUDED instruments; spec.exog holds any included
    ' exogenous controls, which serve as their own instruments.
    '
    '   X = [1, endog, exog]      Z = [1, instruments, exog]
    '   beta = (X' Pz X)^-1 X' Pz y      with Pz = Z (Z'Z)^-1 Z'
    '   u    = y - X beta                     <- the ORIGINAL X, not X-hat
    '
    ' That last line is the whole point. Running the two stages as two calls to
    ' `ols` gives the same beta and residuals from X-hat, which are smaller;
    ' the resulting standard errors are too small by a factor that depends on
    ' how weak the instrument is, and nothing in the output says so.
    '
    ' spec, all optional: exog, cluster, hc, level (as `did`).
    '
    ' The classical covariance uses sigma^2 = RSS/(n-p) and the t distribution
    ' on n-p, which is R's `AER::ivreg` and Stata's `ivregress 2sls, small`.
    ' Stata's DEFAULT is asymptotic -- RSS/n and a normal reference -- so the
    ' standard errors there are slightly smaller. Named because a reader
    ' comparing two tools over the same data deserves to know which convention
    ' explains a difference in the third decimal.
    '
    ' Diagnostics, all reported rather than enforced:
    '   first_stage   per endogenous regressor, the F on the EXCLUDED
    '                 instruments. Below ~10 the instrument is weak and 2SLS is
    '                 biased toward OLS with confidence intervals that do not
    '                 cover; `weak` flags it.
    '   sargan        overidentification (only when there are more instruments
    '                 than endogenous regressors -- with exact identification
    '                 there is nothing to test, and reporting a number there
    '                 would be reporting a tautology)
    '   wu_hausman    whether the regressor was endogenous at all; a small
    '                 p-value says OLS is inconsistent and IV was needed
    function iv_2sls(y, endog, instruments, spec)
        n = len(y)
        if n = 0 then
            return { ok: false, message: "no observations" }
        end if
        ecols = _norm_cols(endog, n)
        if is_unknown(ecols) then
            return { ok: false, message: "an endogenous column does not have " + string(n) + " rows" }
        end if
        zcols = _norm_cols(instruments, n)
        if is_unknown(zcols) then
            return { ok: false, message: "an instrument column does not have " + string(n) + " rows" }
        end if
        xcols = []
        ex = _causal_opt(spec, "exog", unknown)
        if not is_unknown(ex) then
            xcols = _norm_cols(ex, n)
            if is_unknown(xcols) then
                return { ok: false, message: "an exogenous column does not have " + string(n) + " rows" }
            end if
        end if
        ke = len(ecols)
        kz = len(zcols)
        if ke = 0 then
            return { ok: false, message: "no endogenous regressor given" }
        end if
        if kz < ke then
            return { ok: false, message: "under-identified: " + string(kz) + " excluded instrument(s) for " + string(ke) + " endogenous regressor(s)" }
        end if

        p = 1 + ke + len(xcols)
        if n <= p then
            return { ok: false, message: "not enough observations (" + string(n) + ") for " + string(p) + " parameters" }
        end if
        bigx = _design(_cat_cols(ecols, xcols), n)
        bigz = _design(_cat_cols(zcols, xcols), n)
        zt = mat_transpose(bigz)
        ztzinv = mat_inverse(mat_mul(zt, bigz))
        if is_unknown(ztzinv) then
            return { ok: false, message: "the instrument matrix is singular: two instruments carry the same information" }
        end if
        ' X-hat = Z (Z'Z)^-1 Z' X, the projection of every regressor onto the
        ' instrument space. An exogenous control projects onto itself.
        xhat = mat_mul(bigz, mat_mul(ztzinv, mat_mul(zt, bigx)))
        xht = mat_transpose(xhat)
        ainv = mat_inverse(mat_mul(xht, xhat))
        if is_unknown(ainv) then
            return { ok: false, message: "the instruments do not identify the endogenous regressor(s)" }
        end if
        beta = mat_vec(ainv, mat_vec(xht, y))

        fitted = mat_vec(bigx, beta)
        u = []
        rss = 0
        i = 0
        while i < n
            r = y[i] - fitted[i]
            append(u, r)
            rss = rss + r * r
            i = i + 1
        end while

        groups = _causal_opt(spec, "cluster", unknown)
        if not is_unknown(groups) then
            if len(groups) != n then
                return { ok: false, message: "cluster has " + string(len(groups)) + " rows, y has " + string(n) }
            end if
        end if
        hc = _causal_opt(spec, "hc", "")
        dof = n - p
        ctype = "classic"
        ng = 0
        normal = false
        cov = unknown
        if not is_unknown(groups) then
            cm = _meat_cluster(xhat, u, groups, p, n)
            if is_unknown(cm) then
                return { ok: false, message: "clustering needs at least two clusters" }
            end if
            cov = _sandwich(ainv, cm.meat)
            ctype = "cluster"
            ng = cm.groups
            dof = ng - 1
        else
            if hc = "" then
                s2 = rss / dof
                cov = _zeros_sq(p)
                a = 0
                while a < p
                    b = 0
                    while b < p
                        cov[a][b] = s2 * ainv[a][b]
                        b = b + 1
                    end while
                    a = a + 1
                end while
            else
                w = _hc_weights(xhat, u, ainv, p, n, hc)
                if is_unknown(w) then
                    return { ok: false, message: "unknown covariance option '" + string(hc) + "' (expected HC0, HC1, HC2 or HC3)" }
                end if
                cov = _sandwich(ainv, _meat_weighted(xhat, w, p, n))
                ctype = hc
                normal = true
            end if
        end if

        ses = []
        stats = []
        pvals = []
        j = 0
        while j < p
            v = cov[j][j]
            se = unknown
            sv = unknown
            pv = unknown
            if is_number(v) and v >= 0 then
                se = sqrt(v)
                if se > 0 then
                    sv = beta[j] / se
                    if normal then
                        pv = 2 * (1 - _norm_cdf_std(abs(sv)))
                    else
                        pv = 2 * (1 - t_cdf(abs(sv), dof))
                    end if
                end if
            end if
            append(ses, se)
            append(stats, sv)
            append(pvals, pv)
            j = j + 1
        end while

        ' First stage per endogenous regressor: exogenous controls first so the
        ' excluded instruments are the trailing block _f_drop restricts.
        fscols = _cat_cols(xcols, zcols)
        stage1 = []
        vhat = []
        weak = false
        minf = unknown
        j = 0
        while j < ke
            t1 = _f_drop(ecols[j], fscols, n, kz)
            if not t1.ok then
                return t1
            end if
            s1 = _fit_cov(ecols[j], fscols, n, "", unknown)
            if not s1.ok then
                return s1
            end if
            append(vhat, s1.residuals)
            isweak = true
            if is_number(t1.f_stat) then
                if t1.f_stat >= 10 then
                    isweak = false
                end if
                if is_unknown(minf) then
                    minf = t1.f_stat
                else
                    if t1.f_stat < minf then
                        minf = t1.f_stat
                    end if
                end if
            end if
            if isweak then
                weak = true
            end if
            append(stage1, { f_stat: t1.f_stat, df1: t1.df1, df2: t1.df2, p_value: t1.p_value, r_squared: s1.r_squared, weak: isweak })
            j = j + 1
        end while

        ' Overidentification: with more instruments than endogenous regressors
        ' the extra ones are testable. Sargan's J = n * R^2 from regressing the
        ' structural residuals on every instrument.
        sargan = unknown
        if kz > ke then
            sg = _fit_cov(u, _cat_cols(zcols, xcols), n, "", unknown)
            if sg.ok then
                if is_number(sg.r_squared) then
                    jstat = n * sg.r_squared
                    sdf = kz - ke
                    sargan = { statistic: jstat, df: sdf, p_value: 1 - chi2_cdf(jstat, sdf) }
                end if
            end if
        end if

        ' Wu-Hausman: add the first-stage residuals to the structural equation
        ' and test them jointly. Significant means the regressor really was
        ' endogenous and OLS really was inconsistent.
        hausman = unknown
        hcols = _cat_cols(_cat_cols(ecols, xcols), vhat)
        ht = _f_drop(y, hcols, n, ke)
        if ht.ok then
            hausman = { f_stat: ht.f_stat, df1: ht.df1, df2: ht.df2, p_value: ht.p_value }
        end if

        level = _causal_opt(spec, "level", 0.95)
        lo = unknown
        hi = unknown
        if is_number(ses[1]) then
            if normal then
                crit = normal_quantile(1 - (1 - level) / 2, 0, 1)
            else
                crit = t_quantile(1 - (1 - level) / 2, dof)
            end if
            lo = beta[1] - crit * ses[1]
            hi = beta[1] + crit * ses[1]
        end if
        note = ""
        if weak then
            note = "weak instrument: the first-stage F is below 10, so this estimate is biased toward OLS and its interval under-covers"
        end if
        return { ok: true, estimate: beta[1], std_error: ses[1], t_value: stats[1], p_value: pvals[1], conf_low: lo, conf_high: hi, level: level, coefficients: beta, std_errors: ses, t_values: stats, p_values: pvals, cov: cov, cov_type: ctype, normal: normal, clusters: ng, residuals: u, fitted: fitted, rss: rss, first_stage: stage1, weak: weak, min_first_stage_f: minf, sargan: sargan, wu_hausman: hausman, endogenous: ke, instruments: kz, exogenous: len(xcols), overidentified: kz - ke, n: n, df: dof, note: note, message: "" }
    end function

end library
