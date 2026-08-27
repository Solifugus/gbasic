' Exploratory factor analysis in stats.bas.
' Golden-compared by tests/run_factor_analysis.sh.
'
' THE FIXTURE HAS A KNOWN FACTOR STRUCTURE, built rather than sampled: six
' variables, the first three driven by one latent factor and the last three by
' another, the two independent. A correct EFA must recover exactly that block
' pattern. That is a much stronger test than a golden, and it earned its keep
' immediately -- it caught two bugs that both produced converged, plausible
' output: eigenvectors indexed transposed (Heywood cases everywhere), and a
' varimax criterion using the wrong two of its four sums (every loading pinned
' at 0.7, which is the LEAST simple structure there is).
'
' The distinction the whole feature turns on: FACTOR ANALYSIS IS NOT PCA. PCA
' summarises the observed variables and explains TOTAL variance; factor
' analysis posits latent causes and explains COMMON variance only. In the
' arithmetic that is one diagonal -- 1s for PCA, communalities for FA.

load stats

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

seed(42)
v0 = []
v1 = []
v2 = []
v3 = []
v4 = []
v5 = []
i = 0
while i < 300
    fa = random() + random() + random() - 1.5
    fb = random() + random() + random() - 1.5
    append(v0, 0.9 * fa + 0.2 * (random() - 0.5))
    append(v1, 0.85 * fa + 0.2 * (random() - 0.5))
    append(v2, 0.8 * fa + 0.2 * (random() - 0.5))
    append(v3, 0.9 * fb + 0.2 * (random() - 0.5))
    append(v4, 0.85 * fb + 0.2 * (random() - 0.5))
    append(v5, 0.8 * fb + 0.2 * (random() - 0.5))
    i += 1
end while
cols = [v0, v1, v2, v3, v4, v5]

print "-- it fits the constructed structure"
f = stats.factor_analysis(cols, { factors: 2 })
append(results, check("it fits", true, f.ok))
append(results, check("and converges", true, f.converged))
append(results, check("with no Heywood case", 0, f.heywood))
append(results, check("two factors", 2, f.n_factors))
append(results, check("six variables' loadings", 6, len(f.loadings)))
append(results, check("and it rotated", true, f.rotated))

print ""
print "-- THE BLOCK STRUCTURE IS RECOVERED"
' Each variable must load strongly on exactly ONE factor. Stated as a property
' over all six rather than as six pairs of digits, so it cannot be satisfied by
' a solution that happens to match one recorded run.
clean = true
i = 0
while i < 6
    hi = abs(f.loadings[i][0])
    lo = abs(f.loadings[i][1])
    if lo > hi then
        hi = abs(f.loadings[i][1])
        lo = abs(f.loadings[i][0])
    end if
    if hi < 0.9 or lo > 0.15 then
        clean = false
    end if
    i += 1
end while
append(results, check("every variable loads >0.9 on one factor and <0.15 on the other", true, clean))

' And the two blocks must land on DIFFERENT factors -- a solution loading all
' six on one factor would pass the test above.
function dominant(row)
    if abs(row[0]) >= abs(row[1]) then
        return 0
    end if
    return 1
end function
append(results, check("v0, v1, v2 share a factor", true, dominant(f.loadings[0]) = dominant(f.loadings[1]) and dominant(f.loadings[1]) = dominant(f.loadings[2])))
append(results, check("v3, v4, v5 share the other", true, dominant(f.loadings[3]) = dominant(f.loadings[4]) and dominant(f.loadings[4]) = dominant(f.loadings[5])))
append(results, check("and the two blocks differ", true, dominant(f.loadings[0]) != dominant(f.loadings[3])))

print ""
print "-- ROTATION CANNOT CHANGE FIT"
' The load-bearing invariant. Any rotation reproduces the same correlation
' matrix and the same communalities; it only relabels the axes. If a rotation
' ever moved a communality, the rotation would be wrong.
u = stats.factor_analysis(cols, { factors: 2, rotate: false })
append(results, check("the unrotated fit succeeds", true, u.ok))
append(results, check("it is marked unrotated", false, u.rotated))
same = true
i = 0
while i < 6
    if round(u.communalities[i], 9) != round(f.communalities[i], 9) then
        same = false
    end if
    i += 1
end while
append(results, check("communalities are identical to 9 places", true, same))
' The unrotated solution is the COMPLEX one -- that is what rotation is for.
append(results, check("unrotated, v0 loads on both factors", true, abs(u.loadings[0][0]) > 0.5 and abs(u.loadings[0][1]) > 0.5))

print ""
print "-- communality and uniqueness"
' FA sets aside each variable's unique variance, so a communality is a
' PROPORTION: it cannot reach 1 in a well-specified model, and h2 + u2 = 1.
ok_sum = true
i = 0
while i < 6
    if round(f.communalities[i] + f.uniquenesses[i], 9) != 1 then
        ok_sum = false
    end if
    if f.communalities[i] >= 1 or f.communalities[i] <= 0 then
        ok_sum = false
    end if
    i += 1
end while
append(results, check("communality plus uniqueness is 1, and h2 is in (0,1)", true, ok_sum))
' These variables are almost all common variance by construction.
append(results, check("communalities are high, as constructed", true, f.communalities[0] > 0.9))

print ""
print "-- FA IS NOT PCA, and this is the tier that can tell"
' The distinction is one diagonal: 1s for PCA, communalities for FA. On the
' clean fixture above the two nearly coincide (communalities are already 0.98),
' so NOTHING there can distinguish them -- proven, by making the substitution
' and watching every check stay green. It takes data with real unique variance.
'
' Here each variable is only about half common. Factor analysis sets the unique
' part aside and reports communalities near 0.40; leaving 1s on the diagonal
' folds each variable's own noise into the factors and reports near 0.60 --
' a 50% overstatement of how much the latent factors explain.
seed(7)
c = []
i = 0
while i < 6
    append(c, [])
    i += 1
end while
i = 0
while i < 300
    na = random() + random() - 1
    nb = random() + random() - 1
    j = 0
    while j < 3
        c[j] = c[j]
        append(c[j], 0.6 * na + 1.0 * (random() - 0.5))
        j += 1
    end while
    while j < 6
        c[j] = c[j]
        append(c[j], 0.6 * nb + 1.0 * (random() - 0.5))
        j += 1
    end while
    i += 1
end while
nf = stats.factor_analysis(c, { factors: 2 })
append(results, check("it fits noisy data", true, nf.ok))
' Every communality must sit in the factor-analysis band, well under what
' leaving 1s on the diagonal produces (~0.60).
in_band = true
i = 0
while i < 6
    if nf.communalities[i] > 0.55 or nf.communalities[i] < 0.25 then
        in_band = false
    end if
    i += 1
end while
append(results, check("common variance only: communalities near 0.40, not 0.60", true, in_band))
' And the structure is still found, so the low communalities are not a failure
' to fit -- they are the model correctly declining to claim the noise.
append(results, check("the block structure survives the noise", true, dominant(nf.loadings[0]) = dominant(nf.loadings[1]) and dominant(nf.loadings[0]) != dominant(nf.loadings[4])))

print ""
print "-- refusals"
append(results, check("fewer than three variables", false, stats.factor_analysis([v0, v1], { factors: 1 }).ok))
append(results, check("more factors than variables", false, stats.factor_analysis(cols, { factors: 6 }).ok))
append(results, check("zero factors", false, stats.factor_analysis(cols, { factors: 0 }).ok))
append(results, check("columns of different lengths", false, stats.factor_analysis([v0, v1, [1,2,3]], { factors: 1 }).ok))
append(results, check("not an array of columns", false, stats.factor_analysis([1,2,3], { factors: 1 }).ok))
append(results, check("no variables at all", false, stats.factor_analysis([], { factors: 1 }).ok))
' Fewer observations than variables makes the correlation matrix unestimable.
append(results, check("too few observations", false, stats.factor_analysis([[1,2],[2,1],[1,1]], { factors: 1 }).ok))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
