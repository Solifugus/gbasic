' stats.bas — exact ARMA maximum-likelihood estimation via the Kalman filter
' (the follow-on that upgrades the best-effort CSS fit). arma_fit matches
' statsmodels ARIMA(order=(p,0,q), trend='c') to ~4 decimals on the parameters
' and to display precision on sigma2 / log-likelihood / AIC / BIC. The series is
' a fixed 100-point ARMA(1,1) sample (gBASIC has no RNG-with-noise or sin, so it
' is hardcoded); references computed with statsmodels on this exact series.
' Parameters are shown to 3 decimals: the likelihood surface is flat near the
' optimum, so the 4th decimal is optimizer-tolerance noise (the log-likelihood
' agrees to display precision).
program demo(args)
    load stats from "../stdlib/stats.bas"

    y = [2.6809, 2.9289, 5.1612, 5.9279, 4.6276, 3.6541, 4.5325, 4.8091, 4.0524, 3.8939, 4.9894, 7.2678, 5.907, 4.4201, 3.5977, 1.4181, 0.9594, 1.1823, 3.4238, 4.3719, 2.9471, 3.7656, 4.1618, 2.6114, 2.2729, 2.5365, 2.4126, 3.1277, 1.6403, 1.8171, 3.9963, 5.6559, 6.8428, 7.6684, 8.5363, 6.5985, 6.3576, 4.292, 3.228, 1.7812, 3.3511, 5.7636, 5.7943, 7.1722, 6.3385, 5.6838, 5.6235, 4.3711, 4.6349, 6.8951, 5.8877, 6.022, 5.8184, 7.1989, 7.511, 7.3763, 7.3394, 6.91, 4.8499, 5.7433, 4.632, 4.4695, 5.3912, 4.6424, 4.5954, 5.9831, 6.0329, 4.7215, 1.5617, 0.7989, 1.4995, 2.1469, 2.1215, 3.2662, 3.5359, 3.7822, 4.9046, 4.359, 3.973, 6.4219, 7.5907, 5.6106, 4.7692, 5.3941, 6.6957, 6.6598, 5.5249, 4.3722, 4.9762, 5.5568, 4.9426, 4.5699, 4.2501, 5.1082, 4.6648, 5.0093, 6.0517, 6.4579, 5.235, 6.1523]

    ' Exact ARMA(1,1) MLE. statsmodels: const 4.712, phi 0.626, theta 0.421,
    ' sigma2 1.0598, llf -145.378, aic 298.755, bic 309.176.
    m = arma_fit(y, 1, 1)
    print("arma11 const " + string(round(m.const, 3)) + " phi " + string(round(m.phi[0], 3)) + " theta " + string(round(m.theta[0], 3)))
    print("arma11 sigma2 " + string(round(m.sigma2, 4)) + " llf " + string(round(m.llf, 3)))
    print("arma11 aic " + string(round(m.aic, 3)) + " bic " + string(round(m.bic, 3)))

    ' arima_fit routes q>0 through the exact MLE (here d=0, so identical).
    ai = arima_fit(y, 1, 0, 1)
    print("arima101 const " + string(round(ai.const, 3)) + " phi " + string(round(ai.phi[0], 3)) + " theta " + string(round(ai.theta[0], 3)))

    ' domain guard returns unknown
    print("guard " + string(arma_fit([1, 2, 3], 1, 1)))
end program
