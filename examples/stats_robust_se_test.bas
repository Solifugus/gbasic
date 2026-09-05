' stats.bas — OLS with heteroskedasticity-consistent (robust) standard errors.
' HC0/HC1/HC2/HC3 sandwich estimators; z/p use the normal distribution, matching
' statsmodels OLS.fit(cov_type='HC*'). Fixed 50-row heteroskedastic dataset.
program demo(args)
    load stats from "../stdlib/stats.bas"

    x1 = [-0.052, -0.111, 1.042, -1.257, 0.745, -1.711, -0.206, -0.235, 1.128, -0.013, -0.613, 1.374, 1.611, -0.689, 0.692, -0.448, 0.162, 0.257, -1.275, 0.064, -1.062, -0.989, -0.458, -1.984, -1.476, 0.232, 0.644, 0.852, -0.464, 0.697, 1.568, 1.179, -1.384, -1.747, 0.403, 1.244, -0.024, 0.953, 0.245, 0.224, 0.297, 0.221, -0.423, 1.846, 0.92, -0.558, -0.285, -1.041, 0.48, -1.427]
    x2 = [2.659, 0.983, 2.414, 2.392, 1.997, 0.732, 2.458, 1.403, 2.217, 1.382, 2.592, 2.106, 0.504, 0.538, 2.55, 1.095, 1.143, 0.124, 1.902, 2.413, 2.692, 0.584, 2.572, 0.53, 1.121, 1.147, 1.712, 1.533, 1.587, 1.57, 2.796, 0.74, 0.625, 2.448, 0.912, 2.026, 2.771, 0.21, 2.345, 0.105, 1.527, 1.316, 2.072, 2.338, 0.142, 0.224, 1.815, 2.137, 0.452, 1.844]
    y = [-0.44, -0.25, 0.212, 1.185, 0.797, -0.16, -0.827, 1.779, -1.54, -0.172, 0.034, 0.052, 0.872, 0.644, 0.964, 0.619, 0.047, 1.636, -2.501, -0.649, -2.329, 0.275, -0.925, -2.461, -0.698, -0.042, 0.212, 0.6, -0.607, -0.151, -0.942, 2.9, -3.17, -1.16, 0.269, 0.1, -1.322, 1.625, -0.335, 1.799, 0.09, -0.171, -1.28, 2.846, 2.262, 0.15, 0.978, -0.602, 0.926, -0.361]

    ' Coefficients are identical across cov types; only the SEs change.
    ' statsmodels HC SEs (const, x1, x2):
    '   HC0 0.278  0.18839 0.1643
    '   HC1 0.28673 0.19431 0.16947
    '   HC2 0.29101 0.19779 0.17194
    '   HC3 0.30475 0.20773 0.17999
    h0 = stats.ols_robust(y, [x1, x2], "HC0")
    print("HC0 se " + string(round(h0.std_errors[0], 5)) + " " + string(round(h0.std_errors[1], 5)) + " " + string(round(h0.std_errors[2], 5)))
    h1 = stats.ols_robust(y, [x1, x2], "HC1")
    print("HC1 se " + string(round(h1.std_errors[0], 5)) + " " + string(round(h1.std_errors[1], 5)) + " " + string(round(h1.std_errors[2], 5)))
    h2 = stats.ols_robust(y, [x1, x2], "HC2")
    print("HC2 se " + string(round(h2.std_errors[0], 5)) + " " + string(round(h2.std_errors[1], 5)) + " " + string(round(h2.std_errors[2], 5)))
    h3 = stats.ols_robust(y, [x1, x2], "HC3")
    print("HC3 se " + string(round(h3.std_errors[0], 5)) + " " + string(round(h3.std_errors[1], 5)) + " " + string(round(h3.std_errors[2], 5)))
    print("coef " + string(round(h0.coefficients[0], 5)) + " " + string(round(h0.coefficients[1], 5)) + " " + string(round(h0.coefficients[2], 5)))
    print("HC3 p " + string(round(h3.p_values[1], 5)) + " " + string(round(h3.p_values[2], 5)))

    ' domain guard returns unknown
    print("guard " + string(stats.ols_robust([1, 2], [[1, 2]], "HC1")))
end program
