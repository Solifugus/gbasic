' Meta-analysis in stats.bas. Golden-compared by tests/run_meta_analysis.sh.
'
' The pooled numbers below are not recorded from a run: they were computed
' INDEPENDENTLY in python from the same inputs and are stated here as literals.
' (My own hand arithmetic for the random-effects estimate was wrong in the 5th
' decimal, which is exactly why the oracle is a separate implementation and not
' a careful person.)
'
' Inputs: three studies, effect/variance 0.5/0.04, 0.3/0.02, 0.7/0.05.
'   fixed  = 0.4368421053   se = 0.1025978352
'   Q      = 2.4210526316   tau^2 = 0.0072727273   I^2 = 17.391304
'   random = 0.4489741302   se = 0.1152544758

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

studies = [
    { effect: 0.5, variance: 0.04 },
    { effect: 0.3, variance: 0.02 },
    { effect: 0.7, variance: 0.05 }
]

print "-- fixed effect, against an independent computation"
f = stats.meta_analysis(studies, { model: "fixed" })
append(results, check("it pools", true, f.ok))
append(results, check("estimate", 0.4368421053, round(f.estimate, 10)))
append(results, check("standard error", 0.1025978352, round(f.se, 10)))
append(results, check("weights are inverse-variance, in percent", 26.3157895, round(f.weights[0], 7)))
append(results, check("weights sum to 100", 100, round(f.weights[0] + f.weights[1] + f.weights[2], 6)))

print ""
print "-- heterogeneity is reported whichever model is asked for"
append(results, check("Cochran's Q", 2.4210526316, round(f.q, 10)))
append(results, check("its df", 2, f.df))
append(results, check("I squared", 17.391304, round(f.i_squared, 6)))
append(results, check("tau squared", 0.0072727273, round(f.tau_squared, 10)))

print ""
print "-- random effects (DerSimonian-Laird)"
r = stats.meta_analysis(studies, { model: "random" })
append(results, check("estimate", 0.4489741302, round(r.estimate, 10)))
append(results, check("standard error", 0.1152544758, round(r.se, 10)))
' Random effects must be at least as wide as fixed: it adds between-study
' variance to every weight. Narrower would mean tau-squared went negative.
append(results, check("it is never narrower than fixed", true, r.se >= f.se))
append(results, check("the same heterogeneity is reported", 2.4210526316, round(r.q, 10)))

print ""
print "-- identical studies: an invariant, not a recorded number"
same = [ { effect: 0.4, variance: 0.02 }, { effect: 0.4, variance: 0.02 }, { effect: 0.4, variance: 0.02 } ]
hf = stats.meta_analysis(same, { model: "fixed" })
hr = stats.meta_analysis(same, { model: "random" })
append(results, check("no heterogeneity", 0, hr.tau_squared))
append(results, check("I squared is zero", 0, hr.i_squared))
' With tau-squared zero the two models are the same estimator, so they must
' agree EXACTLY -- a stronger statement than any single expected value.
append(results, check("random and fixed coincide", true, hr.estimate = hf.estimate))
append(results, check("and so do their errors", true, hr.se = hf.se))
append(results, check("the estimate is the common effect", 0.4, round(hf.estimate, 10)))

print ""
print "-- RATIO MEASURES POOL ON THE LOG SCALE"
' Halving and doubling are the same size of effect in opposite directions, so
' the truth is NO effect. Pooled as raw numbers they average to 1.25, which
' reads as a 25% harm -- a wrong answer that looks entirely ordinary, and one
' no inspection of the values could reveal.
opposite = [ { effect: 0.5, variance: 0.04 }, { effect: 2.0, variance: 0.04 } ]
raw = stats.meta_analysis(opposite, { model: "fixed" })
lg  = stats.meta_analysis(opposite, { model: "fixed", scale: "ratio" })
append(results, check("pooled as plain numbers it reads as an effect", 1.25, round(raw.estimate, 10)))
append(results, check("pooled on the log scale it is exactly none", 1, round(lg.estimate, 10)))
append(results, check("the interval is back-transformed too", true, lg.ci_low < 1 and lg.ci_high > 1))
append(results, check("and the log-scale estimate is kept", 0, round(lg.log_estimate, 10)))

print ""
print "-- refusals"
append(results, check("a non-positive variance is infinite weight", false, stats.meta_analysis([{effect: 1, variance: 0},{effect: 2, variance: 0.1}], {}).ok))
append(results, check("a study with neither variance nor se", false, stats.meta_analysis([{effect: 1},{effect: 2, variance: 0.1}], {}).ok))
append(results, check("fewer than two studies", false, stats.meta_analysis([{effect: 1, variance: 0.1}], {}).ok))
append(results, check("an unknown model is named", false, stats.meta_analysis(studies, { model: "bayes" }).ok))
append(results, check("an unknown scale is named", false, stats.meta_analysis(studies, { scale: "logit" }).ok))
append(results, check("a non-positive ratio cannot be logged", false, stats.meta_analysis([{effect: -1, variance: 0.1},{effect: 2, variance: 0.1}], { scale: "ratio" }).ok))
append(results, check("a non-record study", false, stats.meta_analysis([5, {effect: 2, variance: 0.1}], {}).ok))

print ""
print "-- se is accepted in place of variance"
b = stats.meta_analysis([{effect: 0.5, se: 0.2},{effect: 0.3, se: 0.1414213562373095}], { model: "fixed" })
append(results, check("same answer as the equivalent variances", true, b.ok))
append(results, check("and it agrees to 8 places", round(stats.meta_analysis([{effect: 0.5, variance: 0.04},{effect: 0.3, variance: 0.02}], {model:"fixed"}).estimate, 8), round(b.estimate, 8)))

print ""
print "-- smd_variance, the bridge from a reported d"
' var(d) = (n1+n2)/(n1*n2) + d^2/(2(n1+n2)); with d=0.5, n1=n2=50:
'   100/2500 + 0.25/200 = 0.04 + 0.00125 = 0.04125
append(results, check("variance of a standardized mean difference", 0.04125, round(stats.smd_variance(0.5, 50, 50), 10)))
append(results, check("a zero group size is refused", true, is_unknown(stats.smd_variance(0.5, 0, 50))))

print ""
print "-- Egger's test says when it cannot say much"
e = stats.eggers_test(studies)
append(results, check("it runs", true, e.ok))
append(results, check("and warns that 3 studies is too few", true, contains(e.note, "not informative")))
append(results, check("fewer than three is refused", false, stats.eggers_test([{effect: 1, variance: 0.1}, {effect: 2, variance: 0.1}]).ok))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
