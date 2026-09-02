' scoring.bas -- credit scorecards (docs/scoring_design.md).
'
' SELF-CHECKING, and here it is forced. EVERY defect this library can produce
' is a plausible number: a WOE orientation that flips every sign, an AUC below
' 0.5 quietly turned the right way up, a point scale running backwards. All
' three yield a scorecard with ordinary-looking discrimination pointed at the
' people who will not pay, and a golden would record every one of them as
' expected and then defend it.
'
' Expected WOE, IV, AUC, Gini, KS, scaling and PSI figures were computed
' OUTSIDE gBASIC, in Python, from the bin counts. They are evidence only
' because of that; a fixture asserting what the library said is a transcript.

load scoring
load stats

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function near(label, got, want, tol)
    tally.checks = tally.checks + 1
    if abs(got - want) <= tol then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' --- the population -------------------------------------------------------
' Four bins of unequal size, bad rate falling with x but not perfectly.
' 160 rows: 124 goods, 36 bads.

' NOTE the population is built INLINE rather than through a helper: an array
' is a VALUE, so `append` inside a function mutates a local copy and the
' caller's array stays empty. That is the standing gBASIC rule (UNLEARN), and
' it is worth seeing in a fixture that would otherwise silently test nothing --
' the first draft of this file did exactly that and reported 0 rows.
population = [{ x: 10, bad: 1, k: 18 }, { x: 10, bad: 0, k: 12 },
              { x: 30, bad: 1, k: 10 }, { x: 30, bad: 0, k: 30 },
              { x: 50, bad: 1, k: 6 },  { x: 50, bad: 0, k: 44 },
              { x: 70, bad: 1, k: 2 },  { x: 70, bad: 0, k: 38 }]
xs = []
ys = []
for each s in population
    for i = 1 to s.k
        append(xs, s.x)
        append(ys, s.bad)
    next
next

check("the population is 160 rows", count(xs), 160)

cuts = [20, 40, 60]
bins = scoring.bin_numeric(xs, ys, cuts)
check("four bins are occupied", count(bins), 4)
check("the lowest bin is 12 good / 18 bad",
      string(bins[0].goods) + "/" + string(bins[0].bads), "12/18")

' --- TIER: WOE and IV against figures computed outside gBASIC -------------
t = scoring.woe_table(bins, { orientation: "good_bad" })
near("woe of the worst bin", t.rows[0].woe, -1.6422277353, 0.0000001)
near("woe of the second bin", t.rows[1].woe, -0.1381503385, 0.0000001)
near("woe of the third bin", t.rows[2].woe, 0.7556675375, 0.0000001)
near("woe of the best bin", t.rows[3].woe, 1.7076763520, 0.0000001)
near("information value", t.iv, 1.2377849963, 0.0000001)
near("the worst bin's bad rate", t.rows[0].bad_rate, 0.6, 0.0000001)
check("the distributions sum to one",
      round(t.rows[0].dist_good + t.rows[1].dist_good
            + t.rows[2].dist_good + t.rows[3].dist_good, 10), 1)

' --- TIER: ORIENTATION IS A DIFFERENCE ------------------------------------
' The whole reason it is declared. The two conventions must give WOE of EQUAL
' MAGNITUDE and OPPOSITE SIGN -- a suite that cannot tell them apart is not
' testing the declaration, and a library that ignored `orientation` would
' pass every check above.
u = scoring.woe_table(bins, { orientation: "bad_good" })
opposite = 0
for i = 0 to count(t.rows) - 1
    if abs(t.rows[i].woe + u.rows[i].woe) < 0.0000000001 then
        opposite = opposite + 1
    end if
next
check("every bin's woe flips sign under the other orientation", opposite, 4)
check("and none of them is zero, so the flip is observable",
      abs(t.rows[0].woe) > 0.1, true)
' IV is orientation-INDEPENDENT, which is a fact about the formula rather than
' a coincidence: the log ratio and the difference flip together.
near("information value is the same either way", u.iv, t.iv, 0.0000000001)

' --- TIER: AUC against a brute-force pair count ---------------------------
' The DEFINITION, reimplemented here: every good/bad pair compared directly.
' The library uses the rank identity, so this is a second implementation and
' not the same function called twice.

function auc_bruteforce(scores, outcomes)
    total = 0
    ng = 0
    nb = 0
    for i = 0 to count(scores) - 1
        if outcomes[i] = 1 then
            nb = nb + 1
        else
            ng = ng + 1
        end if
    next
    for i = 0 to count(scores) - 1
        if outcomes[i] = 0 then
            for j = 0 to count(scores) - 1
                if outcomes[j] = 1 then
                    if scores[i] > scores[j] then
                        total = total + 1
                    else if scores[i] = scores[j] then
                        total = total + 0.5
                    end if
                end if
            next
        end if
    next
    return total / (ng * nb)
end function

a = scoring.auc(xs, ys)
near("auc against the value computed outside gBASIC", a.auc, 0.7822580645, 0.0000001)
near("auc against a brute-force pair count", a.auc, auc_bruteforce(xs, ys), 0.0000000001)
near("gini is 2*auc - 1", a.gini, 0.5645161290, 0.0000001)
check("and the model is not reversed", a.reversed, false)

' --- TIER: what AUC must say at the extremes ------------------------------
perfect_s = []
perfect_y = []
for i = 1 to 20
    append(perfect_s, 100 + i)
    append(perfect_y, 0)
next
for i = 1 to 20
    append(perfect_s, i)
    append(perfect_y, 1)
next
near("a perfect separator scores 1", scoring.auc(perfect_s, perfect_y).auc, 1, 0.0000000001)

flat_s = []
for i = 1 to 40
    append(flat_s, 7)
next
near("a model with no information scores 0.5 (all ties)",
     scoring.auc(flat_s, perfect_y).auc, 0.5, 0.0000000001)

' A BACKWARDS MODEL IS REPORTED BACKWARDS. This is the tier that matters most
' in the whole file: flipping it silently turns the most consequential error
' in scorecard work into a mediocre-looking result that gets deployed.
neg = []
for i = 0 to count(xs) - 1
    append(neg, 0 - xs[i])
next
r = scoring.auc(neg, ys)
near("a reversed model reports its auc BELOW 0.5, not flipped",
     r.auc, 0.2177419355, 0.0000001)
check("and says so", r.reversed, true)
near("its gini is negative", r.gini, 0 - 0.5645161290, 0.0000001)

' --- TIER: KS is its own quantity -----------------------------------------
k = scoring.ks(xs, ys)
near("ks against the value computed outside gBASIC", k.ks, 0.6810035842, 0.0000001)
check("ks and auc are different numbers", abs(k.ks - a.auc) > 0.05, true)

' --- TIER: the point scale ------------------------------------------------
sc = scoring.scaling({ base_score: 650, base_odds: 20, pdo: 20 })
near("the factor is pdo / ln 2", sc.factor, 28.8539008178, 0.0000001)
near("the offset", sc.offset, 563.5614381023, 0.0000001)
near("base odds give the base score", scoring.points_of(sc, log(20)), 650, 0.0000001)

' THE DEFINING PROPERTY, asserted as arithmetic rather than as a value:
' `pdo` more points must exactly double the odds, and the scale must invert.
near("pdo more points doubles the odds", scoring.points_of(sc, log(40)), 670, 0.0000001)
near("pdo fewer points halves them", scoring.points_of(sc, log(10)), 630, 0.0000001)
near("the scale inverts", exp(scoring.log_odds_of(sc, 650)), 20, 0.0000001)
near("and inverts away from the base point too",
     exp(scoring.log_odds_of(sc, 690)), 80, 0.0000001)

function bucket_of(x)
    if x < 20 then
        return 0
    end if
    if x < 40 then
        return 1
    end if
    if x < 60 then
        return 2
    end if
    return 3
end function

' --- TIER: DIRECTION, end to end ------------------------------------------
' The statement a sign error anywhere above cannot survive: fit a real model
' on the WOE column, scale it to points, and require the KNOWN BADS to score
' LOWER than the known goods. Every other tier here checks a component; this
' one checks that the components are wired together the right way up.
woe_col = []
for i = 0 to count(xs) - 1
    append(woe_col, scoring.woe_of(t, bins[bucket_of(xs[i])].label))
next
model = stats.logistic_regression(ys, [woe_col])
check("the model converged", model.converged, true)

' The model predicts P(bad), so ln(good/bad) is the NEGATED linear predictor.
good_pts = 0
good_n = 0
bad_pts = 0
bad_n = 0
for i = 0 to count(xs) - 1
    eta = model.coefficients[0] + model.coefficients[1] * woe_col[i]
    pts = scoring.points_of(sc, 0 - eta)
    if ys[i] = 1 then
        bad_pts = bad_pts + pts
        bad_n = bad_n + 1
    else
        good_pts = good_pts + pts
        good_n = good_n + 1
    end if
next
check("the known bads score BELOW the known goods",
      bad_pts / bad_n < good_pts / good_n, true)
check("and the gap is material, not a rounding difference",
      good_pts / good_n - bad_pts / bad_n > 20, true)

' --- TIER: stability -------------------------------------------------------
p = scoring.psi([100, 200, 300, 250, 150], [120, 180, 280, 260, 160])
near("psi against the value computed outside gBASIC", p.psi, 0.0081710912, 0.0000001)
check("a small shift is labelled stable", p.note, "stable")
big = scoring.psi([100, 200, 300, 250, 150], [400, 300, 150, 100, 50])
check("a large shift is labelled", big.note, "large shift")
check("and the label is offered as a rule of thumb, not a verdict",
      contains(big.thresholds, "rule of thumb"), true)

' --- TIER: refusals, each beside its nearest legal neighbour ---------------
on error goto next

x = scoring.woe_table(bins, { })
check("a missing orientation is refused", contains(error.message, "neither is the default"), true)
error.clear()

x = scoring.woe_table(bins, { orientation: "whichever" })
check("an unknown orientation is refused by name",
      contains(error.message, "they differ by SIGN"), true)
error.clear()

' An empty cell is refused rather than smoothed -- and the message says which
' bin and which side, because "re-bin" is unactionable without that.
empty_pop = [{ x: 10, bad: 1, k: 5 }, { x: 30, bad: 0, k: 20 },
             { x: 50, bad: 0, k: 20 }]
empty_x = []
empty_y = []
for each s in empty_pop
    for i = 1 to s.k
        append(empty_x, s.x)
        append(empty_y, s.bad)
    next
next
eb = scoring.bin_numeric(empty_x, empty_y, [20, 40])
x = scoring.woe_table(eb, { orientation: "good_bad" })
check("a bin with no goods is refused, not smoothed",
      contains(error.message, "has no goods"), true)
error.clear()

x = scoring.bin_numeric(xs, ys, [40, 20])
check("cut points that do not ascend are refused",
      contains(error.message, "must ascend"), true)
error.clear()

x = scoring.bin_categorical(["a", "z"], [1, 0], [{ label: "A", values: ["a"] }])
check("a category in no group is refused rather than swept into \"other\"",
      contains(error.message, "is in no group"), true)
error.clear()

x = scoring.auc(xs, [1, 0])
check("mismatched lengths are refused",
      contains(error.message, "160 scores against 2 outcomes"), true)
error.clear()

all_good = []
for i = 1 to 10
    append(all_good, 0)
next
x = scoring.auc([1,2,3,4,5,6,7,8,9,10], all_good)
check("an outcome column with one class is refused",
      contains(error.message, "one class is absent"), true)
error.clear()

x = scoring.auc([1, 2], [0, 2])
check("an outcome that is not 0 or 1 is refused",
      contains(error.message, "1 for BAD and 0 for good"), true)
error.clear()

x = scoring.scaling({ base_score: 650, base_odds: 20 })
check("a scaling without a pdo is refused",
      contains(error.message, "silently redefine every cut-off"), true)
error.clear()

x = scoring.woe_of(t, "no such bin")
check("a value the table never saw is refused",
      contains(error.message, "no evidence to offer"), true)
error.clear()

x = scoring.psi([1, 2, 3], [1, 2])
check("psi over mismatched bands is refused",
      contains(error.message, "the bands must match"), true)
error.clear()

on error stop

' THE CONTROL: the nearest legal neighbour of each refusal still works.
' Without it, every refusal above is satisfied by a library that refuses
' everything.
smoothed = scoring.woe_table(eb, { orientation: "good_bad", smoothing: 0.5 })
check("the SAME empty bin is accepted once a smoothing is declared",
      count(smoothed.rows), 3)
check("and the smoothing is reported, so the invented evidence is visible",
      smoothed.smoothing, 0.5)
ok_cat = scoring.bin_categorical(["a", "z"], [1, 0],
           [{ label: "A", values: ["a"] }, { label: "Z", values: ["z"] }])
check("a fully covered categorical binning is accepted", count(ok_cat), 2)
ok_scale = scoring.scaling({ base_score: 600, base_odds: 50, pdo: 25 })
check("a fully declared scaling is accepted", ok_scale.pdo, 25)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
