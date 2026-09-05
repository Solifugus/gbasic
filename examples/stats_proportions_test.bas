' stats.bas Phase 9 — proportions & the business lens
' (docs/statistics_scientist_plan.md §9). One/two-sample proportion z-tests,
' A/B test, RFM, conversion funnel, cohort retention. The proportion tests are
' verified against statsmodels proportions_ztest; the business conveniences are
' deterministic. gBASIC prints ~6 sig figs, so results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' One-sample proportion z-test.
    p1 = stats.prop_test_1(30, 200, 0.1)
    print("prop1 z " + string(round(p1.z, 6)) + " p " + string(round(p1.p_value, 6)) + " ci " + string(round(p1.ci_low, 6)) + " " + string(round(p1.ci_high, 6)))

    ' Two-sample proportion z-test.
    p2 = stats.prop_test_2(45, 100, 30, 100)
    print("prop2 z " + string(round(p2.z, 6)) + " p " + string(round(p2.p_value, 6)) + " diff " + string(round(p2.diff, 6)) + " ci " + string(round(p2.ci_low, 6)) + " " + string(round(p2.ci_high, 6)))

    ' A/B test convenience.
    ab = stats.ab_test({ successes: 120, n: 1000 }, { successes: 150, n: 1000 })
    print("ab lift " + string(round(ab.lift, 6)) + " diff " + string(round(ab.diff, 6)) + " p " + string(round(ab.p_value, 6)) + " sig " + string(ab.significant))

    ' Conversion funnel.
    fn = stats.funnel([1000, 600, 350, 200])
    i = 0
    while i < len(fn)
        row = fn[i]
        print("funnel " + string(row.stage) + " count " + string(row.count) + " conv " + string(round(row.conversion, 6)) + " cum " + string(round(row.cumulative, 6)) + " drop " + string(round(row.dropoff, 6)))
        i = i + 1
    end while

    ' RFM analysis.
    tx = [ { customer: 1, day: 1, amount: 50 }, { customer: 1, day: 5, amount: 30 }, { customer: 1, day: 9, amount: 20 }, { customer: 2, day: 2, amount: 200 }, { customer: 3, day: 8, amount: 40 }, { customer: 3, day: 10, amount: 60 }, { customer: 4, day: 3, amount: 500 }, { customer: 4, day: 7, amount: 100 }, { customer: 4, day: 10, amount: 100 } ]
    rf = stats.rfm(tx)
    i = 0
    while i < len(rf)
        row = rf[i]
        print("rfm c" + string(row.customer) + " r " + string(row.recency) + " f " + string(row.frequency) + " m " + string(row.monetary) + " score " + string(row.rfm))
        i = i + 1
    end while

    ' Cohort retention.
    ev = [ { customer: 1, cohort: 0, period: 0 }, { customer: 2, cohort: 0, period: 0 }, { customer: 3, cohort: 0, period: 0 }, { customer: 1, cohort: 0, period: 1 }, { customer: 2, cohort: 0, period: 1 }, { customer: 1, cohort: 0, period: 2 }, { customer: 4, cohort: 1, period: 0 }, { customer: 5, cohort: 1, period: 0 }, { customer: 4, cohort: 1, period: 1 } ]
    ch = stats.cohort_retention(ev)
    i = 0
    while i < len(ch)
        row = ch[i]
        line = "cohort " + string(row.cohort) + " size " + string(row.size) + " ret"
        j = 0
        while j < len(row.retention)
            line = line + " " + string(round(row.retention[j], 6))
            j = j + 1
        end while
        print(line)
        i = i + 1
    end while

    ' domain guards return unknown
    print("guard_prop1 " + string(stats.prop_test_1(5, 0, 0.5)))
    print("guard_funnel " + string(stats.funnel([])))
    print("guard_rfm " + string(stats.rfm([])))
end program
