' Survival analysis in stats.bas. Golden-compared by tests/run_survival.sh.
'
' THE ORACLE IS EXTERNAL AND PUBLISHED. The main fixture is the Freireich 1963
' 6-mercaptopurine leukaemia remission trial -- the dataset every survival
' textbook works through -- and the expected values are its published results,
' not numbers recorded from this implementation: median remission 23 weeks on
' 6-MP against 8 on placebo, S(10) = 0.7529, S(23) = 0.4482, log-rank
' chi-square 16.79. Agreement with an outside source is a far stronger claim
' than agreement with yesterday's run of the same code.
'
' CENSORING IS THE SUBJECT, and the fixture proves why by doing it wrong on
' purpose: the same data yields a median of 23 handled correctly, 10 if the
' censored subjects are dropped, and 16 if they are counted as events. Every
' one of those is an ordinary-looking number.

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

' Freireich et al. 1963. 1 = relapse observed, 0 = still in remission (censored).
mp_t = [6,6,6,6,7,9,10,10,11,13,16,17,19,20,22,23,25,32,32,34,35]
mp_e = [1,1,1,0,1,0,1,0,0,1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0]
pl_t = [1,1,2,2,3,4,4,5,5,8,8,8,8,11,11,12,12,15,17,22,23]
pl_e = [1,1,1,1,1,1,1,1,1,1,1,1,1, 1, 1, 1, 1, 1, 1, 1, 1]

print "-- Kaplan-Meier against the published trial"
k = stats.kaplan_meier(mp_t, mp_e)
append(results, check("it estimates", true, k.ok))
append(results, check("21 subjects", 21, k.n))
append(results, check("9 relapses, the rest censored", 9, k.n_events))
append(results, check("median remission is 23 weeks", 23, k.median))
append(results, check("and it was reached", true, k.median_reached))
append(results, check("S(10) matches the textbook", 0.7529, round(stats.survival_at(k, 10), 4)))
append(results, check("S(23) matches the textbook", 0.4482, round(stats.survival_at(k, 23), 4)))

print ""
print "-- the curve's own structure"
append(results, check("the first event time is week 6", 6, k.times[0]))
append(results, check("all 21 are at risk then", 21, k.at_risk[0]))
append(results, check("3 relapse at week 6", 3, k.events[0]))
append(results, check("1 is censored at week 6", 1, k.censored[0]))
' A subject censored at t is at risk FOR the event at t -- the stated
' convention. With 21 at risk and 3 events, S = 1 - 3/21.
append(results, check("so S(6) is 1 - 3/21", 0.857142857, round(k.survival[0], 9)))
' Survival can never increase.
mono = true
i = 1
while i < len(k.survival)
    if k.survival[i] > k.survival[i-1] then
        mono = false
    end if
    i += 1
end while
append(results, check("survival never increases", true, mono))
append(results, check("before the first event survival is 1", 1, stats.survival_at(k, 0)))

print ""
print "-- the placebo arm"
p = stats.kaplan_meier(pl_t, pl_e)
append(results, check("every placebo subject relapsed", 21, p.n_events))
append(results, check("median 8 weeks, as published", 8, p.median))

print ""
print "-- log-rank against the published chi-square"
lr = stats.logrank(mp_t, mp_e, pl_t, pl_e)
append(results, check("it runs", true, lr.ok))
append(results, check("chi-square 16.79 as published", 16.793, round(lr.chi_squared, 3)))
append(results, check("on 1 degree of freedom", 1, lr.df))
append(results, check("p is far below 0.0001", true, lr.p < 0.0001))
append(results, check("9 relapses observed on 6-MP", 9, lr.observed_a))
' Far fewer than expected under a common hazard -- which IS the finding.
append(results, check("many more expected under the null", true, lr.expected_a > 19))
' Observed and expected must each total the same across the two groups.
append(results, check("observed and expected totals agree", true, round(lr.observed_a + lr.observed_b, 6) = round(lr.expected_a + lr.expected_b, 6)))
' A group compared against itself has nothing to find.
self_lr = stats.logrank(mp_t, mp_e, mp_t, mp_e)
append(results, check("a group against itself gives chi-square 0", 0, round(self_lr.chi_squared, 10)))

print ""
print "-- CENSORING, done wrong on purpose"
' Drop the censored subjects: only failures remain, so survival looks worse.
dt = []
de = []
i = 0
while i < len(mp_t)
    if mp_e[i] = 1 then
        append(dt, mp_t[i])
        append(de, 1)
    end if
    i += 1
end while
append(results, check("dropping censored subjects understates survival", 10, stats.kaplan_meier(dt, de).median))
' Count them as events: failures are invented that never happened.
alle = []
i = 0
while i < len(mp_t)
    append(alle, 1)
    i += 1
end while
append(results, check("counting them as events also understates it", 16, stats.kaplan_meier(mp_t, alle).median))
append(results, check("only the correct handling gives 23", 23, k.median))

print ""
print "-- a median that does not exist"
' Survival never falls to 0.5. Reporting the largest observed time (30) would
' understate survival by however long the study happened to run.
few_t = [5, 8, 12, 20, 30]
few_e = [1, 0,  0,  0,  0]
nm = stats.kaplan_meier(few_t, few_e)
append(results, check("the median is unknown, not the last time", true, is_unknown(nm.median)))
append(results, check("and it says it was never reached", false, nm.median_reached))
append(results, check("the curve is still returned", 0.8, round(nm.survival[0], 6)))

print ""
print "-- Greenwood standard errors"
append(results, check("an error accompanies every point", len(k.times), len(k.se)))
append(results, check("intervals are clamped into [0,1]", true, k.lower[0] >= 0 and k.upper[0] <= 1))
' VALUES, not just "positive". A wrong Greenwood formula still yields a
' positive number, so asserting positivity tests nothing -- which a red proof
' demonstrated: breaking the formula left every tier green.
' Independently computed: SE = S * sqrt(sum d/(n(n-d))).
append(results, check("Greenwood SE at week 6", 0.076360355, round(k.se[0], 9)))
append(results, check("Greenwood SE at week 7", 0.086935285, round(k.se[1], 9)))
append(results, check("Greenwood SE at week 10", 0.096349653, round(k.se[2], 9)))
append(results, check("it accumulates, never shrinking", true, k.se[3] > k.se[2]))

print ""
print "-- the median when survival lands EXACTLY on 0.5"
' Four subjects, two events at week 10: S = 0.5 precisely. The convention is
' the FIRST time survival is 0.5 OR BELOW, so the median is 10. A strict
' comparison would skip it and report 20 -- and in the Freireich data the curve
' never touches 0.5 exactly, so nothing there could tell the two apart.
half = stats.kaplan_meier([10,10,20,20], [1,1,1,1])
append(results, check("S is exactly one half at week 10", 0.5, half.survival[0]))
append(results, check("so the median is 10, not 20", 10, half.median))

print ""
print "-- refusals"
append(results, check("mismatched lengths", false, stats.kaplan_meier([1,2,3], [1,0]).ok))
append(results, check("a negative time", false, stats.kaplan_meier([1,-2], [1,0]).ok))
append(results, check("no observations", false, stats.kaplan_meier([], []).ok))
append(results, check("log-rank checks both groups", false, stats.logrank([1,2],[1,1],[3],[1,1]).ok))
append(results, check("no events at all", false, stats.logrank([1,2],[0,0],[3,4],[0,0]).ok))

print ""
print "-- Cox proportional hazards, against the PUBLISHED fit of this trial"
' Both arms pooled, treatment as the covariate (0 = 6-MP, 1 = placebo).
' Published: beta = 1.5092, hazard ratio 4.523, se 0.4096, z 3.68, p 0.00023.
' The fit is a derivative-free partial-likelihood maximisation with numerically
' differentiated standard errors, so agreeing to four decimals with the
' textbook is a claim about the method, not about this run.
ct = [6,6,6,6,7,9,10,10,11,13,16,17,19,20,22,23,25,32,32,34,35,
      1,1,2,2,3,4,4,5,5,8,8,8,8,11,11,12,12,15,17,22,23]
ce = [1,1,1,0,1,0,1,0,0,1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
      1,1,1,1,1,1,1,1,1,1,1,1,1, 1, 1, 1, 1, 1, 1, 1, 1]
cg = [0,0,0,0,0,0,0,0,0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      1,1,1,1,1,1,1,1,1,1,1,1,1, 1, 1, 1, 1, 1, 1, 1, 1]
fit = stats.cox_ph(ct, ce, cg)
append(results, check("it fits", true, fit.ok))
append(results, check("and converges", true, fit.converged))
append(results, check("beta as published", 1.5092, round(fit.coefficients[0], 4)))
append(results, check("hazard ratio as published", 4.523, round(fit.hazard_ratios[0], 3)))
append(results, check("standard error as published", 0.4096, round(fit.std_errors[0], 4)))
append(results, check("z as published", 3.685, round(fit.z_values[0], 3)))
append(results, check("p as published", 0.00023, round(fit.p_values[0], 5)))
append(results, check("42 subjects, 30 events", true, fit.n = 42 and fit.n_events = 30))
append(results, check("the tie handling is stated", "breslow", fit.ties))
' The hazard ratio must be exp(beta) -- an identity, not an observation.
append(results, check("the hazard ratio is exp(beta)", round(exp(fit.coefficients[0]), 9), round(fit.hazard_ratios[0], 9)))
' The interval must bracket the point estimate and exclude 1 at this p.
append(results, check("the interval brackets the estimate", true, fit.ci_low[0] < fit.hazard_ratios[0] and fit.hazard_ratios[0] < fit.ci_high[0]))
append(results, check("and excludes 1", true, fit.ci_low[0] > 1))

print ""
print "-- the SCALING trap: a hazard ratio is PER UNIT"
' The identical grouping measured in a unit 10000x smaller, as a covariate in
' dollars would be. The effect is unchanged; the per-unit number is 1.00015,
' which reads as nothing at all.
tiny = []
for each v in cg
    append(tiny, v * 10000)
next v
big = stats.cox_ph(ct, ce, tiny)
append(results, check("per unit it reads as no effect", 1.000151, round(big.hazard_ratios[0], 6)))
append(results, check("over a stated interval it is the same effect", 4.523, round(stats.hr_per(big, 0, 10000), 3)))
append(results, check("hr_per refuses a covariate that is not there", true, is_unknown(stats.hr_per(big, 5, 1))))

print ""
print "-- two covariates"
noise = []
i = 0
while i < len(ct)
    append(noise, mod(i, 3))
    i += 1
end while
mfit = stats.cox_ph(ct, ce, [cg, noise])
append(results, check("both coefficients come back", 2, len(mfit.coefficients)))
append(results, check("the treatment effect survives", true, mfit.p_values[0] < 0.001))
append(results, check("an uninformative covariate does not", true, mfit.p_values[1] > 0.05))

print ""
print "-- Cox refusals"
append(results, check("mismatched event flags", false, stats.cox_ph([1,2,3], [1,1], cg).ok))
append(results, check("a covariate of the wrong length", false, stats.cox_ph([1,2], [1,1], [[1]]).ok))
append(results, check("no events at all", false, stats.cox_ph([1,2], [0,0], [1,2]).ok))
append(results, check("no covariates", false, stats.cox_ph([1,2], [1,1], []).ok))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
