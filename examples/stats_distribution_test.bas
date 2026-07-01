' stats.bas Phase 6 — distribution expansion
' (docs/statistics_scientist_plan.md §6). Uniform / exponential / gamma / beta /
' log-normal / Weibull / negative-binomial families, each pdf/cdf/quantile.
' Values verified against scipy.stats; gBASIC prints ~6 sig figs, so results
' round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    print("uniform pdf " + string(round(uniform_pdf(5, 2, 8), 6)) + " cdf " + string(round(uniform_cdf(5, 2, 8), 6)) + " q " + string(round(uniform_quantile(0.3, 2, 8), 6)))

    print("expon pdf " + string(round(expon_pdf(3, 0.5), 6)) + " cdf " + string(round(expon_cdf(3, 0.5), 6)) + " q " + string(round(expon_quantile(0.7, 0.5), 6)))

    print("gamma pdf " + string(round(gamma_pdf(2, 2.5, 1.5), 6)) + " cdf " + string(round(gamma_cdf(2, 2.5, 1.5), 6)) + " q " + string(round(gamma_quantile(0.9, 2.5, 1.5), 6)))

    print("beta pdf " + string(round(beta_pdf(0.3, 2, 5), 6)) + " cdf " + string(round(beta_cdf(0.3, 2, 5), 6)) + " q " + string(round(beta_quantile(0.8, 2, 5), 6)))

    print("lognormal pdf " + string(round(lognormal_pdf(2, 0.5, 0.75), 6)) + " cdf " + string(round(lognormal_cdf(2, 0.5, 0.75), 6)) + " q " + string(round(lognormal_quantile(0.6, 0.5, 0.75), 6)))

    print("weibull pdf " + string(round(weibull_pdf(1.5, 1.5, 2), 6)) + " cdf " + string(round(weibull_cdf(1.5, 1.5, 2), 6)) + " q " + string(round(weibull_quantile(0.5, 1.5, 2), 6)))

    print("negbinom pmf " + string(round(negbinom_pmf(3, 5, 0.4), 6)) + " cdf " + string(round(negbinom_cdf(3, 5, 0.4), 6)) + " q " + string(negbinom_quantile(0.5, 5, 0.4)))

    ' domain guards return unknown
    print("guard_uniform " + string(uniform_pdf(5, 8, 2)))
    print("guard_gamma " + string(gamma_cdf(2, 0, 1.5)))
    print("guard_negbinom " + string(negbinom_pmf(3, 5, 1.5)))
end program
