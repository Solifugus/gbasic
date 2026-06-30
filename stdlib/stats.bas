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

        return { coefficients: beta, fitted: fitted, residuals: residuals, r_squared: r2, adj_r_squared: adj, std_errors: ses, t_values: tvals, p_values: pvals, n: n, df: dof }
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
        return _glm_result(beta, cov, fitted, loglik, iter, converged, n, p)
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
        return _glm_result(beta, cov, fitted, loglik, iter, converged, n, p)
    end function

    ' Shared GLM result assembler: turn coefficients + covariance into the
    ' standard-error / z / p-value table and the result record.
    function _glm_result(beta, cov, fitted, loglik, iter, converged, n, p)
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
        return { coefficients: beta, std_errors: ses, z_values: zvals, p_values: pvals, fitted: fitted, log_likelihood: loglik, iterations: iter, converged: converged, n: n }
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
end library
