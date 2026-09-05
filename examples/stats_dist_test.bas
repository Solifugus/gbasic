' stats.bas distributions: Student's t, chi-squared, F, binomial, Poisson
' (statistics_design.md §8 Phase 1). Each pdf/pmf, cdf, and quantile checked
' against scipy.stats. gBASIC prints numbers to ~6 significant figures, which
' is also the fixed precision the design fixes for cross-architecture
' determinism, so values are rounded to 6 decimals before printing.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' --- Student's t ---
    print("t_pdf_0_10 " + string(round(stats.t_pdf(0, 10), 6)))
    print("t_pdf_2_10 " + string(round(stats.t_pdf(2, 10), 6)))
    print("t_cdf_2_10 " + string(round(stats.t_cdf(2, 10), 6)))
    print("t_cdf_m15_5 " + string(round(stats.t_cdf(-1.5, 5), 6)))
    print("t_cdf_0_7 " + string(round(stats.t_cdf(0, 7), 6)))
    print("t_q975_10 " + string(round(stats.t_quantile(0.975, 10), 6)))
    print("t_q025_10 " + string(round(stats.t_quantile(0.025, 10), 6)))
    print("t_q500_10 " + string(round(stats.t_quantile(0.5, 10), 6)))
    print("t_roundtrip " + string(round(stats.t_quantile(stats.t_cdf(1.3, 8), 8), 6)))
    print("t_cdf_df0 " + string(stats.t_cdf(2, 0)))
    print("t_q_oob " + string(stats.t_quantile(1.5, 10)))

    ' --- Chi-squared ---
    print("chi2_pdf_2_3 " + string(round(stats.chi2_pdf(2, 3), 6)))
    print("chi2_cdf_7_3 " + string(round(stats.chi2_cdf(7, 3), 6)))
    print("chi2_cdf_0_3 " + string(round(stats.chi2_cdf(0, 3), 6)))
    print("chi2_q95_3 " + string(round(stats.chi2_quantile(0.95, 3), 6)))
    print("chi2_q05_10 " + string(round(stats.chi2_quantile(0.05, 10), 6)))
    print("chi2_q500_1 " + string(round(stats.chi2_quantile(0.5, 1), 6)))
    print("chi2_cdf_k0 " + string(stats.chi2_cdf(5, 0)))

    ' --- F ---
    print("f_pdf_1_5_10 " + string(round(stats.f_pdf(1, 5, 10), 6)))
    print("f_cdf_3_5_10 " + string(round(stats.f_cdf(3, 5, 10), 6)))
    print("f_q95_5_10 " + string(round(stats.f_quantile(0.95, 5, 10), 6)))
    print("f_q99_2_20 " + string(round(stats.f_quantile(0.99, 2, 20), 6)))
    print("f_cdf_neg " + string(round(stats.f_cdf(-1, 5, 10), 6)))
    print("f_cdf_d0 " + string(stats.f_cdf(3, 0, 10)))

    ' --- Binomial ---
    print("binom_pmf_3_10_5 " + string(round(stats.binom_pmf(3, 10, 0.5), 6)))
    print("binom_pmf_0_10_5 " + string(round(stats.binom_pmf(0, 10, 0.5), 6)))
    print("binom_pmf_10_10_5 " + string(round(stats.binom_pmf(10, 10, 0.5), 6)))
    print("binom_cdf_3_10_5 " + string(round(stats.binom_cdf(3, 10, 0.5), 6)))
    print("binom_cdf_6_20_3 " + string(round(stats.binom_cdf(6, 20, 0.3), 6)))
    print("binom_q50_10_5 " + string(stats.binom_quantile(0.5, 10, 0.5)))
    print("binom_q95_20_3 " + string(stats.binom_quantile(0.95, 20, 0.3)))
    print("binom_pmf_oob " + string(stats.binom_pmf(3, 10, 1.5)))

    ' --- Poisson ---
    print("pois_pmf_2_3 " + string(round(stats.pois_pmf(2, 3), 6)))
    print("pois_pmf_0_3 " + string(round(stats.pois_pmf(0, 3), 6)))
    print("pois_cdf_2_3 " + string(round(stats.pois_cdf(2, 3), 6)))
    print("pois_cdf_8_10 " + string(round(stats.pois_cdf(8, 10), 6)))
    print("pois_q50_3 " + string(stats.pois_quantile(0.5, 3)))
    print("pois_q95_10 " + string(stats.pois_quantile(0.95, 10)))
    print("pois_cdf_neg " + string(stats.pois_cdf(2, -3)))
end program
