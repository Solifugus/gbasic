' stats.bas Phase 10 (keystone) — the optimizer
' (docs/statistics_scientist_plan.md §10). Derivative-free Nelder-Mead simplex
' and nonlinear curve fitting, both pure gBASIC over first-class function
' values. Verified against scipy.optimize (Rosenbrock minimum at (1,1); the
' exponential-decay and logistic fits recover their true parameters). gBASIC
' prints ~6 sig figs, so results round to 6 decimals.

' Rosenbrock function (classic optimizer test); ctx is unused.
function rosenbrock(p, ctx)
    a = 1 - p[0]
    b = p[1] - p[0] * p[0]
    return a * a + 100 * b * b
end function

' Exponential decay model: y = a*exp(-b*x) + c.
function expmodel(x, p)
    return p[0] * exp(0 - p[1] * x) + p[2]
end function

' Logistic growth model: y = L / (1 + exp(-k*(x - x0))).
function logmodel(x, p)
    return p[0] / (1 + exp(0 - p[1] * (x - p[2])))
end function

program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Minimize Rosenbrock; global minimum f=0 at (1, 1).
    r = stats.optimize(rosenbrock, [-1.2, 1.0], { max_iter: 2000 }, unknown)
    print("rosen p " + string(round(r.params[0], 4)) + " " + string(round(r.params[1], 4)) + " conv " + string(r.converged))

    ' Fit exponential decay to noiseless data from a=2.5, b=0.4, c=0.5.
    xs = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
    ys = []
    i = 0
    while i < len(xs)
        append(ys, 2.5 * exp(0 - 0.4 * xs[i]) + 0.5)
        i = i + 1
    end while
    ef = stats.curve_fit(expmodel, xs, ys, [1.0, 1.0, 1.0])
    print("expfit p " + string(round(ef.params[0], 4)) + " " + string(round(ef.params[1], 4)) + " " + string(round(ef.params[2], 4)) + " r2 " + string(round(ef.r_squared, 6)))

    ' Fit logistic growth to noiseless data from L=10, k=1, x0=5.
    xs2 = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    ys2 = []
    i = 0
    while i < len(xs2)
        append(ys2, 10 / (1 + exp(0 - 1 * (xs2[i] - 5))))
        i = i + 1
    end while
    lf = stats.curve_fit(logmodel, xs2, ys2, [8.0, 0.5, 4.0])
    print("logfit p " + string(round(lf.params[0], 4)) + " " + string(round(lf.params[1], 4)) + " " + string(round(lf.params[2], 4)) + " r2 " + string(round(lf.r_squared, 6)))

    ' domain guards return unknown
    print("guard_opt " + string(stats.optimize(rosenbrock, [], unknown, unknown)))
    print("guard_fit " + string(stats.curve_fit(expmodel, [1, 2], [1], [1, 1, 1])))
end program
