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
end library
