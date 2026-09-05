' stats.bas Phase 4 — time series (statistics_design.md §8 Phase 4).
' Moving averages (sma, ewma), differencing, the autocorrelation /
' partial-autocorrelation functions (acf, pacf), and the exponential-smoothing
' family (ses, holt, holt_winters). Values checked against pandas / statsmodels;
' gBASIC prints ~6 sig figs (the cross-architecture determinism rule), so
' results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    a = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9]

    ' --- simple moving average (window 3) ---
    sm = stats.sma(a, 3)
    print("sma3 " + string(sm[1]) + " " + string(round(sm[2], 6)) + " " + string(round(sm[4], 6)) + " " + string(round(sm[14], 6)))

    ' --- ewma (alpha 0.3, recursive) ---
    ew = stats.ewma(a, 0.3)
    print("ewma " + string(round(ew[0], 6)) + " " + string(round(ew[1], 6)) + " " + string(round(ew[4], 6)) + " " + string(round(ew[14], 6)))

    ' --- differencing ---
    d1 = stats.diff(a, 1)
    print("diff1 len " + string(len(d1)) + " " + string(round(d1[0], 6)) + " " + string(round(d1[1], 6)) + " " + string(round(d1[13], 6)))

    ' --- acf / pacf (lags 0..5) ---
    ac = stats.acf(a, 5)
    print("acf " + string(round(ac[0], 6)) + " " + string(round(ac[1], 6)) + " " + string(round(ac[3], 6)) + " " + string(round(ac[4], 6)))
    pc = stats.pacf(a, 5)
    print("pacf " + string(round(pc[0], 6)) + " " + string(round(pc[1], 6)) + " " + string(round(pc[4], 6)) + " " + string(round(pc[5], 6)))

    ' --- simple exponential smoothing (alpha 0.3, h 2) ---
    se = stats.ses(a, 0.3, 2)
    print("ses level " + string(round(se.level[14], 6)) + " fit12 " + string(round(se.fitted[1], 6)) + " " + string(round(se.fitted[2], 6)))
    print("ses sse " + string(round(se.sse, 6)) + " fc " + string(round(se.forecast[0], 6)) + " " + string(round(se.forecast[1], 6)))

    ' --- Holt's linear trend (alpha 0.5, beta 0.3, h 3) ---
    hs = [10, 12, 14, 16, 18, 20]
    ho = stats.holt(hs, 0.5, 0.3, 3)
    print("holt level " + string(round(ho.level[5], 6)) + " trend " + string(round(ho.trend[5], 6)))
    print("holt fc " + string(round(ho.forecast[0], 6)) + " " + string(round(ho.forecast[1], 6)) + " " + string(round(ho.forecast[2], 6)))

    ' --- additive Holt-Winters (period 4, alpha 0.5, beta 0.3, gamma 0.2, h 4) ---
    ws = [10, 20, 30, 40, 12, 22, 32, 42, 14, 24, 34, 44, 16, 26, 36, 46]
    hw = stats.holt_winters(ws, 0.5, 0.3, 0.2, 4, 4)
    print("hw level " + string(round(hw.level, 6)) + " trend " + string(round(hw.trend, 6)))
    print("hw fc " + string(round(hw.forecast[0], 6)) + " " + string(round(hw.forecast[1], 6)) + " " + string(round(hw.forecast[2], 6)) + " " + string(round(hw.forecast[3], 6)))
    print("hw fit " + string(round(hw.fitted[4], 6)) + " " + string(round(hw.fitted[5], 6)))

    ' --- domain guards return unknown ---
    print("guard_sma " + string(stats.sma(a, 0)))
    print("guard_ewma " + string(stats.ewma(a, 1.5)))
    print("guard_acf " + string(stats.acf(a, 20)))
    print("guard_hw " + string(stats.holt_winters(ws, 0.5, 0.3, 0.2, 10, 4)))
end program
