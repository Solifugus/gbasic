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
function fires(seed, shape, regions, cats, nullkind)
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
            comparison: "period_over_period", null: nullkind })
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
        if fires(t * 37, shape, regions, cats, nullkind) then
            hits = hits + 1
        end if
    next
    return hits / trials
end function
