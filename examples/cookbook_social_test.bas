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
    tt = stats.t_test_2sample(ctrl, treat)
    print("ttest t " + string(round(tt.statistic, 3)) + " p " + string(round(tt.p_value, 4)) + " d " + string(round(stats.cohens_d(ctrl, treat), 3)))

    ' ---- 3+ groups: one-way ANOVA, post-hoc, effect size ----
    g = [ [5, 6, 4, 5], [7, 8, 6, 7], [9, 10, 8, 9] ]
    av = stats.anova_oneway(g)
    es = stats.eta_squared(g)
    print("anova F " + string(round(av.statistic, 3)) + " p " + string(round(av.p_value, 5)) + " eta2 " + string(round(es.eta_squared, 3)))

    ' ---- Association between two variables ----
    xcorr = [1, 2, 3, 4, 5, 6, 7, 8]
    ycorr = [2, 1, 4, 3, 6, 5, 8, 9]
    sp = stats.spearman(xcorr, ycorr)
    print("spearman rho " + string(round(sp.rho, 3)) + " p " + string(round(sp.p_value, 4)))

    ' ---- Continuous outcome with a factor + interaction (OLS) ----
    y = [2.1, 3.4, 2.8, 4.1, 3.0, 5.2, 3.3, 4.8, 2.5, 3.9, 4.4, 5.5]
    x = [1.0, 2.0, 1.5, 3.0, 2.2, 3.5, 1.8, 3.1, 1.2, 2.7, 2.9, 3.8]
    grp = ["A", "A", "A", "A", "B", "B", "B", "B", "C", "C", "C", "C"]
    d = stats.dummy_code(grp)
    fit = stats.ols(y, [x, d.columns[0], d.columns[1]])
    print("ols b0 " + string(round(fit.coefficients[0], 3)) + " bx " + string(round(fit.coefficients[1], 3)) + " r2 " + string(round(fit.r_squared, 3)))
    rob = stats.ols_robust(y, [x, d.columns[0], d.columns[1]], "HC3")
    print("ols HC3 se_x " + string(round(rob.std_errors[1], 4)))

    ' ---- Binary outcome: logistic with odds ratios + marginal effects ----
    yb = [0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1]
    lg = stats.logistic_regression(yb, [x])
    orr = stats.odds_ratios(lg, 0.95)
    me = stats.marginal_effects(lg, [x])
    print("logit OR_x " + string(round(orr[1].odds_ratio, 3)) + " AME_x " + string(round(me.effects[0], 4)) + " prsq " + string(round(lg.pseudo_r2, 3)))

    ' ---- Likert-scale outcome: ordinal logistic ----
    likert = [0, 1, 0, 1, 2, 2, 1, 1, 0, 2, 1, 2]
    orl = stats.ordinal_regression(likert, [x])
    print("ordinal b " + string(round(orl.coefficients[0], 3)) + " prsq " + string(round(orl.pseudo_r2, 3)))

    ' ---- Scale reliability + content-analysis intercoder agreement ----
    items = [ [4, 4, 3], [5, 5, 4], [2, 1, 2], [3, 3, 4], [5, 4, 5], [1, 2, 1] ]
    print("cronbach " + string(round(stats.cronbach_alpha(items).alpha, 3)))
    coded = [ [1, 2, 3, 3, 2], [1, 2, 3, 2, 2], [1, 2, 3, 3, 1] ]
    print("krippendorff " + string(round(stats.krippendorff_alpha(coded, "nominal").alpha, 3)))

    ' ---- Proportions / A-B campaign test ----
    ab = stats.ab_test({ successes: 90, n: 1000 }, { successes: 120, n: 1000 })
    print("abtest lift " + string(round(ab.lift, 3)) + " p " + string(round(ab.p_value, 4)) + " sig " + string(ab.significant))

    ' ---- Mediation (X -> M -> Y) ----
    mx = [1.0, 2.0, 3.0, 1.5, 2.5, 3.5, 1.2, 2.8, 3.2, 1.8, 2.2, 3.8]
    mm = [2.0, 2.6, 3.9, 2.2, 3.1, 4.0, 1.9, 3.6, 3.8, 2.4, 2.9, 4.4]
    my = [2.4, 3.1, 4.2, 2.6, 3.6, 4.5, 2.2, 4.0, 4.3, 2.8, 3.3, 4.9]
    seed(7)
    med = stats.mediation(my, mx, mm, 1000)
    print("mediation indirect " + string(round(med.indirect, 3)) + " direct " + string(round(med.c_direct, 3)))

    ' ---- Moderation: simple slopes of x at mean(w) +/- 1 SD ----
    w = [0.5, 1.0, 1.5, 0.8, 1.2, 1.8, 0.6, 1.4, 1.6, 0.9, 1.1, 1.9]
    ss = stats.simple_slopes(y, x, w, [mean(w) - stdev(w), mean(w), mean(w) + stdev(w)])
    print("moderation intr_p " + string(round(ss.interaction_p, 4)) + " slope_hi " + string(round(ss.slopes[2].slope, 3)))

    ' ---- Design: power and required sample size ----
    print("power2 " + string(round(stats.power_ttest(0.5, 30, 0.05, 2), 3)) + " n_needed " + string(stats.sample_size_ttest(0.5, 0.8, 0.05, 2)))

    ' ---- Survival: time to event, with censoring carried explicitly ----
    ' The Freireich 1963 leukaemia trial. The `+` cases in the paper are
    ' censored -- still in remission when observation stopped -- and the event
    ' indicator is what keeps them from being read as relapses.
    tt = [6, 6, 6, 6, 7, 9, 10, 10, 11, 13, 16, 17, 19, 20, 22, 23, 25, 32, 32, 34, 35]
    ee = [1, 1, 1, 0, 1, 0,  1,  0,  0,  1,  1,  0,  0,  0,  1,  1,  0,  0,  0,  0,  0]
    ct = [1, 1, 2, 2, 3, 4, 4, 5, 5, 8, 8, 8, 8, 11, 11, 12, 12, 15, 17, 22, 23]
    ce = [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1,  1,  1]
    km = stats.kaplan_meier(tt, ee)
    print("survival median " + string(km.median) + " S(10) " + string(round(stats.survival_at(km, 10), 4)))
    lr2 = stats.logrank(tt, ee, ct, ce)
    print("logrank chi2 " + string(round(lr2.chi_squared, 2)) + " p " + string(round(lr2.p, 6)))
    grp = []
    for each v in tt
        append(grp, 1)
    next v
    for each v in ct
        append(grp, 0)
    next v
    alltimes = []
    allev = []
    for each v in tt
        append(alltimes, v)
    next v
    for each v in ct
        append(alltimes, v)
    next v
    for each v in ee
        append(allev, v)
    next v
    for each v in ce
        append(allev, v)
    next v
    cx = stats.cox_ph(alltimes, allev, [grp])
    print("cox hr " + string(round(cx.hazard_ratios[0], 3)) + " p " + string(round(cx.p_values[0], 5)))

    ' ---- Meta-analysis: pool studies, and report the heterogeneity beside it ----
    studies = [{ effect: 0.30, variance: 0.010 }, { effect: 0.45, variance: 0.015 },
               { effect: 0.20, variance: 0.008 }, { effect: 0.55, variance: 0.020 },
               { effect: 0.35, variance: 0.012 }]
    ma = stats.meta_analysis(studies, { model: "random" })
    print("meta pooled " + string(round(ma.estimate, 4)) + " I2 " + string(round(ma.i_squared, 1)) + " Q p " + string(round(ma.q_p, 4)))

    ' ---- Factor analysis: latent structure, not a summary of the variables ----
    ' Six items, the first three driven by one latent factor and the last three
    ' by another; a correct fit recovers exactly that block pattern.
    c1 = []
    c2 = []
    c3 = []
    c4 = []
    c5 = []
    c6 = []
    i = 0
    while i < 120
        fa1 = mod(i * 7, 11) - 5
        fa2 = mod(i * 3, 13) - 6
        append(c1, fa1 + 0.3 * (mod(i, 5) - 2))
        append(c2, 0.9 * fa1 + 0.3 * (mod(i * 2, 5) - 2))
        append(c3, 1.1 * fa1 + 0.3 * (mod(i * 4, 5) - 2))
        append(c4, fa2 + 0.3 * (mod(i * 3, 5) - 2))
        append(c5, 0.8 * fa2 + 0.3 * (mod(i * 5, 5) - 2))
        append(c6, 1.2 * fa2 + 0.3 * (mod(i * 6, 5) - 2))
        i += 1
    end while
    fa = stats.factor_analysis([c1, c2, c3, c4, c5, c6], { factors: 2, rotate: "varimax" })
    ' Which factor each item leads on. Rotation does not promise an ORDER, so
    ' the property to check is the BLOCK PATTERN -- items 1-3 together, items
    ' 4-6 together, and the two groups apart -- never "item 1 is on factor 1".
    lead = []
    for each row in fa.loadings
        pick = 0
        if abs(row[1]) > abs(row[0]) then
            pick = 1
        end if
        append(lead, pick)
    next row
    block = lead[0] = lead[1] and lead[1] = lead[2] and lead[3] = lead[4] and lead[4] = lead[5] and lead[0] != lead[3]
    print("factors ok " + string(fa.ok) + " two clean blocks " + string(block) + " variance explained " + string(round(fa.variance_explained[0] + fa.variance_explained[1], 3)))
end program
