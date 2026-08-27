' stats causal inference: difference-in-differences and IV/2SLS.
' Golden-compared by tests/run_causal.sh.
'
' EVERY CHECK STATES ITS OWN EXPECTED ANSWER, and here almost every one of
' them DERIVES that answer a second way inside the fixture rather than quoting
' a number from a run:
'
'   the DiD estimate      against the four cell means, arithmetic
'   the CR1 covariance    against ols_robust's HC1, which is a different
'                         formula in different code and must agree exactly
'                         when every cluster holds one observation
'   the 2SLS estimate     against the Wald ratio (y1-y0)/(x1-x0), four means
'                         and no matrix algebra at all
'   the 2SLS std error    against sigma^2 / sum (xhat - mean xhat)^2, computed
'                         here from the structural residuals
'   the first-stage F     against t^2 from an ordinary `ols` first stage
'   Sargan's J            against n * R^2 from `ols` of the residuals on the
'                         instruments
'   Wu-Hausman's F        against two residual sums of squares from `ols`
'   the pre-trend F       likewise
'
' That matters more than usual for this pair of estimators. Both produce a
' number that looks like every other regression coefficient, and both have a
' failure mode where the COEFFICIENT IS RIGHT and the standard error is not:
' a golden would record the wrong standard error as the expected one and
' defend it forever.
'
' Headline values were cross-checked once against numpy (a direct transcription
' of the matrix algebra, independent of this implementation) while the suite
' was written; they are pinned here, and a rebaseline is only meaningful if
' that check is repeated.

load stats

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' Whether a value sits inside a stated band -- for the claims that are about a
' MAGNITUDE rather than a digit (how wrong the naive standard error is, how
' strong an instrument is), where pinning a digit would assert precision the
' claim does not have.
function within(label, lo, hi, actual)
    if is_number(actual) then
        if actual >= lo then
            if actual <= hi then
                print "ok   " + label
                return true
            end if
        end if
    end if
    print "MISMATCH " + label + ": expected " + string(lo) + ".." + string(hi) + ", got '" + string(actual) + "'"
    return false
end function

function sum_sq_dev(xs)
    m = mean(xs)
    s = 0
    for each v in xs
        s = s + (v - m) * (v - m)
    next v
    return s
end function

print "== DiD: the estimate IS a difference of differences =="
' Four cells, two observations each, arranged so the answer is visible:
' control moves 10.5 -> 12.5 (+2), treated moves 20.5 -> 26.5 (+6), so the
' treated group gained 4 that the control group did not.
dy = [10, 11, 12, 13, 20, 21, 26, 27]
dt = [0, 0, 0, 0, 1, 1, 1, 1]
dp = [0, 0, 1, 1, 0, 0, 1, 1]
d = stats.did(dy, dt, dp, {})
append(results, check("it fits", true, d.ok))
append(results, check("control_pre", 10.5, d.means.control_pre))
append(results, check("control_post", 12.5, d.means.control_post))
append(results, check("treated_pre", 20.5, d.means.treated_pre))
append(results, check("treated_post", 26.5, d.means.treated_post))
hand = (d.means.treated_post - d.means.treated_pre) - (d.means.control_post - d.means.control_pre)
append(results, check("the regression coefficient equals the hand arithmetic", round(hand, 10), round(d.att, 10)))
append(results, check("and so does diff_in_means", round(hand, 10), round(d.diff_in_means, 10)))
append(results, check("which is 4", 4, round(d.att, 10)))
append(results, check("the fit is saturated", true, d.saturated))
append(results, check("each cell is counted", 2, d.counts.treated_post))
append(results, check("classic covariance by default", "classic", d.cov_type))
append(results, check("df is n - 4", 4, d.df))
append(results, check("the interval brackets the estimate", true, d.conf_low < d.att and d.att < d.conf_high))

print ""
print "-- booleans are accepted as indicators, and nothing else is"
bt = [false, false, false, false, true, true, true, true]
bp = [false, false, true, true, false, false, true, true]
db = stats.did(dy, bt, bp, {})
append(results, check("true/false give the same estimate", round(d.att, 10), round(db.att, 10)))
append(results, check("a string indicator is refused", false, stats.did(dy, ["a","a","a","a","b","b","b","b"], dp, {}).ok))
append(results, check("and 2 is not an indicator", false, stats.did(dy, [0,0,0,0,1,1,1,2], dp, {}).ok))

print ""
print "-- a covariate breaks the identity, and `saturated` says so"
' A control that moves WITH the interaction: the coefficient must no longer be
' the raw difference of differences, and a caller must be able to tell.
cov1 = [0, 1, 0, 1, 0, 1, 4, 5]
dc = stats.did(dy, dt, dp, { covariates: cov1 })
append(results, check("it still fits", true, dc.ok))
append(results, check("but is not saturated", false, dc.saturated))
append(results, check("and the coefficient has moved off the cell arithmetic", false, round(dc.att, 6) = round(dc.diff_in_means, 6)))
append(results, check("while diff_in_means still reports the cells", 4, round(dc.diff_in_means, 10)))

print ""
print "== CLUSTERING: the standard error a panel actually earns =="
' Bertrand, Duflo & Mullainathan (2004). Thirty units over twenty periods, and
' the disturbance is a PERSISTENT LEVEL SHIFT that arrives with the post
' period and differs by unit -- so a unit's ten post observations are one draw,
' not ten. Conventional standard errors count six hundred independent
' observations and are wrong by a factor of three in the direction that
' manufactures significance.
py = []
pt = []
pp = []
pc = []
u = 0
while u < 30
    shift = 2.0 * (mod(u * 3, 5) - 2)
    t = 0
    if u >= 15 then
        t = 1
    end if
    p = 0
    while p < 20
        pv = 0
        if p >= 10 then
            pv = 1
        end if
        idio = 0.05 * (mod(u * 3 + p * 7, 11) - 5)
        append(py, 5 + 0.7 * pv + 0.2 * t + 1.5 * t * pv + shift * pv + idio)
        append(pt, t)
        append(pp, pv)
        append(pc, u)
        p += 1
    end while
    u += 1
end while
cls = stats.did(py, pt, pp, {})
clu = stats.did(py, pt, pp, { cluster: pc })
append(results, check("both fits succeed", true, cls.ok and clu.ok))
append(results, check("the ESTIMATE is identical -- clustering changes uncertainty, not the answer", round(cls.att, 12), round(clu.att, 12)))
append(results, check("thirty clusters", 30, clu.clusters))
append(results, check("and df is G - 1, not n - p", 29, clu.df))
append(results, check("the estimate", 1.49633333, round(cls.att, 8)))
append(results, check("the conventional standard error", 0.328689, round(cls.std_error, 6)))
append(results, check("the clustered one", 1.052655, round(clu.std_error, 6)))
append(results, within("understated by a factor of about three", 3.15, 3.25, clu.std_error / cls.std_error))
append(results, check("conventionally this is a discovery", true, cls.p_value < 0.001))
append(results, check("clustered, the data cannot tell it from zero", true, clu.p_value > 0.10))
append(results, check("the covariance is labelled", "cluster", clu.cov_type))
append(results, check("one cluster is refused", false, stats.did(dy, dt, dp, { cluster: [1,1,1,1,1,1,1,1] }).ok))

print ""
print "-- CR1 with one observation per cluster IS HC1"
' Two formulas, written in different code from different definitions: the
' cluster correction (G/(G-1))((n-1)/(n-p)) collapses to n/(n-p) when every
' cluster is a singleton, which is exactly HC1's. They must agree to the digit.
ty = []
t1 = []
t2 = []
tg = []
i = 0
while i < 40
    append(t1, mod(i, 2))
    append(t2, mod(floor(i / 2), 2))
    append(ty, 3 + 1.4 * mod(i, 2) - 0.9 * mod(floor(i / 2), 2) + 2.1 * mod(i, 2) * mod(floor(i / 2), 2) + 0.35 * (mod(i, 7) - 3))
    append(tg, i)
    i += 1
end while
tinter = []
i = 0
while i < 40
    append(tinter, t1[i] * t2[i])
    i += 1
end while
sing = stats.did(ty, t1, t2, { cluster: tg })
hc1 = stats.ols_robust(ty, [t1, t2, tinter], "HC1")
append(results, check("both paths fit", true, sing.ok))
worst = 0
j = 0
while j < 4
    dgap = abs(sing.std_errors[j] - hc1.std_errors[j])
    if dgap > worst then
        worst = dgap
    end if
    j += 1
end while
append(results, check("every standard error agrees to 1e-12", true, worst < 0.000000000001))
append(results, check("and they are not all zero", true, sing.std_errors[3] > 0.001))

print ""
print "== IV/2SLS: the estimate equals the Wald ratio =="
' A binary instrument, so the 2SLS coefficient has a closed form that involves
' no matrices: the difference in mean y between the instrument's two groups,
' divided by the difference in mean x. If the matrix algebra above is wrong,
' it will not land on this number.
iz = []
ix = []
iya = []
iyb = []
i = 0
while i < 60
    zi = 0
    if mod(i, 2) = 1 then
        zi = 1
    end if
    w = 0.1 * (mod(i, 7) - 3)
    e = 0.05 * (mod(i, 5) - 2)
    xi = 1 + 2 * zi + w + e
    append(iz, zi)
    append(ix, xi)
    append(iya, 3 + 1.5 * xi + 2 * w)      ' confounder pushes y UP with x
    append(iyb, 3 + 1.5 * xi - 2 * w)      ' and DOWN with x
    i += 1
end while
sy1 = 0
sy0 = 0
sx1 = 0
sx0 = 0
n1 = 0
n0 = 0
i = 0
while i < 60
    if iz[i] = 1 then
        sy1 = sy1 + iya[i]
        sx1 = sx1 + ix[i]
        n1 = n1 + 1
    else
        sy0 = sy0 + iya[i]
        sx0 = sx0 + ix[i]
        n0 = n0 + 1
    end if
    i += 1
end while
wald = (sy1 / n1 - sy0 / n0) / (sx1 / n1 - sx0 / n0)
iva = stats.iv_2sls(iya, ix, iz, {})
append(results, check("it fits", true, iva.ok))
append(results, check("the 2SLS estimate IS the Wald ratio", round(wald, 10), round(iva.estimate, 10)))
append(results, check("which is 1.5066445183", 1.5066445183, round(iva.estimate, 10)))
append(results, check("exactly identified", 0, iva.overidentified))
append(results, check("one endogenous regressor, one instrument", true, iva.endogenous = 1 and iva.instruments = 1))
append(results, check("df is n - p", 58, iva.df))

' The bias IV exists to remove: OLS on the same data is pulled off the truth.
olsa = stats.ols(iya, ix)
append(results, check("OLS is biased away from it", true, abs(olsa.coefficients[1] - 1.5) > abs(iva.estimate - 1.5)))
append(results, check("and the two do not agree", false, round(olsa.coefficients[1], 4) = round(iva.estimate, 4)))

print ""
print "-- the standard error, derived here from the structural residuals"
' sigma^2 = RSS/(n-2) with RSS measured against the ORIGINAL x, and the
' denominator sum (xhat - mean xhat)^2 -- no matrix inverse involved.
fs = stats.ols(ix, iz)
rss = 0
i = 0
while i < 60
    r = iya[i] - iva.coefficients[0] - iva.coefficients[1] * ix[i]
    rss = rss + r * r
    i += 1
end while
hand_se = sqrt((rss / 58) / sum_sq_dev(fs.fitted))
append(results, check("the reported standard error matches it", round(hand_se, 12), round(iva.std_error, 12)))
append(results, check("which is 0.051883171606", 0.051883171606, round(iva.std_error, 12)))

print ""
print "-- TRAP: two ordinary regressions give the same estimate and the wrong error"
' The naive recipe -- fit x on z, then y on xhat -- reports the SECOND STAGE's
' residuals. Those are not the model's: the model is y = a + b*x + u and u must
' be measured against x. Both directions are shown, because the error is not
' conservative: it can inflate the standard error or shrink it, and which one
' happens depends on the sign of the confounding, which nobody knows.
naive_a = stats.ols(iya, fs.fitted)
append(results, check("naive and correct COEFFICIENTS agree to ten digits", round(iva.estimate, 10), round(naive_a.coefficients[1], 10)))
append(results, check("the standard errors do not", false, round(naive_a.std_errors[1], 8) = round(iva.std_error, 8)))
append(results, within("here the naive one is 1.78x too LARGE", 1.75, 1.81, naive_a.std_errors[1] / iva.std_error))

ivb = stats.iv_2sls(iyb, ix, iz, {})
naive_b = stats.ols(iyb, fs.fitted)
append(results, check("with the confounding reversed the estimate still agrees", round(ivb.estimate, 10), round(naive_b.coefficients[1], 10)))
append(results, within("but now the naive error is 2.7x too SMALL", 0.36, 0.37, naive_b.std_errors[1] / ivb.std_error))
append(results, check("which is the direction that manufactures significance", true, naive_b.std_errors[1] < ivb.std_error))
append(results, check("the two datasets have the SAME correct standard error", round(iva.std_error, 12), round(ivb.std_error, 12)))
append(results, check("so the naive answer is not merely conservative", true, naive_a.std_errors[1] > iva.std_error and naive_b.std_errors[1] < ivb.std_error))

print ""
print "== diagnostics =="
' First-stage F on one excluded instrument is the square of its t. Computed
' from an ordinary `ols` first stage, which shares nothing with _f_drop.
append(results, check("the first-stage F is the instrument's t squared", round(fs.t_values[1] * fs.t_values[1], 8), round(iva.first_stage[0].f_stat, 8)))
append(results, check("this instrument is strong", true, iva.min_first_stage_f > 1000))
append(results, check("so nothing is flagged", false, iva.weak))
append(results, check("and there is no note", "", iva.note))

' A deliberately weak instrument: it moves x barely more than the noise does.
wz = []
wx = []
wy = []
i = 0
while i < 60
    zi = mod(i, 3) - 1
    w = 1.0 * (mod(i * 7, 13) - 6)
    append(wz, zi)
    append(wx, 1 + 0.15 * zi + w)
    append(wy, 3 + 1.5 * (1 + 0.15 * zi + w) - 1.5 * w)
    i += 1
end while
wiv = stats.iv_2sls(wy, wx, wz, {})
append(results, check("a weak instrument still produces an estimate", true, wiv.ok))
append(results, check("its first-stage F is below ten", true, wiv.first_stage[0].f_stat < 10))
append(results, check("it is flagged weak", true, wiv.weak))
append(results, check("and says why", true, contains(wiv.note, "weak instrument")))

print ""
print "-- Wu-Hausman: was the regressor endogenous at all?"
' Against the same F, rebuilt here from two residual sums of squares.
vhat = fs.residuals
aug = stats.ols(iya, [ix, vhat])
plain = stats.ols(iya, ix)
rss_u = 0
for each r in aug.residuals
    rss_u = rss_u + r * r
next r
rss_r = 0
for each r in plain.residuals
    rss_r = rss_r + r * r
next r
hand_f = ((rss_r - rss_u) / 1) / (rss_u / (60 - 3))
append(results, check("the reported F matches the augmented regression", round(hand_f, 8), round(iva.wu_hausman.f_stat, 8)))
append(results, check("and it rejects exogeneity", true, iva.wu_hausman.p_value < 0.001))

' The control: an x that is NOT endogenous must not be reported as if it were.
cy = []
cx = []
cz = []
i = 0
while i < 60
    zi = mod(i, 3) - 1
    v = 0.7 * (mod(i * 5, 11) - 5)
    xi = 1 + 1.8 * zi + v
    append(cz, zi)
    append(cx, xi)
    append(cy, 3 + 1.5 * xi + 0.4 * (mod(i * 3, 7) - 3))   ' noise from neither
    i += 1
end while
civ = stats.iv_2sls(cy, cx, cz, {})
append(results, check("an exogenous regressor is not flagged endogenous", true, civ.wu_hausman.p_value > 0.10))
append(results, check("and OLS and IV then agree closely", true, abs(stats.ols(cy, cx).coefficients[1] - civ.estimate) < 0.05))

print ""
print "-- Sargan: only where there is something to test"
' Exact identification has no overidentifying restriction, so there is no
' statistic. Reporting one would be reporting a tautology as evidence.
append(results, check("exactly identified reports no Sargan statistic", true, is_unknown(iva.sargan)))

oz1 = []
oz2 = []
ow = []
ox = []
oy = []
i = 0
while i < 80
    a = mod(i, 3) - 1
    b = mod(i * 7, 5) - 2
    w = 0.4 * (mod(i, 11) - 5)
    xi = 1 + 1.5 * a + 0.8 * b + w
    append(oz1, a)
    append(oz2, b)
    append(ow, w)
    append(ox, xi)
    append(oy, 3 + 1.5 * xi - 1.5 * w)
    i += 1
end while
oiv = stats.iv_2sls(oy, ox, [oz1, oz2], {})
append(results, check("two instruments for one regressor is overidentified by one", 1, oiv.overidentified))
append(results, check("so a Sargan statistic exists", 1, oiv.sargan.df))
' J = n R^2 from regressing the structural residuals on every instrument.
sg = stats.ols(oiv.residuals, [oz1, oz2])
append(results, check("J is n times that regression's R-squared", round(80 * sg.r_squared, 8), round(oiv.sargan.statistic, 8)))
append(results, check("valid instruments are not rejected", true, oiv.sargan.p_value > 0.10))

' The same regressor, instrumented partly by the confounder itself.
biv = stats.iv_2sls(oy, ox, [oz1, ow], {})
append(results, check("an invalid instrument IS rejected", true, biv.sargan.p_value < 0.01))
append(results, check("and its estimate is badly wrong", true, abs(biv.estimate - 1.5) > 0.5))
append(results, check("while the valid pair is close", true, abs(oiv.estimate - 1.5) < 0.1))

print ""
print "-- an included exogenous control instruments itself"
' The three inputs are laid out on nested cycles that divide 60 exactly -- the
' instrument turns over every 4 rows, the control every 4th row over 5 values,
' the confounder every 20 -- so all three are EXACTLY orthogonal in this
' sample, not merely uncorrelated in expectation. 2SLS must therefore recover
' 1.5 and 0.8 to six digits, and a failure is arithmetic rather than luck.
ey = []
ex = []
ec = []
ez = []
ev = []
i = 0
while i < 60
    zi = mod(i, 4) - 1.5
    ci = mod(floor(i / 4), 5) - 2
    vi = mod(floor(i / 20), 3) - 1
    xi = 1 + 1.6 * zi + 0.7 * ci + vi
    append(ez, zi)
    append(ec, ci)
    append(ev, vi)
    append(ex, xi)
    append(ey, 2 + 1.5 * xi + 0.8 * ci - 1.2 * vi)
    i += 1
end while
eiv = stats.iv_2sls(ey, ex, ez, { exog: ec })
append(results, check("it fits with a control", true, eiv.ok))
append(results, check("one exogenous column", 1, eiv.exogenous))
append(results, check("the endogenous coefficient is recovered", 1.5, round(eiv.estimate, 6)))
append(results, check("and so is the control's", 0.8, round(eiv.coefficients[2], 6)))
append(results, check("df counts all three parameters", 57, eiv.df))
' The first-stage F must test the EXCLUDED instrument only. With a control in
' the first stage a joint F over both columns is also a perfectly ordinary
' number, and the no-control fixture above cannot tell the two apart because
' there the two tests are the same test.
efs = stats.ols(ex, [ec, ez])
append(results, check("the first-stage F excludes the control", round(efs.t_values[2] * efs.t_values[2], 8), round(eiv.first_stage[0].f_stat, 8)))
append(results, check("which is not the joint F over both columns", false, round(eiv.first_stage[0].f_stat, 4) = round(efs.t_values[1] * efs.t_values[1], 4)))
append(results, check("and it tests one restriction", 1, eiv.first_stage[0].df1))
' Wu-Hausman with a control present, rebuilt the same way: the augmented
' regression adds the first-stage residuals to y = a + b*x + c*control, and
' only THOSE are restricted. Restricting the control alongside them is another
' ordinary-looking F, and the no-control fixture cannot tell the two apart.
eaug = stats.ols(ey, [ex, ec, efs.residuals])
epln = stats.ols(ey, [ex, ec])
eru = 0
for each r in eaug.residuals
    eru = eru + r * r
next r
err2 = 0
for each r in epln.residuals
    err2 = err2 + r * r
next r
ehf = ((err2 - eru) / 1) / (eru / (60 - 4))
append(results, check("Wu-Hausman restricts only the first-stage residuals", round(ehf, 8), round(eiv.wu_hausman.f_stat, 8)))
append(results, check("on one and 56 degrees of freedom", true, eiv.wu_hausman.df1 = 1 and eiv.wu_hausman.df2 = 56))
append(results, check("and this regressor really is endogenous", true, eiv.wu_hausman.p_value < 0.001))
' OLS on the same data is not: the confounder is still in the disturbance.
eols = stats.ols(ey, [ex, ec])
append(results, check("OLS does not recover it", false, round(eols.coefficients[1], 6) = 1.5))
' Dropping the control does NOT bias the estimate here, and saying otherwise
' would be the easy wrong lesson: the control is orthogonal to the instrument,
' so omitting it moves 0.8*c into the disturbance where the instrument is still
' valid. What it costs is PRECISION -- the disturbance is bigger, so the
' interval is wider for the same answer.
enc = stats.iv_2sls(ey, ex, ez, {})
append(results, check("dropping an orthogonal control leaves the estimate alone", 1.5, round(enc.estimate, 6)))
append(results, check("but the standard error grows", true, enc.std_error > eiv.std_error))

print ""
print "== pre-trends: the testable half of parallel trends =="
' Twenty-four units, six periods, treatment starting at period 5. Under
' genuinely parallel pre-trends the leads are jointly indistinguishable from
' zero; when the treated group is already pulling away they are not.
function make_panel(drift)
    ys = []
    ts = []
    ps = []
    u = 0
    while u < 24
        t = 0
        if u >= 12 then
            t = 1
        end if
        p = 1
        while p <= 6
            append(ys, 10 + 0.5 * p + 0.3 * t + drift * t * p + 0.2 * (mod(u + p, 5) - 2))
            append(ts, t)
            append(ps, p)
            p += 1
        end while
        u += 1
    end while
    return { y: ys, t: ts, p: ps }
end function

flat = make_panel(0)
pf = stats.pre_trends(flat.y, flat.t, flat.p, 5)
append(results, check("it fits", true, pf.ok))
append(results, check("four pre-periods", 4, pf.periods))
append(results, check("the last is the omitted reference", 4, pf.reference))
append(results, check("so three leads are tested", 3, pf.df1))
append(results, check("and three are reported", 3, count(pf.leads)))
append(results, check("the first lead is period 1", 1, pf.leads[0].period))
append(results, check("parallel trends are not rejected", true, pf.p_value > 0.10))
append(results, check("only the pre-period rows are used", 96, pf.n))
append(results, check("and the result refuses to be read as proof", true, contains(pf.note, "not evidence for it")))

tilt = make_panel(0.35)
pt2 = stats.pre_trends(tilt.y, tilt.t, tilt.p, 5)
append(results, check("a diverging treated group is caught", true, pt2.p_value < 0.001))
append(results, check("and the leads carry the drift", true, pt2.leads[0].coefficient < -0.5))
append(results, check("the ESTIMATE is untouched by any of this", true, pt2.ok))

' The F, rebuilt here from two ordinary fits over the same columns.
d1 = []
d2 = []
d3 = []
l1 = []
l2 = []
l3 = []
tt = []
yy2 = []
i = 0
while i < len(flat.y)
    if flat.p[i] < 5 then
        append(yy2, flat.y[i])
        append(tt, flat.t[i])
        a1 = 0
        a2 = 0
        a3 = 0
        if flat.p[i] = 1 then
            a1 = 1
        end if
        if flat.p[i] = 2 then
            a2 = 1
        end if
        if flat.p[i] = 3 then
            a3 = 1
        end if
        append(d1, a1)
        append(d2, a2)
        append(d3, a3)
        append(l1, a1 * flat.t[i])
        append(l2, a2 * flat.t[i])
        append(l3, a3 * flat.t[i])
    end if
    i += 1
end while
fu = stats.ols(yy2, [tt, d1, d2, d3, l1, l2, l3])
fr = stats.ols(yy2, [tt, d1, d2, d3])
ru = 0
for each r in fu.residuals
    ru = ru + r * r
next r
rr = 0
for each r in fr.residuals
    rr = rr + r * r
next r
hf = ((rr - ru) / 3) / (ru / (96 - 8))
append(results, check("the reported F is that F", round(hf, 8), round(pf.f_stat, 8)))
append(results, check("on 3 and 88 degrees of freedom", true, pf.df1 = 3 and pf.df2 = 88))

print ""
print "== refusals: each one names what is wrong =="
append(results, check("under-identification is named by count", "under-identified: 1 excluded instrument(s) for 2 endogenous regressor(s)", stats.iv_2sls(iya, [ix, iz], iz, {}).message))
append(results, check("an empty cell is refused", false, stats.did([1,2,3,4], [0,0,1,1], [0,1,0,0], {}).ok))
append(results, check("and named", "no observations in the treated_post cell", stats.did([1,2,3,4], [0,0,1,1], [0,1,0,0], {}).message))
append(results, check("a length mismatch is named", true, contains(stats.did(dy, [0,1], dp, {}).message, "treated has 2 rows")))
append(results, check("a bad covariance option is named", true, contains(stats.did(dy, dt, dp, { hc: "HC9" }).message, "HC0, HC1, HC2 or HC3")))
append(results, check("an empty y is refused", false, stats.did([], [], [], {}).ok))
append(results, check("too few observations is refused", false, stats.iv_2sls([1,2], [1,2], [1,2], {}).ok))
append(results, check("a single pre-period cannot be tested", true, contains(stats.pre_trends(flat.y, flat.t, flat.p, 2).message, "at least two pre-treatment periods")))
append(results, check("no pre-periods at all is refused", false, stats.pre_trends(flat.y, flat.t, flat.p, 1).ok))
append(results, check("a cluster column of the wrong length is refused", true, contains(stats.did(dy, dt, dp, { cluster: [1,2] }).message, "cluster has 2 rows")))
append(results, check("an instrument column of the wrong length is refused", true, contains(stats.iv_2sls(iya, ix, [1,2,3], {}).message, "instrument column")))
append(results, check("a duplicated instrument is refused", true, contains(stats.iv_2sls(oy, ox, [oz1, oz1], {}).message, "singular")))

print ""
print "== robust covariance options still work here =="
h3 = stats.did(py, pt, pp, { hc: "HC3" })
append(results, check("HC3 fits", true, h3.ok))
append(results, check("labelled", "HC3", h3.cov_type))
append(results, check("same estimate", round(cls.att, 12), round(h3.att, 12)))
append(results, check("different standard error", false, round(h3.std_error, 8) = round(cls.std_error, 8)))
ivh = stats.iv_2sls(iya, ix, iz, { hc: "HC1" })
append(results, check("2SLS takes them too", "HC1", ivh.cov_type))
append(results, check("with the same estimate", round(iva.estimate, 12), round(ivh.estimate, 12)))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
