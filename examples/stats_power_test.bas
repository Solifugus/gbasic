' stats.bas Phase 7 power analysis (docs/statistics_scientist_plan.md,
' cross-cutting). Noncentral t / F distributions and power/sample-size for
' t-tests and one-way ANOVA. Values verified against scipy.stats.nct /
' scipy.stats.ncf and statsmodels.stats.power; gBASIC prints ~6 sig figs so
' results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Noncentral t CDF vs scipy.stats.nct.cdf
    print("nct 2,10,1.5 " + string(round(nct_cdf(2, 10, 1.5), 6)))
    print("nct 0.5,20,2 " + string(round(nct_cdf(0.5, 20, 2), 6)))
    print("nct -1,15,0.5 " + string(round(nct_cdf(-1, 15, 0.5), 6)))

    ' Noncentral F CDF vs scipy.stats.ncf.cdf
    print("ncf 2,3,20,5 " + string(round(ncf_cdf(2, 3, 20, 5), 6)))
    print("ncf 4,4,12,10 " + string(round(ncf_cdf(4, 4, 12, 10), 6)))

    ' Two-sample t-test power (per-group n) vs statsmodels TTestIndPower
    print("pow_ind 0.5,30,2 " + string(round(power_ttest(0.5, 30, 0.05, 2), 6)))
    print("pow_ind 0.8,20,2 " + string(round(power_ttest(0.8, 20, 0.05, 2), 6)))
    print("pow_ind 0.5,30,1 " + string(round(power_ttest(0.5, 30, 0.05, 1), 6)))

    ' Paired / one-sample t-test power vs statsmodels TTestPower
    print("pow_paired 0.5,30,2 " + string(round(power_ttest_paired(0.5, 30, 0.05, 2), 6)))
    print("pow_paired 0.8,15,2 " + string(round(power_ttest_paired(0.8, 15, 0.05, 2), 6)))

    ' One-way ANOVA power (Cohen's f) vs statsmodels FTestAnovaPower
    print("pow_anova 3,20,0.25 " + string(round(power_anova(3, 20, 0.25, 0.05), 6)))
    print("pow_anova 4,15,0.4 " + string(round(power_anova(4, 15, 0.4, 0.05), 6)))

    ' Sample size (per group, two-sample) to reach target power
    print("n 0.5,0.8,2 " + string(sample_size_ttest(0.5, 0.8, 0.05, 2)))
    print("n 0.8,0.9,2 " + string(sample_size_ttest(0.8, 0.9, 0.05, 2)))
    print("n 0.3,0.8,1 " + string(sample_size_ttest(0.3, 0.8, 0.05, 1)))

    ' domain guards return unknown
    print("guard_nct " + string(nct_cdf(1, 0, 1)))
    print("guard_anova " + string(power_anova(1, 20, 0.25, 0.05)))
    print("guard_n " + string(sample_size_ttest(0, 0.8, 0.05, 2)))
end program
