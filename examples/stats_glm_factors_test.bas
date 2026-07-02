' stats.bas — categorical predictors for regression: treatment (dummy) coding
' and interaction terms. dummy_code drops the first (sorted) level as the
' reference, matching statsmodels/patsy C(x). Verified: an OLS on the dummy
' columns reproduces statsmodels 'y ~ x1 + C(g)', and adding interaction()
' columns reproduces 'y ~ x1 * C(g)'. Fixed 60-row dataset (3-level factor g).
program demo(args)
    load stats from "../stdlib/stats.bas"

    x1 = [0.001, -0.29, -1.116, -0.013, -0.378, -0.481, -1.517, -0.491, -0.241, -0.648, 0.636, 1.74, 0.297, 0.708, 1.823, 0.431, 1.543, -0.901, -0.137, 1.298, 0.675, 0.032, 0.918, 0.381, 0.516, -0.355, 0.209, 0.328, -0.498, -2.092, -0.083, 2.455, -2.672, -0.913, -0.227, 0.269, 1.13, 1.042, 1.304, 1.389, -0.656, -0.056, -0.5, 0.436, -0.376, -0.923, 1.917, -0.15, -0.639, 0.825, -1.211, -0.503, -0.702, -1.974, -2.656, -0.058, -0.656, -0.662, 0.769, -0.899]
    g = ["A", "C", "C", "B", "C", "C", "A", "B", "A", "B", "A", "B", "C", "B", "C", "C", "B", "C", "B", "C", "B", "A", "A", "C", "A", "B", "C", "A", "B", "B", "C", "C", "C", "A", "B", "A", "C", "A", "A", "B", "C", "A", "C", "A", "B", "C", "A", "B", "B", "B", "B", "A", "C", "B", "B", "A", "C", "C", "B", "A"]
    y = [1.716, 0.393, -0.786, 3.275, 0.252, -0.056, 0.083, 2.451, 1.938, 2.229, 2.665, 6.746, 0.822, 3.599, 3.091, 0.918, 5.026, -0.728, 2.084, 2.926, 3.126, 1.036, 2.826, 1.068, 3.505, 2.158, 1.402, 3.246, 2.781, -0.319, 0.605, 4.481, -3.399, 0.606, 2.88, 2.42, 1.477, 3.937, 3.98, 5.179, -0.489, 1.601, -0.521, 2.672, 2.131, -0.358, 4.594, 3.077, 2.267, 3.981, 1.223, 1.526, 0.396, -0.785, -1.361, 1.892, -0.822, -0.701, 4.158, 0.703]

    ' Treatment coding: reference = "A", dummies for "B" and "C".
    d = dummy_code(g)
    print("levels " + d.levels[0] + " " + d.levels[1] + " " + d.levels[2] + " ref " + d.reference)

    ' y ~ x1 + C(g). statsmodels: Intercept 2.0056, x1 1.5084,
    ' B 0.9308, C -1.3874, R2 0.9504.
    m = ols(y, [x1, d.columns[0], d.columns[1]])
    print("main b " + string(round(m.coefficients[0], 4)) + " x1 " + string(round(m.coefficients[1], 4)) + " B " + string(round(m.coefficients[2], 4)) + " C " + string(round(m.coefficients[3], 4)))
    print("main r2 " + string(round(m.r_squared, 4)))

    ' y ~ x1 * C(g): add the x1-by-factor interaction columns. statsmodels:
    ' Intercept 2.0218, x1 1.4178, B 0.932, C -1.4093, x1:B 0.1858, x1:C 0.0304,
    ' R2 0.9525.
    xB = interaction(x1, d.columns[0])
    xC = interaction(x1, d.columns[1])
    mi = ols(y, [x1, d.columns[0], d.columns[1], xB, xC])
    print("intr b " + string(round(mi.coefficients[0], 4)) + " x1 " + string(round(mi.coefficients[1], 4)) + " B " + string(round(mi.coefficients[2], 4)) + " C " + string(round(mi.coefficients[3], 4)))
    print("intr x1B " + string(round(mi.coefficients[4], 4)) + " x1C " + string(round(mi.coefficients[5], 4)) + " r2 " + string(round(mi.r_squared, 4)))

    ' domain guards return unknown
    print("guard_dummy " + string(dummy_code(["A", "A"])))
    print("guard_intr " + string(interaction([1, 2], [1])))
end program
