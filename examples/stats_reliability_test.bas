' stats.bas Phase 8 — reliability & agreement
' (docs/statistics_scientist_plan.md §8). Cronbach's alpha, Cohen's kappa,
' intraclass correlation, Krippendorff's alpha. Values verified against
' statsmodels (kappa), published Shrout & Fleiss (1979) ICCs, and the
' standard Krippendorff worked example (nominal 0.743, interval 0.849).
' gBASIC prints ~6 sig figs, so results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Cronbach's alpha: 8 subjects x 4 items.
    items = [ [4, 3, 4, 3], [5, 4, 5, 5], [3, 2, 3, 2], [4, 4, 3, 4], [2, 1, 2, 1], [5, 5, 4, 5], [3, 3, 3, 2], [1, 2, 1, 1] ]
    ca = stats.cronbach_alpha(items)
    print("cronbach alpha " + string(round(ca.alpha, 6)) + " items " + string(ca.n_items) + " subj " + string(ca.n_subjects))

    ' Cohen's kappa: two raters over 15 items.
    r1 = [1, 2, 3, 1, 2, 3, 3, 2, 1, 1, 2, 3, 1, 1, 2]
    r2 = [1, 2, 3, 1, 2, 2, 3, 2, 1, 2, 2, 3, 1, 1, 3]
    ck = stats.cohens_kappa(r1, r2)
    print("kappa " + string(round(ck.kappa, 6)) + " po " + string(round(ck.po, 6)) + " pe " + string(round(ck.pe, 6)))

    ' ICC: Shrout & Fleiss (1979) 6 subjects x 4 judges.
    sf = [ [9, 2, 5, 8], [6, 1, 3, 2], [8, 4, 6, 8], [7, 1, 2, 6], [10, 5, 6, 9], [6, 2, 4, 7] ]
    ic = stats.icc(sf)
    print("icc1 " + string(round(ic.icc1, 6)) + " icc2 " + string(round(ic.icc2, 6)) + " icc3 " + string(round(ic.icc3, 6)))
    print("icc1k " + string(round(ic.icc1k, 6)) + " icc2k " + string(round(ic.icc2k, 6)) + " icc3k " + string(round(ic.icc3k, 6)))

    ' Krippendorff's alpha: 4 observers x 12 units with missing ratings.
    data = [ [1, 2, 3, 3, 2, 1, 4, 1, 2, unknown, unknown, unknown], [1, 2, 3, 3, 2, 2, 4, 1, 2, 5, unknown, 3], [unknown, 3, 3, 3, 2, 3, 4, 2, 2, 5, 1, unknown], [1, 2, 3, 3, 2, 4, 4, 1, 2, 5, 1, unknown] ]
    print("kripp nominal " + string(round(stats.krippendorff_alpha(data, "nominal").alpha, 6)))
    print("kripp interval " + string(round(stats.krippendorff_alpha(data, "interval").alpha, 6)))
    print("kripp ordinal " + string(round(stats.krippendorff_alpha(data, "ordinal").alpha, 6)))

    ' domain guards return unknown
    print("guard_cronbach " + string(stats.cronbach_alpha([ [1, 2] ])))
    print("guard_kappa " + string(stats.cohens_kappa([1, 2], [1])))
    print("guard_icc " + string(stats.icc([ [1, 2] ])))
end program
