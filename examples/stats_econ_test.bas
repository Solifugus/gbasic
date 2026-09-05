' Econometric diagnostics & finance metrics from stdlib/stats.bas.
' Every number below was verified to match statsmodels / scipy exactly
' (see docs/cookbook_econometrics_finance.md); rounded here for a stable
' golden file.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' ---- Unit-root testing (Augmented Dickey-Fuller) ----
    x = [10.0, 10.5, 10.2, 10.8, 11.0, 10.6, 11.3, 11.1, 11.6, 11.4, 12.0, 11.8, 12.3, 12.1, 12.6, 12.4, 13.0, 12.7, 13.2, 13.5, 13.1, 13.8, 13.6, 14.1, 14.4]
    ad = stats.adf_test(x, 1, "c")
    print("adf_c stat " + string(round(ad.statistic, 4)) + " p " + string(round(ad.p_value, 4)) + " crit5 " + string(round(ad.crit_5, 3)))
    adct = stats.adf_test(x, 1, "ct")
    print("adf_ct stat " + string(round(adct.statistic, 4)) + " p " + string(round(adct.p_value, 5)))

    ' ---- Residual diagnostics ----
    e = [0.5, -0.3, 0.2, -0.6, 0.4, -0.1, 0.3, -0.5, 0.6, -0.2, 0.1, -0.4, 0.5, -0.3, 0.25, -0.15, 0.35, -0.45, 0.55, -0.25]
    lb = stats.ljung_box(e, 4)
    print("ljung_box Q " + string(round(lb.statistic, 3)) + " p " + string(round(lb.p_value, 6)))
    print("durbin_watson " + string(round(stats.durbin_watson(e), 4)))
    al = stats.arch_lm(e, 2)
    print("arch_lm stat " + string(round(al.statistic, 4)) + " p " + string(round(al.p_value, 4)))

    ' ---- Heteroskedasticity + HAC standard errors ----
    yy = [2.1, 3.4, 2.8, 4.1, 3.0, 5.2, 3.3, 4.8, 2.5, 3.9, 4.4, 5.5, 4.0, 5.8, 4.6, 6.1]
    xx = [1.0, 2.0, 1.5, 3.0, 2.2, 3.5, 1.8, 3.1, 1.2, 2.7, 2.9, 3.8, 2.4, 4.0, 3.2, 4.3]
    reg = stats.ols(yy, [xx])
    bp = stats.breusch_pagan(reg.residuals, [xx])
    print("breusch_pagan stat " + string(round(bp.statistic, 4)) + " p " + string(round(bp.p_value, 4)))
    nw = stats.newey_west(yy, [xx], 2)
    print("newey_west b1 " + string(round(nw.coefficients[1], 4)) + " se1 " + string(round(nw.std_errors[1], 4)))

    ' ---- Finance return metrics ----
    prices = [100.0, 102.0, 101.0, 105.0, 103.0, 108.0, 107.0, 110.0, 106.0, 112.0, 111.0, 115.0]
    sr = stats.simple_returns(prices)
    print("cumret " + string(round(stats.cumulative_return(sr), 4)))
    print("sharpe " + string(round(stats.sharpe_ratio(sr, 0.0, 252), 3)))
    print("sortino " + string(round(stats.sortino_ratio(sr, 0.0, 252), 3)))
    md = stats.max_drawdown(prices)
    print("maxdd " + string(round(md.max_drawdown, 4)) + " trough " + string(md.trough))
    print("var_hist " + string(round(stats.value_at_risk(sr, 0.05, "historical"), 4)))
    print("var_norm " + string(round(stats.value_at_risk(sr, 0.05, "normal"), 4)))
    print("cvar " + string(round(stats.cvar(sr, 0.25), 4)))

    ' ---- CAPM market model ----
    asset = [0.02, -0.01, 0.03, 0.01, -0.02, 0.04, 0.00, 0.02, -0.03, 0.05, 0.01, 0.02]
    mkt = [0.01, -0.005, 0.02, 0.008, -0.01, 0.025, 0.002, 0.012, -0.02, 0.03, 0.005, 0.015]
    cp = stats.capm(asset, mkt, 0.001)
    print("capm alpha " + string(round(cp.alpha, 5)) + " beta " + string(round(cp.beta, 4)) + " r2 " + string(round(cp.r_squared, 4)))
end program
