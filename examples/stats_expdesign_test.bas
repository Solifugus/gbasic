' stats.bas Phase 7 — experimental-design tests
' (docs/statistics_scientist_plan.md §7). Two-way ANOVA, repeated-measures
' ANOVA, Friedman test, Tukey HSD (studentized-range p-values + CIs).
' Values verified against statsmodels / scipy.stats; gBASIC prints ~6 sig
' figs, so results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"

    ' Two-way balanced ANOVA: 2 A-levels x 3 B-levels, 3 replicates per cell.
    cells = [ [ [12, 14, 11], [20, 19, 22], [15, 16, 14] ], [ [18, 17, 19], [25, 24, 26], [21, 20, 23] ] ]
    aw = anova_twoway(cells)
    print("twoway A f " + string(round(aw.a.statistic, 6)) + " p " + string(round(aw.a.p_value, 6)))
    print("twoway B f " + string(round(aw.b.statistic, 6)) + " p " + string(round(aw.b.p_value, 6)))
    print("twoway AB f " + string(round(aw.interaction.statistic, 6)) + " p " + string(round(aw.interaction.p_value, 6)))

    ' Repeated-measures ANOVA: 5 subjects x 3 conditions.
    rm = [ [10, 12, 13], [9, 11, 11], [11, 12, 15], [8, 10, 11], [12, 13, 14] ]
    ra = anova_repeated(rm)
    print("rm f " + string(round(ra.statistic, 6)) + " df1 " + string(ra.df1) + " df2 " + string(ra.df2) + " p " + string(round(ra.p_value, 6)) + " peta " + string(round(ra.partial_eta2, 6)))

    ' Friedman on the same layout (with within-row ties).
    fr = friedman(rm)
    print("friedman stat " + string(round(fr.statistic, 6)) + " df " + string(fr.df) + " p " + string(round(fr.p_value, 6)))

    ' Tukey HSD across 3 groups.
    groups = [ [24, 26, 25, 23, 27], [30, 31, 29, 32, 28], [20, 22, 19, 21, 23] ]
    tk = tukey_hsd(groups)
    i = 0
    while i < len(tk)
        row = tk[i]
        print("tukey " + string(row.group1) + "-" + string(row.group2) + " md " + string(round(row.mean_diff, 6)) + " padj " + string(round(row.p_adj, 6)) + " ci " + string(round(row.ci_low, 6)) + " " + string(round(row.ci_high, 6)) + " reject " + string(row.reject))
        i = i + 1
    end while

    ' domain guards return unknown
    print("guard_twoway " + string(anova_twoway([ [ [1, 2], [3, 4] ] ])))
    print("guard_rm " + string(anova_repeated([ [1, 2, 3] ])))
    print("guard_tukey " + string(tukey_hsd([ [1, 2, 3] ])))
end program
