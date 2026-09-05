' stats.bas OLS regression (statistics_design.md §8 Phase 1). Coefficients,
' R^2, standard errors, t and p-values checked against numpy / scipy.stats.
' Numbers print to ~6 significant figures, so values are rounded for the golden.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' --- Simple regression: y ~ x ---
    x = [1, 2, 3, 4, 5, 6, 7, 8]
    y = [5.1, 7.9, 11.2, 13.8, 17.1, 19.9, 23.2, 25.8]
    r = stats.ols(y, x)
    print("b0 " + string(round(r.coefficients[0], 6)))
    print("b1 " + string(round(r.coefficients[1], 6)))
    print("r2 " + string(round(r.r_squared, 6)))
    print("adj " + string(round(r.adj_r_squared, 6)))
    print("se0 " + string(round(r.std_errors[0], 6)))
    print("se1 " + string(round(r.std_errors[1], 6)))
    print("t1 " + string(round(r.t_values[1], 4)))
    print("p1 " + string(round(r.p_values[1], 8)))
    print("n " + string(r.n))
    print("df " + string(r.df))
    print("resid0 " + string(round(r.residuals[0], 6)))
    print("fit0 " + string(round(r.fitted[0], 6)))

    ' --- Multiple regression: y ~ x1 + x2 ---
    x1 = [1, 2, 3, 4, 5, 6]
    x2 = [2, 1, 4, 3, 6, 5]
    y2 = [4.0, 5.1, 9.9, 11.2, 16.8, 17.9]
    m = stats.ols(y2, [x1, x2])
    print("m_b0 " + string(round(m.coefficients[0], 6)))
    print("m_b1 " + string(round(m.coefficients[1], 6)))
    print("m_b2 " + string(round(m.coefficients[2], 6)))
    print("m_r2 " + string(round(m.r_squared, 6)))
    print("m_df " + string(m.df))

    ' --- Edge cases return unknown ---
    print("empty " + string(stats.ols([], [])))
    print("under " + string(stats.ols([1, 2], [[1, 2]])))
    print("mismatch " + string(stats.ols([1, 2, 3], [[1, 2]])))
end program
