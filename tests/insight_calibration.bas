' PROPERTY TEST: does the threshold mean what the Finding says it means?
'
' Every Finding asserts `alpha` and a `correction`, and R1, R11, R12 and every
' `clears` verdict rest on that number. Nothing verified it until this existed,
' and when it was finally measured the answer was NO: 0.09, 0.11 and 0.143
' against a requested 0.05, worsening as the search widened -- the opposite of
' what a family-wise correction is for.
'
' THE MEASUREMENT IS THE TEST. Run the search over data where NOTHING is
' planted and count how often ANY cell clears. That is the family-wise
' false-positive rate by definition, and it is the only way to check a
' correction rather than trust its formula.
'
' Deterministic: every trial's data comes from an explicit seed, so this is a
' fixed computation and not a flaky one. The tolerance is wide on purpose --
' 200 trials estimate a 0.05 rate to about +/- 0.015 -- so the tier catches
' being 2-3x wrong, which is the failure that happened, and does not chase
' sampling noise.
'
' distribution matters and the tier says so: the parametric threshold is exact
' for light-tailed data and anti-conservative for heavy-tailed. Both are
' asserted, because a test on only one of them would have passed all along.

load insight
load frame
load fake

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function report(label, rate, lo, hi)
    tally.checks = tally.checks + 1
    if rate >= lo and rate <= hi then
        print ("ok   " + label + " = " + string(round(rate, 3))
               + "  (in [" + string(lo) + ", " + string(hi) + "])")
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + " = " + string(round(rate, 3))
               + ", wanted [" + string(lo) + ", " + string(hi) + "]")
    end if
    return nothing
end function

' One null trial: nothing planted anywhere.
function fires(seed, shape, regions, cats, nullkind, reps)
    load fake
    load frame
    load insight
    rows = []
    cell = 0
    for r = 1 to regions
        for c = 1 to cats
            for d = 1 to 8
                for p = 0 to 1
                    i = p * 200000 + cell * 300 + d
                    v = fake.between(seed, i, 500, 1500)
                    if shape = "heavy" then
                        v = fake.lognormal(seed, i, 1000, 0.5)
                    end if
                    append(rows, { region: "r" + string(r), category: "c" + string(c),
                                   period: p, revenue: v })
                next
            next
            cell = cell + 1
        next
    next
    f = insight.explain_change(frame.from_rows(rows),
          { measure: "revenue", period: "period", baseline: 0, current: 1,
            dimensions: ["region", "category"],
            comparison: "period_over_period", null: nullkind,
            repetitions: reps })
    for each ct in f.contributors
        if ct.clears then
            return true
        end if
    next
    return false
end function

function rate_of(shape, regions, cats, nullkind, trials)
    hits = 0
    for t = 1 to trials
        if fires(t * 37, shape, regions, cats, nullkind, 1) then
            hits = hits + 1
        end if
    next
    return hits / trials
end function

' --- TIER: the machinery is right when its assumption holds -----------------
' The control. Under light-tailed cell changes the t threshold is exactly what
' it claims, and without this tier the two below could be read as "the
' correction is simply broken" rather than "its assumption is unmet".
light = rate_of("light", 4, 5, "siblings", 150)
report("light-tailed, t threshold", light, 0.0, 0.11)

' --- TIER: and wrong when it does not --------------------------------------
' THE DOCUMENTED LIMITATION, ASSERTED AS A FACT. `null.calibration` tells a
' reader the t threshold delivers 0.10-0.13 on revenue-like data. If that
' stopped being true the label would be wrong, so the label is tested.
heavy_t = rate_of("heavy", 4, 5, "siblings", 150)
report("heavy-tailed, t threshold", heavy_t, 0.06, 0.22)
check("which is materially above the requested 0.05", heavy_t > 0.07, true)

' --- TIER: the permuted null is better, asserted as a DIFFERENCE ------------
' Not "the permuted rate is small" -- that passes on a null that never fires,
' which is exactly the bug the first attempt shipped (sign-flipping left every
' |deviation| intact, so the threshold landed above the statistic it judged and
' the test fired 0 times in 200 trials). A difference against the t null on the
' SAME data is what distinguishes better calibration from no calibration, and
' the lower bound is what distinguishes it from silence.
heavy_p = rate_of("heavy", 4, 5, "siblings_permuted", 150)
report("heavy-tailed, permuted threshold", heavy_p, 0.005, 0.14)
check("the permuted null fires LESS often than the t null on the same data",
      heavy_p < heavy_t, true)
check("but it still fires -- it is a threshold, not a mute button",
      heavy_p > 0, true)

' --- TIER: R18, the correction is per RUN and a campaign is many runs -------
'
' Every tier above measures ONE search. A monitoring process asks the same
' question every month, and the correction has always been family-wise over
' the CELLS of one search -- twelve families, paid for once. This is not a
' consequence of the tail-weight miscalibration above: even a perfectly
' calibrated 0.05 per run is 1 - 0.95^12 = 0.46 over a year.
'
' A campaign fires if ANY month does, which is what an operator experiences.
function campaign_fires(seed, shape, regions, cats, nullkind, periods, reps)
    for m = 1 to periods
        if fires(seed * 1000 + m * 37, shape, regions, cats, nullkind, reps) then
            return true
        end if
    next
    return false
end function

function campaign_rate(shape, regions, cats, nullkind, periods, campaigns, reps)
    hits = 0
    for t = 1 to campaigns
        if campaign_fires(t, shape, regions, cats, nullkind, periods, reps) then
            hits = hits + 1
        end if
    next
    return hits / campaigns
end function

undeclared = campaign_rate("heavy", 4, 5, "siblings", 12, 40, 1)
report("a year of monthly runs, repetition UNDECLARED", undeclared, 0.55, 0.90)
check("which is most of the time -- an ordinary year raises a finding",
      undeclared > 0.5, true)

' AND THE OTHER HALF, which is what makes the first a finding rather than a
' complaint: declaring the repetition is what the library needed, and it works.
declared = campaign_rate("heavy", 4, 5, "siblings", 12, 40, 12)
report("the same year with repetitions: 12 declared", declared, 0.04, 0.36)
check("declaring it cuts the campaign false-alarm rate several fold",
      declared < undeclared / 2, true)
' AND IT DOES NOT REACH THE REQUESTED 0.05, which is the same tail-weight
' limitation the tiers above measure, now deeper into the tail. Asserted so
' the honest claim stays honest: this REDUCES the rate, it does not deliver
' alpha.
check("but it does not deliver the requested 0.05 either", declared > 0.05, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
