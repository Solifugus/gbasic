' stats.bas normal distribution composition (statistics_design.md §8 Phase 1).
' Values checked against scipy.stats.norm.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Standard normal landmarks.
    print("pdf0 " + string(round(stats.normal_pdf(0, 0, 1), 9)))
    print("cdf0 " + string(round(stats.normal_cdf(0, 0, 1), 9)))
    print("cdf1 " + string(round(stats.normal_cdf(1, 0, 1), 9)))
    print("cdf196 " + string(round(stats.normal_cdf(1.959963985, 0, 1), 6)))
    print("q975 " + string(round(stats.normal_quantile(0.975, 0, 1), 6)))
    print("q500 " + string(round(stats.normal_quantile(0.5, 0, 1), 9)))
    print("q025 " + string(round(stats.normal_quantile(0.025, 0, 1), 6)))

    ' Round-trip: quantile(cdf(x)) recovers x.
    print("roundtrip " + string(round(stats.normal_quantile(stats.normal_cdf(1.3, 0, 1), 0, 1), 9)))

    ' Parameterized N(100, 15) — the IQ scale.
    print("iq_cdf115 " + string(round(stats.normal_cdf(115, 100, 15), 9)))
    print("iq_q90 " + string(round(stats.normal_quantile(0.9, 100, 15), 6)))
    print("z115 " + string(round(stats.zscore(115, 100, 15), 9)))

    ' Domain guards return unknown (the native NA), not a bogus number.
    print("badsigma " + string(stats.normal_pdf(0, 0, 0)))
    print("badp " + string(stats.normal_quantile(1.5, 0, 1)))
end program
