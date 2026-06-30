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
end library
