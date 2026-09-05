' stats.bas — ARIMA-family time-series modeling (unblocked by the Phase 10
' optimizer). AR(p) fit/forecast and differencing are exact conditional MLE and
' match statsmodels AutoReg to machine precision; ARMA CSS uses the optimizer.
' gBASIC prints ~6 sig figs, so results round to 6 decimals. The series is a
' fixed 40-point sample (gBASIC has no sin builtin, so it is hardcoded); all
' reference values are computed on this exact series.
program demo(args)
    load stats from "../stdlib/stats.bas"

    y = [10.0, 9.4207, 9.1071, 8.6507, 7.9906, 7.5848, 7.813, 8.4994, 9.0317, 8.9252, 8.2768, 7.681, 7.685, 8.2849, 8.9292, 9.0257, 8.4856, 7.8055, 7.6107, 8.0803, 8.7825, 9.0718, 8.6821, 7.9718, 7.5939, 7.8958, 8.6, 9.059, 8.8509, 8.1669, 7.636, 7.7462, 8.3962, 8.9885, 8.9784, 8.3752, 7.7336, 7.6433, 8.1875, 8.8657]

    ' AR(1) — matches statsmodels AutoReg(lags=1, trend='c').
    a1 = stats.ar_fit(y, 1)
    print("ar1 const " + string(round(a1.const, 6)) + " phi " + string(round(a1.phi[0], 6)) + " sigma2 " + string(round(a1.sigma2, 6)) + " n " + string(a1.n))
    print("ar1 aic " + string(round(a1.aic, 6)) + " bic " + string(round(a1.bic, 6)))

    ' AR(2) — matches statsmodels; note the second phi is negative.
    a2 = stats.ar_fit(y, 2)
    print("ar2 const " + string(round(a2.const, 6)) + " phi " + string(round(a2.phi[0], 6)) + " " + string(round(a2.phi[1], 6)))
    print("ar2 aic " + string(round(a2.aic, 6)) + " bic " + string(round(a2.bic, 6)))

    ' AR(2) forecast (recursive) — matches AutoReg.forecast.
    fc = stats.ar_forecast(a2, y, 4)
    print("ar2 fc " + string(round(fc[0], 6)) + " " + string(round(fc[1], 6)) + " " + string(round(fc[2], 6)) + " " + string(round(fc[3], 6)))

    ' ARIMA(1,1,0): difference once, then AR(1) — exact.
    ai = stats.arima_fit(y, 1, 1, 0)
    print("arima110 const " + string(round(ai.const, 6)) + " phi " + string(round(ai.phi[0], 6)))
    aifc = stats.arima_forecast(ai, y, 3)
    print("arima110 fc " + string(round(aifc[0], 6)) + " " + string(round(aifc[1], 6)) + " " + string(round(aifc[2], 6)))

    ' ARMA(1,1) by CSS via the optimizer (best-effort; not exact-MLE). This
    ' short AR-like series does not identify the MA term (the SSE surface is a
    ' flat, near-non-invertible ridge at sse ~= 1.22), so the estimate is only
    ' weakly determined; the value below is gBASIC's deterministic optimum.
    cs = stats.arma_css_fit(y, 1, 1)
    print("css const " + string(round(cs.const, 6)) + " phi " + string(round(cs.phi[0], 6)) + " theta " + string(round(cs.theta[0], 6)) + " sse " + string(round(cs.sse, 6)))

    ' domain guards return unknown
    print("guard_ar " + string(stats.ar_fit([1, 2], 3)))
    print("guard_fit " + string(stats.arima_fit([1, 2, 3], 5, 0, 0)))
end program
