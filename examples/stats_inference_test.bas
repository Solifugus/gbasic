' stats.bas Phase 2 — inferential statistics (statistics_design.md §8 Phase 2).
' Hypothesis tests, ANOVA, chi-squared, nonparametric rank tests, confidence
' intervals, effect sizes, multiple-comparison corrections, and GLM (logistic
' / Poisson via IRLS). Every value checked against scipy.stats / statsmodels;
' gBASIC prints ~6 significant figures (the cross-architecture determinism
' rule), so results are rounded to 6 decimals before printing.
program demo(args)
    load stats from "../stdlib/stats.bas"

    xs = [5.1, 4.9, 6.2, 5.7, 5.5, 6.1, 4.8, 5.9]
    a = [23, 25, 21, 30, 28, 26]
    b = [19, 22, 20, 18, 24, 21]
    pre = [120, 118, 130, 125, 122]
    post = [115, 116, 128, 119, 121]
    g1 = [1, 2, 3, 4, 5, 6]
    g2 = [2, 4, 6, 8, 10, 12]
    g3 = [1, 3, 5, 7, 9, 11]

    ' --- t-tests ---
    r1 = stats.t_test_1sample(xs, 5.0)
    print("t1 stat " + string(round(r1.statistic, 6)) + " df " + string(r1.df) + " p " + string(round(r1.p_value, 6)))
    r2 = stats.t_test_2sample(a, b)
    print("t2 stat " + string(round(r2.statistic, 6)) + " df " + string(r2.df) + " p " + string(round(r2.p_value, 6)))
    r3 = stats.t_test_welch(a, b)
    print("welch stat " + string(round(r3.statistic, 6)) + " df " + string(round(r3.df, 6)) + " p " + string(round(r3.p_value, 6)))
    r4 = stats.t_test_paired(pre, post)
    print("paired stat " + string(round(r4.statistic, 6)) + " df " + string(r4.df) + " p " + string(round(r4.p_value, 6)))

    ' --- effect size & confidence interval ---
    print("cohend " + string(round(stats.cohens_d(a, b), 6)))
    ci = stats.confidence_interval(xs, 0.95)
    print("ci mean " + string(round(ci.mean, 6)) + " lo " + string(round(ci.lower, 6)) + " hi " + string(round(ci.upper, 6)))

    ' --- ANOVA ---
    av = stats.anova_oneway([g1, g2, g3])
    print("anova F " + string(round(av.statistic, 6)) + " dfb " + string(av.df_between) + " dfw " + string(av.df_within) + " p " + string(round(av.p_value, 6)))

    ' --- chi-squared ---
    gof = stats.chi_square_gof([18, 22, 20, 25, 15], [20, 20, 20, 20, 20])
    print("gof stat " + string(round(gof.statistic, 6)) + " df " + string(gof.df) + " p " + string(round(gof.p_value, 6)))
    ind = stats.chi_square_independence([[10, 20, 30], [6, 9, 17]])
    print("indep stat " + string(round(ind.statistic, 6)) + " df " + string(ind.df) + " p " + string(round(ind.p_value, 6)) + " e00 " + string(round(ind.expected[0][0], 6)))

    ' --- nonparametric ---
    mw = stats.mann_whitney(a, b)
    print("mw U " + string(round(mw.u, 6)) + " p " + string(round(mw.p_value, 6)))
    wil = stats.wilcoxon(pre, post)
    print("wil stat " + string(round(wil.statistic, 6)) + " p " + string(round(wil.p_value, 6)))
    kw = stats.kruskal_wallis([g1, g2, g3])
    print("kw H " + string(round(kw.statistic, 6)) + " df " + string(kw.df) + " p " + string(round(kw.p_value, 6)))

    ' --- multiple comparisons ---
    pvals = [0.01, 0.04, 0.03, 0.005, 0.2]
    bf = stats.bonferroni(pvals)
    print("bonf " + string(round(bf[0], 6)) + " " + string(round(bf[1], 6)) + " " + string(round(bf[2], 6)) + " " + string(round(bf[3], 6)) + " " + string(round(bf[4], 6)))
    bh = stats.benjamini_hochberg(pvals)
    print("bh " + string(round(bh[0], 6)) + " " + string(round(bh[1], 6)) + " " + string(round(bh[2], 6)) + " " + string(round(bh[3], 6)) + " " + string(round(bh[4], 6)))

    ' --- GLM: logistic regression ---
    yl = [0, 0, 0, 1, 0, 1, 1, 1, 1, 1]
    x1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    lr = stats.logistic_regression(yl, x1)
    print("logit coef " + string(round(lr.coefficients[0], 6)) + " " + string(round(lr.coefficients[1], 6)))
    print("logit se " + string(round(lr.std_errors[0], 6)) + " " + string(round(lr.std_errors[1], 6)))
    print("logit z " + string(round(lr.z_values[0], 6)) + " " + string(round(lr.z_values[1], 6)))
    print("logit p " + string(round(lr.p_values[0], 6)) + " " + string(round(lr.p_values[1], 6)))
    print("logit ll " + string(round(lr.log_likelihood, 6)) + " conv " + string(lr.converged))

    ' --- GLM: Poisson regression ---
    yp = [1, 2, 1, 3, 4, 5, 4, 6, 7, 8]
    xp = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    pr = stats.poisson_regression(yp, xp)
    print("pois coef " + string(round(pr.coefficients[0], 6)) + " " + string(round(pr.coefficients[1], 6)))
    print("pois se " + string(round(pr.std_errors[0], 6)) + " " + string(round(pr.std_errors[1], 6)))
    print("pois z " + string(round(pr.z_values[0], 6)) + " " + string(round(pr.z_values[1], 6)))
    print("pois p " + string(round(pr.p_values[0], 6)) + " " + string(round(pr.p_values[1], 6)))
    print("pois ll " + string(round(pr.log_likelihood, 6)) + " conv " + string(pr.converged))

    ' --- domain guards return unknown ---
    print("guard_anova " + string(stats.anova_oneway([[1, 2, 3]])))
    print("guard_t " + string(stats.t_test_1sample([5], 0)))
end program
