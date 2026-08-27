' Cookbook — Econometrics & Finance. Runnable companion to
' docs/cookbook_econometrics_finance.md: every recipe below executes, so the
' snippets in the cookbook are known-good. Small fixed series are used for
' speed; the idioms are what matter. All functions come from stdlib/stats.bas.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' ---- Returns from a price series ----
    prices = [100.0, 102.0, 101.0, 105.0, 103.0, 108.0, 107.0, 110.0, 106.0, 112.0, 111.0, 115.0]
    r = simple_returns(prices)
    lr = log_returns(prices)
    print("returns simple1 " + string(round(r[0], 4)) + " log1 " + string(round(lr[0], 4)) + " cum " + string(round(cumulative_return(r), 4)))

    ' ---- Performance & risk metrics ----
    print("perf sharpe " + string(round(sharpe_ratio(r, 0.0, 252), 3)) + " sortino " + string(round(sortino_ratio(r, 0.0, 252), 3)))
    md = max_drawdown(prices)
    print("risk maxdd " + string(round(md.max_drawdown, 4)) + " var95 " + string(round(value_at_risk(r, 0.05, "historical"), 4)) + " cvar " + string(round(cvar(r, 0.25), 4)))

    ' ---- CAPM market model (systematic risk) ----
    asset = [0.02, -0.01, 0.03, 0.01, -0.02, 0.04, 0.00, 0.02, -0.03, 0.05, 0.01, 0.02]
    mkt = [0.01, -0.005, 0.02, 0.008, -0.01, 0.025, 0.002, 0.012, -0.02, 0.03, 0.005, 0.015]
    cp = capm(asset, mkt, 0.001)
    print("capm alpha " + string(round(cp.alpha, 5)) + " beta " + string(round(cp.beta, 3)) + " r2 " + string(round(cp.r_squared, 3)))

    ' ---- Regression with serial-correlation-robust inference ----
    yy = [2.1, 3.4, 2.8, 4.1, 3.0, 5.2, 3.3, 4.8, 2.5, 3.9, 4.4, 5.5, 4.0, 5.8, 4.6, 6.1]
    xx = [1.0, 2.0, 1.5, 3.0, 2.2, 3.5, 1.8, 3.1, 1.2, 2.7, 2.9, 3.8, 2.4, 4.0, 3.2, 4.3]
    fit = ols(yy, [xx])
    nw = newey_west(yy, [xx], 2)
    print("ols b1 " + string(round(fit.coefficients[1], 4)) + " se " + string(round(fit.std_errors[1], 4)) + " | HAC se " + string(round(nw.std_errors[1], 4)))
    bp = breusch_pagan(fit.residuals, [xx])
    print("het breusch_pagan p " + string(round(bp.p_value, 4)))

    ' ---- Stationarity: unit-root test, then difference if needed ----
    lvl = [10.0, 10.5, 10.2, 10.8, 11.0, 10.6, 11.3, 11.1, 11.6, 11.4, 12.0, 11.8, 12.3, 12.1, 12.6, 12.4, 13.0, 12.7, 13.2, 13.5, 13.1, 13.8, 13.6, 14.1, 14.4]
    a1 = adf_test(lvl, 1, "c")
    dlvl = diff(lvl, 1)
    a2 = adf_test(dlvl, 1, "c")
    print("adf level p " + string(round(a1.p_value, 3)) + " -> diff p " + string(round(a2.p_value, 4)))

    ' ---- Autocorrelation structure (identify AR/MA order) ----
    ac = acf(dlvl, 3)
    pc = pacf(dlvl, 3)
    print("acf lag1 " + string(round(ac[1], 3)) + " pacf lag1 " + string(round(pc[1], 3)))

    ' ---- Fit ARIMA, forecast, check residuals ----
    m = arima_fit(lvl, 1, 1, 0)
    fc = arima_forecast(m, lvl, 3)
    print("arima phi1 " + string(round(m.phi[0], 3)) + " aic " + string(round(m.aic, 2)) + " fcast1 " + string(round(fc[0], 3)))

    ' ---- Volatility: test for ARCH effects, then fit GARCH ----
    ret = [0.012, -0.018, 0.025, -0.031, 0.028, -0.015, 0.009, -0.022, 0.033, -0.027, 0.019, -0.011, 0.024, -0.029, 0.016, -0.008, 0.021, -0.026, 0.030, -0.014, 0.013, -0.019, 0.027, -0.023]
    arch = arch_lm(ret, 2)
    g = garch_fit(ret)
    print("arch_lm p " + string(round(arch.p_value, 4)) + " garch persistence " + string(round(g.persistence, 3)))

    ' ---- Whiteness of residuals (Ljung-Box / Durbin-Watson) ----
    lb = ljung_box(fit.residuals, 4)
    print("resid ljung_box p " + string(round(lb.p_value, 3)) + " dw " + string(round(durbin_watson(fit.residuals), 3)))

    ' ---- Event study: did the announcement move the stock? ----
    ' Constructed so the answer is known: the asset is exactly 1.5x the market
    ' with a clean +4% shock on day 150, so a correct market model returns it.
    mkt_r = []
    ast_r = []
    i = 0
    while i < 200
        mm = 0.001 * (mod(i, 7) - 3)
        append(mkt_r, mm)
        append(ast_r, 1.5 * mm)
        i += 1
    end while
    ast_r[150] = ast_r[150] + 0.04
    ev = abnormal_returns(ast_r, mkt_r, { event: 150, pre: 1, post: 1, estimation: 120 })
    print("event beta " + string(round(ev.beta, 3)) + " car " + string(round(ev.car, 4)) + " window " + string(ev.window))

    ' ---- Difference-in-differences: the estimate IS a difference of differences ----
    dy = [10, 11, 12, 13, 20, 21, 26, 27]
    dt = [0, 0, 0, 0, 1, 1, 1, 1]
    dp = [0, 0, 1, 1, 0, 0, 1, 1]
    dd = did(dy, dt, dp, {})
    print("did att " + string(round(dd.att, 4)) + " cells " + string(round(dd.diff_in_means, 4)) + " saturated " + string(dd.saturated))

    ' ---- Instrumental variables: OLS is biased, 2SLS is not ----
    ' A confounder w moves both x and y, so OLS overstates the slope; z moves
    ' only x, so it identifies the true 1.5.
    zc = []
    xc = []
    yc = []
    i = 0
    while i < 60
        zi = mod(i, 2)
        w = 0.1 * (mod(i, 7) - 3)
        xi = 1 + 2 * zi + w + 0.05 * (mod(i, 5) - 2)
        append(zc, zi)
        append(xc, xi)
        append(yc, 3 + 1.5 * xi + 2 * w)
        i += 1
    end while
    iv = iv_2sls(yc, xc, zc, {})
    ob = ols(yc, xc)
    print("iv est " + string(round(iv.estimate, 4)) + " vs ols " + string(round(ob.coefficients[1], 4)) + " firstF " + string(round(iv.first_stage[0].f_stat, 1)) + " weak " + string(iv.weak))
    print("iv endogeneity p " + string(round(iv.wu_hausman.p_value, 4)) + " overid " + string(iv.overidentified))
end program
