' stats.bas Phase 3 — multivariate / unsupervised (statistics_design.md §8 Phase 3).
' Anomaly detection (zscore_outliers, iqr_outliers), k-means clustering with
' k-means++ seeding, agglomerative hierarchical clustering (single / complete /
' average linkage) with a dendrogram cut, and PCA over a pure-gBASIC Jacobi
' eigensolver. Values checked against scipy / numpy; gBASIC prints ~6 sig figs
' (the cross-architecture determinism rule), so results round to 6 decimals.
program demo(args)
    load stats from "../stdlib/stats.bas"
    seed(12345)

    ' --- anomaly detection (1-D) ---
    obs = [10, 12, 11, 13, 12, 11, 100, 12, 10, 11]
    zo = zscore_outliers(obs, 2.5)
    print("zscore idx " + string(zo.indices[0]) + " val " + string(zo.values[0]) + " score " + string(round(zo.scores[6], 6)))
    print("zscore mean " + string(round(zo.mean, 6)) + " stdev " + string(round(zo.stdev, 6)) + " count " + string(len(zo.indices)))
    io = iqr_outliers(obs, 1.5)
    print("iqr idx " + string(io.indices[0]) + " val " + string(io.values[0]) + " lo " + string(round(io.lower, 6)) + " hi " + string(round(io.upper, 6)))

    ' --- k-means: three well-separated clusters ---
    dk = [[1, 1], [1.5, 2], [1, 0.5], [8, 8], [9, 9], [8.5, 7.5], [1, 8], [1.5, 9], [0.5, 8.5]]
    km = kmeans(dk, 3, 100)
    print("km inertia " + string(round(km.inertia, 6)) + " iters " + string(km.iterations) + " conv " + string(km.converged))
    ' Same-group points must share a label (label numbers depend on seeding).
    s0 = "grpA " + string(km.labels[0] = km.labels[1]) + " " + string(km.labels[1] = km.labels[2])
    s1 = "grpB " + string(km.labels[3] = km.labels[4]) + " " + string(km.labels[4] = km.labels[5])
    s2 = "grpC " + string(km.labels[6] = km.labels[7]) + " " + string(km.labels[7] = km.labels[8])
    print(s0 + " " + s1 + " " + s2)

    ' --- hierarchical clustering (distinct merge distances → matches scipy) ---
    dh = [[0, 0], [0.7, 1.3], [1.4, 0.5], [8.1, 8.7], [9.4, 7.2], [7.3, 9.5], [0.3, 8.2], [1.6, 9.6], [0.9, 7.1]]
    methods = ["single", "complete", "average"]
    mi = 0
    while mi < 3
        meth = methods[mi]
        hm = hierarchical(dh, meth)
        line = meth
        r = 0
        while r < len(hm.merges)
            row = hm.merges[r]
            line = line + " [" + string(row[0]) + "," + string(row[1]) + "," + string(round(row[2], 6)) + "]"
            r = r + 1
        end while
        print(line)
        cut = cut_tree(hm, 3)
        cl = "  cut3"
        c = 0
        while c < len(cut)
            cl = cl + " " + string(cut[c])
            c = c + 1
        end while
        print(cl)
        mi = mi + 1
    end while

    ' --- PCA ---
    dp = [[2.5, 2.4], [0.5, 0.7], [2.2, 2.9], [1.9, 2.2], [3.1, 3.0], [2.3, 2.7], [2.0, 1.6], [1.0, 1.1], [1.5, 1.6], [1.1, 0.9]]
    pc = pca(dp, 2)
    print("pca mean " + string(round(pc.mean[0], 6)) + " " + string(round(pc.mean[1], 6)))
    print("pca var " + string(round(pc.explained_variance[0], 6)) + " " + string(round(pc.explained_variance[1], 6)))
    print("pca ratio " + string(round(pc.explained_variance_ratio[0], 6)) + " " + string(round(pc.explained_variance_ratio[1], 6)))
    print("pca comp0 " + string(round(pc.components[0][0], 6)) + " " + string(round(pc.components[0][1], 6)))
    print("pca comp1 " + string(round(pc.components[1][0], 6)) + " " + string(round(pc.components[1][1], 6)))
    print("pca score0 " + string(round(pc.scores[0][0], 6)) + " " + string(round(pc.scores[0][1], 6)))
    print("pca score1 " + string(round(pc.scores[1][0], 6)) + " " + string(round(pc.scores[1][1], 6)))
    print("pca score2 " + string(round(pc.scores[2][0], 6)) + " " + string(round(pc.scores[2][1], 6)))

    ' --- domain guards return unknown ---
    print("guard_km " + string(kmeans(dk, 0, 10)))
    print("guard_pca " + string(pca(dp, 5)))
    print("guard_hier " + string(hierarchical(dh, "ward")))
end program
