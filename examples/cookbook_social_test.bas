' Cookbook — Social & Behavioral Sciences (communication, psychology,
' sociology, political science, education, PR/marketing). Runnable companion to
' docs/cookbook_social_behavioral.md: every recipe below executes, so the
' snippets in the cookbook are known-good. Small fixed datasets are used for
' speed; the idioms are what matter. All functions come from stdlib/stats.bas.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' ---- Describe a variable ----
    ages = [22, 25, 29, 31, 24, 27, 40, 35, 28, 26]
    print("describe mean " + string(round(mean(ages), 3)) + " sd " + string(round(stdev(ages), 3)) + " median " + string(median(ages)))

    ' ---- Two conditions: independent t-test + effect size ----
    ctrl = [4.1, 3.8, 4.4, 3.9, 4.0, 4.2, 3.7, 4.3]
    treat = [4.6, 4.9, 4.4, 5.1, 4.8, 4.7, 5.0, 4.5]
    tt = t_test_2sample(ctrl, treat)
    print("ttest t " + string(round(tt.statistic, 3)) + " p " + string(round(tt.p_value, 4)) + " d " + string(round(cohens_d(ctrl, treat), 3)))

    ' ---- 3+ groups: one-way ANOVA, post-hoc, effect size ----
    g = [ [5, 6, 4, 5], [7, 8, 6, 7], [9, 10, 8, 9] ]
    av = anova_oneway(g)
    es = eta_squared(g)
    print("anova F " + string(round(av.statistic, 3)) + " p " + string(round(av.p_value, 5)) + " eta2 " + string(round(es.eta_squared, 3)))

    ' ---- Association between two variables ----
    xcorr = [1, 2, 3, 4, 5, 6, 7, 8]
    ycorr = [2, 1, 4, 3, 6, 5, 8, 9]
    sp = spearman(xcorr, ycorr)
    print("spearman rho " + string(round(sp.rho, 3)) + " p " + string(round(sp.p_value, 4)))

    ' ---- Continuous outcome with a factor + interaction (OLS) ----
    y = [2.1, 3.4, 2.8, 4.1, 3.0, 5.2, 3.3, 4.8, 2.5, 3.9, 4.4, 5.5]
    x = [1.0, 2.0, 1.5, 3.0, 2.2, 3.5, 1.8, 3.1, 1.2, 2.7, 2.9, 3.8]
    grp = ["A", "A", "A", "A", "B", "B", "B", "B", "C", "C", "C", "C"]
    d = dummy_code(grp)
    fit = ols(y, [x, d.columns[0], d.columns[1]])
    print("ols b0 " + string(round(fit.coefficients[0], 3)) + " bx " + string(round(fit.coefficients[1], 3)) + " r2 " + string(round(fit.r_squared, 3)))
    rob = ols_robust(y, [x, d.columns[0], d.columns[1]], "HC3")
    print("ols HC3 se_x " + string(round(rob.std_errors[1], 4)))

    ' ---- Binary outcome: logistic with odds ratios + marginal effects ----
    yb = [0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1]
    lg = logistic_regression(yb, [x])
    orr = odds_ratios(lg, 0.95)
    me = marginal_effects(lg, [x])
    print("logit OR_x " + string(round(orr[1].odds_ratio, 3)) + " AME_x " + string(round(me.effects[0], 4)) + " prsq " + string(round(lg.pseudo_r2, 3)))

    ' ---- Likert-scale outcome: ordinal logistic ----
    likert = [0, 1, 0, 1, 2, 2, 1, 1, 0, 2, 1, 2]
    orl = ordinal_regression(likert, [x])
    print("ordinal b " + string(round(orl.coefficients[0], 3)) + " prsq " + string(round(orl.pseudo_r2, 3)))

    ' ---- Scale reliability + content-analysis intercoder agreement ----
    items = [ [4, 4, 3], [5, 5, 4], [2, 1, 2], [3, 3, 4], [5, 4, 5], [1, 2, 1] ]
    print("cronbach " + string(round(cronbach_alpha(items).alpha, 3)))
    coded = [ [1, 2, 3, 3, 2], [1, 2, 3, 2, 2], [1, 2, 3, 3, 1] ]
    print("krippendorff " + string(round(krippendorff_alpha(coded, "nominal").alpha, 3)))

    ' ---- Proportions / A-B campaign test ----
    ab = ab_test({ successes: 90, n: 1000 }, { successes: 120, n: 1000 })
    print("abtest lift " + string(round(ab.lift, 3)) + " p " + string(round(ab.p_value, 4)) + " sig " + string(ab.significant))

    ' ---- Mediation (X -> M -> Y) ----
    mx = [1.0, 2.0, 3.0, 1.5, 2.5, 3.5, 1.2, 2.8, 3.2, 1.8, 2.2, 3.8]
    mm = [2.0, 2.6, 3.9, 2.2, 3.1, 4.0, 1.9, 3.6, 3.8, 2.4, 2.9, 4.4]
    my = [2.4, 3.1, 4.2, 2.6, 3.6, 4.5, 2.2, 4.0, 4.3, 2.8, 3.3, 4.9]
    seed(7)
    med = mediation(my, mx, mm, 1000)
    print("mediation indirect " + string(round(med.indirect, 3)) + " direct " + string(round(med.c_direct, 3)))

    ' ---- Moderation: simple slopes of x at mean(w) +/- 1 SD ----
    w = [0.5, 1.0, 1.5, 0.8, 1.2, 1.8, 0.6, 1.4, 1.6, 0.9, 1.1, 1.9]
    ss = simple_slopes(y, x, w, [mean(w) - stdev(w), mean(w), mean(w) + stdev(w)])
    print("moderation intr_p " + string(round(ss.interaction_p, 4)) + " slope_hi " + string(round(ss.slopes[2].slope, 3)))

    ' ---- Design: power and required sample size ----
    print("power2 " + string(round(power_ttest(0.5, 30, 0.05, 2), 3)) + " n_needed " + string(sample_size_ttest(0.5, 0.8, 0.05, 2)))
end program
