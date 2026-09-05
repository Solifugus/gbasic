' stats.bas Phase 5 — correlation family & effect sizes
' (docs/statistics_scientist_plan.md). Spearman / Kendall / partial / point-
' biserial correlation, Cramér's V, Hedges' g, odds ratio, eta/omega squared.
' Values verified against scipy.stats; gBASIC prints ~6 sig figs, so results
' round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"
    x = [10, 8, 13, 9, 11, 14, 6, 4, 12, 7, 5]
    y = [8.04, 6.95, 7.58, 8.81, 8.33, 9.96, 7.24, 4.26, 10.84, 4.82, 5.68]
    z = [2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 11]

    sp = stats.spearman(x, y)
    print("spearman rho " + string(round(sp.rho, 6)) + " p " + string(round(sp.p_value, 6)))

    kt = stats.kendall_tau(x, y)
    print("kendall tau " + string(round(kt.tau, 6)) + " p " + string(round(kt.p_value, 6)))

    pc = stats.partial_correlation(x, y, z)
    print("partial r " + string(round(pc.r, 6)) + " p " + string(round(pc.p_value, 6)))

    b = [0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1]
    pb = stats.point_biserial(b, y)
    print("pbis r " + string(round(pb.r, 6)) + " p " + string(round(pb.p_value, 6)))

    cv = stats.cramers_v([[10, 20, 30], [6, 9, 17]])
    print("cramers v " + string(round(cv.v, 6)) + " chi2 " + string(round(cv.chi2, 6)) + " dof " + string(cv.dof))

    hg = stats.hedges_g([20, 22, 19, 24, 25], [18, 17, 15, 16, 19])
    print("hedges d " + string(round(hg.d, 6)) + " g " + string(round(hg.g, 6)))

    orr = stats.odds_ratio([[20, 30], [15, 35]])
    print("odds or " + string(round(orr.odds_ratio, 6)) + " ci " + string(round(orr.ci_low, 6)) + " " + string(round(orr.ci_high, 6)))

    es = stats.eta_squared([[1, 2, 3, 4], [3, 4, 5, 6], [5, 6, 7, 9]])
    print("eta " + string(round(es.eta_squared, 6)) + " omega " + string(round(es.omega_squared, 6)))

    ' domain guards return unknown
    print("guard_len " + string(stats.spearman([1, 2, 3], [1, 2])))
    print("guard_small " + string(stats.partial_correlation([1, 2, 3], [1, 2, 3], [1, 2, 3])))
    print("guard_or " + string(stats.odds_ratio([[1, 0], [3, 4]])))
end program
