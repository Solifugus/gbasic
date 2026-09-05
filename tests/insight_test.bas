' insight.explain_change (docs/automation_reasoning_design.md §13, §14).
'
' SELF-CHECKING, NOT GOLDEN, AND FORCED. Every defect this increment exists to
' prevent produces a CONFIDENT, ORDINARY-LOOKING CAUSAL STORY -- a chain of
' plausible percentages naming a place and a category. A golden would record
' one as expected and then defend it, which is exactly how Recipe 1 showed the
' unguarded version failing.
'
' THE LOAD-BEARING TIER IS THE PLANTED/NULL PAIR. Everything else here checks a
' component; that one checks the library can tell a cause from nothing.

load insight
load reasoning
load frame
load fake
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

' --- populations -----------------------------------------------------------
' `plant` = 1 puts a real 55% collapse in one known cell. `plant` = 0 puts
' nothing anywhere. `slump` declines EVERY cell, so the aggregate itself is
' real -- that is the control for the share refusal.

function build(seed, plant, slump)
    load fake
    regions = ["North", "South", "East", "West"]
    cats = ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
    rows = []
    i = 0
    for each rg in regions
        for each c in cats
            for d = 1 to 30
                for p = 0 to 1
                    i = i + 1
                    amt = fake.lognormal(seed, i, 1000, 0.5)
                    stock = fake.lognormal(seed, i + 500000, 400, 0.4)
                    if p = 1 then
                        if plant = 1 and rg = "North" and c = "Outdoor" then
                            amt = amt * 0.45
                            stock = stock * 0.45
                        end if
                        ' R13 CHANGED WHAT THIS CONTROL HAS TO BE, and the
                        ' change is the point rather than an adjustment. It
                        ' used to slump EVERY cell by 40% -- a movement common
                        ' to the whole population, which is what an ordinary
                        ' January looks like (Recipe 3), so a share of it
                        ' attributes to one cell what every cell did. The
                        ' control for R2 must be a decline that is SOMEWHERE:
                        ' six of the twenty cells collapse, scattered across
                        ' regions so no single branch of the decomposition
                        ' owns them, and the other fourteen are left alone.
                        ' Measured across five seeds this reports shares every
                        ' time, at t -2.76 to -3.17 and sign p 0.12 to 0.50.
                        ' slump = 2 is the OLD control and is now R13's
                        ' subject: a shift common to every cell.
                        if slump = 2 then
                            amt = amt * 0.6
                        end if
                        if slump = 1 and contains(["North/Outdoor",
                                                   "South/Apparel",
                                                   "East/Home",
                                                   "West/Grocery",
                                                   "North/Home",
                                                   "South/Grocery"],
                                                  rg + "/" + c) then
                            amt = amt * 0.15
                        end if
                    end if
                    append(rows, { region: rg, category: c, period: p,
                                   revenue: amt, stock: stock })
                next
            next
        next
    next
    return frame.from_rows(rows)
end function

function spec_for(nulls)
    return { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "category"],
             comparison: "period_over_period", null: nulls }
end function

planted = insight.explain_change(build(4242, 1, 0), spec_for("siblings"))
quiet = insight.explain_change(build(58, 0, 0), spec_for("siblings"))
slumped = insight.explain_change(build(31, 0, 1), spec_for("siblings"))
common = insight.explain_change(build(31, 0, 2), spec_for("siblings"))
' R15's population: one business of FIXED total size, cut coarse and fine.
' 8,000 transactions per period however many cells they are spread across, so
' the only thing that changes between the two cuts is how much of the business
' is left in each cell.
function cut_business(ncells, per_cell, collapse_cell, frac)
    load fake
    load frame
    rows = []
    for c = 0 to ncells - 1
        for d = 1 to per_cell
            for p = 0 to 1
                amt = fake.lognormal(4242, c * 100000 + d * 10 + p, 1000, 0.7)
                if p = 1 and c = collapse_cell then
                    amt = amt * frac
                end if
                append(rows, { sku: "S" + string(c), period: p, revenue: amt })
            next
        next
    next
    return frame.from_rows(rows)
end function

function cut_look(ncells, per_cell, collapse_cell, frac)
    load insight
    return insight.explain_change(
             cut_business(ncells, per_cell, collapse_cell, frac),
             { measure: "revenue", period: "period", baseline: 0, current: 1,
               dimensions: ["sku"], comparison: "period_over_period",
               null: "siblings" })
end function

function cell_zero(f)
    for each c in f.contributors
        if c.path[0] = "S0" then
            return c
        end if
    next
    return nothing
end function

' R14's population: one WATCHED cell broken in every run, and a varying number
' of unrelated cells broken beside it. The watched cell is identical
' throughout, so anything that moves its verdict is a fact about its
' neighbours.
function two_causes(others)
    load fake
    load frame
    broken = ["North/Outdoor"]
    spare = ["South/Apparel", "East/Home", "West/Grocery"]
    for j = 0 to others - 1
        append(broken, spare[j])
    next
    rows = []
    ri = 0
    for each rg in ["North", "South", "East", "West"]
        ci = 0
        for each c in ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
            hit = contains(broken, rg + "/" + c)
            for d = 1 to 30
                for p = 0 to 1
                    amt = fake.lognormal(4242, ri * 100000 + ci * 10000 + d * 10 + p,
                                         1000, 0.5)
                    if p = 1 and hit then
                        amt = amt * 0.25
                    end if
                    append(rows, { region: rg, category: c, period: p, revenue: amt })
                next
            next
            ci = ci + 1
        next
        ri = ri + 1
    next
    return frame.from_rows(rows)
end function

function watched_z(others, max_causes)
    load insight
    nl = "siblings"
    if max_causes > 1 then
        nl = "siblings_permuted"
    end if
    ' `draws` is tiny on purpose: this asks what the STATISTIC does, and the
    ' statistic does not depend on the number of permutations. The threshold
    ' is asserted in the recipe tier, where it is computed properly.
    f = insight.explain_change(two_causes(others),
          { measure: "revenue", period: "period", baseline: 0, current: 1,
            dimensions: ["region", "category"], comparison: "period_over_period",
            null: nl, max_causes: max_causes, draws: 8 })
    for each c in f.contributors
        if c.path[0] = "North" and c.path[1] = "Outdoor" then
            return c.z
        end if
    next
    return 0
end function
common_planted = insight.explain_change(build(4242, 1, 2), spec_for("siblings"))

' --- TIER: the shape of a Finding -----------------------------------------
check("the decomposition found 20 cells", planted.search.cells, 20)
check("and records the dimensions it searched",
      join(planted.search.dimensions, ","), "region,category")
check("the null is recorded, not assumed", planted.null.kind, "siblings")
check("a Finding carries associations, never a cause",
      is_unknown(planted["cause"]), true)
check("and no materiality -- that needs objectives", is_unknown(planted["materiality"]), true)

' --- TIER: THE PLANTED/NULL PAIR ------------------------------------------
' Recipe 1's finding, now as an assertion the library must keep passing: the
' same decomposition over a real cause and over pure noise must reach OPPOSITE
' verdicts. Without this the rest is a set of tidy numbers about nothing.
check("the planted run leads with the planted cell",
      join(planted.strength.leader, "/"), "North/Outdoor")
check("and that cell clears the search-width threshold", planted.strength.clears, true)
check("the quiet run's leader does NOT clear it", quiet.strength.clears, false)
check("even though it still has a leader to report",
      count(quiet.strength.leader) > 0, true)
' The pairing is the argument: both populations decline, and only one means it.
check("both populations declined", planted.observation.change < 0
      and quiet.observation.change < 0, true)

' --- TIER: THE THRESHOLD IS DERIVED, NOT A CONSTANT -----------------------
' A hardcoded cut silently stops scaling with the search, which is the mistake
' the whole increment is about. Asserted at three widths.
near("the threshold is the family-wise t quantile for 20 cells",
     planted.search.width, stats.t_quantile(1 - 0.05 / (2 * 20), 18), 0.0000001)
' `alpha_requested`, not `alpha`. The rename IS the retraction: measured, the
' t threshold delivers 0.10-0.13 on revenue-like data against a requested 0.05,
' so a Finding may say what was ASKED FOR and must not imply it was DELIVERED.
check("the error rate it was REQUESTED with is recorded", planted.search.alpha_requested, 0.05)
check("and the old field, which implied delivery, is gone",
      is_unknown(planted.search["alpha"]), true)
check("the null says how its threshold was arrived at",
      planted.null.calibration.method, "t quantile")
check("  and that it is ASSUMED rather than measured",
      planted.null.calibration.assumed, true)
check("  carrying the rate actually observed under it",
      contains(planted.null.calibration.measured_null_rate, "0.130"), true)
check("along with which correction", planted.search.correction, "bonferroni")
check("and that cells are judged leave-one-out", planted.null.standardized, "leave_one_out")
check("with the degrees of freedom that implies", planted.null.df, 18)

' IT IS NOT THE EXPECTED MAXIMUM. sqrt(2 ln n) is where the largest of n draws
' lands ON AVERAGE, so half of all pure-noise populations clear it -- measured
' at 6 of 13 seeds while building this. A family-wise quantile is strictly
' larger, and this assertion is what stops a future simplification going back.
check("the threshold is well above sqrt(2 ln n), which is only the average max",
      planted.search.width > sqrt(2 * log(20)), true)

' AND IT IS NOT MONOTONE IN THE SEARCH WIDTH, which is worth pinning because
' the obvious mental model says it should be. Two effects pull opposite ways:
' with few cells the SPREAD is badly estimated and the t distribution demands
' an enormous z, while with many cells the SEARCH PENALTY grows. The threshold
' is U-shaped -- 8.86 at 4 cells, a minimum near 3.48 around 30, 3.73 at 200 --
' so there is a granularity sweet spot, and it falls out of the two corrections
' rather than being chosen. Recipe 6 measured the same thing from the other
' end: 12 cells missed a collapse that 60 cells caught.
one_dim = insight.explain_change(build(4242, 1, 0),
            { measure: "revenue", period: "period", baseline: 0, current: 1,
              dimensions: ["region"], comparison: "period_over_period",
              null: "siblings" })
check("a narrower search has fewer cells", one_dim.search.cells, 4)
near("and its threshold follows the same formula", one_dim.search.width,
     stats.t_quantile(1 - 0.05 / (2 * 4), 2), 0.0000001)
check("four cells demand a far HIGHER z, not a lower one -- the spread is"
      + " barely estimated", one_dim.search.width > planted.search.width, true)
check("  and it is a very high bar indeed", one_dim.search.width > 8, true)

' --- TIER: R2, THE SHARE REFUSAL ------------------------------------------
' A share is contributor_change / net_change and that denominator is unstable.
' Where the net is not distinguishable from zero, no share is reported --
' including in the PLANTED run, where one cell is real and the aggregate is
' not. That is the correct and uncomfortable answer.
check("the quiet run withholds shares", quiet.shares_reportable, false)
check("and says why", contains(quiet.shares_withheld_because,
      "not distinguishable from zero"), true)
check("its contributors carry no share", is_unknown(quiet.contributors[0].share), true)
check("the planted run ALSO withholds them -- one real cell does not make the"
      + " aggregate real", planted.shares_reportable, false)

' THE CONTROL. Without it a library that never reported a share would pass
' every check above. A population with a decline that is really SOMEWHERE
' -- located, not universal (R13) -- must report.
check("a real aggregate decline DOES report shares", slumped.shares_reportable, true)
check("and its contributors carry one", is_unknown(slumped.contributors[0].share), false)
total_share = 0
for each c in slumped.contributors
    total_share = total_share + c.share
next
near("the shares sum to 1 when they are reportable", total_share, 1, 0.0000001)

' --- TIER: R13, THE SECOND WAY A SHARE LIES ------------------------------
' R2 asks whether the net moved at all. R13 asks whether it moved EVERYWHERE,
' and the two are not the same question -- Recipe 3 found a population where
' the net moved enormously (t = -4.49) and the movement was an ordinary
' January in every cell at once. A share of that attributes to one cell what
' every cell did.
'
' The sign test is deliberately the assumption-light one: it says only that a
' cell is as likely to rise as to fall under the null, and nothing about the
' shape of how far -- which is less than the t test beside it already assumes.
check("a shift common to every cell withholds shares",
      common.shares_reportable, false)
check("  and says so in those terms",
      contains(common.shares_withheld_because, "common to the population"), true)
check("  counting the cells that moved together",
      common.null.common_movement.down, 20)
check("  and calling it common", common.null.common_movement.common, true)

' THE TWO REFUSALS MUST BE DISTINGUISHABLE. If one message served both, R13
' would be indistinguishable from R2 and could be deleted without a test
' noticing.
check("R2's refusal is not R13's",
      contains(quiet.shares_withheld_because, "common to the population"), false)
check("and R13's is not R2's",
      contains(common.shares_withheld_because, "not distinguishable from zero"),
      false)

' THE CONTROL, again. A decline that is really SOMEWHERE is not common, and
' must still report -- or R13 is just "refuse everything" wearing a p-value.
check("a located decline is not common movement",
      slumped.null.common_movement.common, false)

' AND THE MECHANISM, asserted as a DIFFERENCE rather than a number. A common
' multiplicative shift does not merely mislead the headline: it inflates the
' cross-sectional spread every cell is judged against, in proportion to
' itself, so it RAISES THE DETECTION FLOOR. The same planted collapse, same
' seed, clears without the shift and does not clear with it. That is why R13
' says the decomposition has not located this, rather than saying the data is
' seasonal -- which it has no way to know.
check("the planted cell clears on its own", planted.strength.clears, true)
check("  and the SAME cell no longer clears under a common shift",
      common_planted.strength.clears, false)
check("  while still being the biggest mover",
      common_planted.strength.leader[1], "Outdoor")

' --- TIER: R14, MORE THAN ONE CAUSE ---------------------------------------
' Leave-one-out removes a cell from its own reference and nothing else. Recipe
' 2 measured what that costs when several cells are broken at once, holding
' ONE cell literally constant and varying only its neighbours.
'
' THE LOAD-BEARING ASSERTION IS A DIFFERENCE, because every plausible wrong
' implementation still produces an ordinary-looking z. Under the default the
' watched cell's verdict is decided by cells it has nothing to do with; with
' the other candidates excluded from the reference it is decided by itself.
d0 = watched_z(0, 1)
d1 = watched_z(1, 1)
d2 = watched_z(2, 1)
d3 = watched_z(3, 1)
check("alone, the watched cell is far out", abs(d0) > 5, true)
check("  one unrelated cause takes a third of that away", abs(d1) < abs(d0) * 0.75, true)
check("  three take two thirds", abs(d3) < abs(d0) * 0.4, true)
check("  and the fall is monotone in how many others broke",
      abs(d0) > abs(d1) and abs(d1) > abs(d2) and abs(d2) > abs(d3), true)

t1 = watched_z(1, 2)
t2 = watched_z(2, 3)
t3 = watched_z(3, 4)
' THE CONTROL FOR ALL OF THE ABOVE. If trimming did nothing, or trimmed the
' wrong thing, these would drift with the neighbours exactly as the defaults
' do. They must not: the cell has not changed.
check("allowing for them restores the statistic to the cell",
      abs(t1 - t2) < 0.3 and abs(t2 - t3) < 0.3, true)
check("  at about the value it had when it was alone",
      abs(abs(t1) - abs(d0)) < 0.5, true)

' The default is 1 and naming it changes nothing -- the claim that this
' introduces no new assumption, stated as a test.
explicit = insight.explain_change(build(4242, 1, 0),
             { measure: "revenue", period: "period", baseline: 0, current: 1,
               dimensions: ["region", "category"],
               comparison: "period_over_period", null: "siblings",
               max_causes: 1 })
check("max_causes defaults to 1", planted.search.max_causes, 1)
check("  and saying so explicitly changes no answer",
      explicit.strength.z, planted.strength.z)

' `strength` names ONE cell, which is the reporting half of the same problem.
check("strength.clearing lists every cell that cleared",
      count(planted.strength.clearing), 1)
check("  and it is the leader when exactly one cleared",
      planted.strength.clearing[0][0], planted.strength.leader[0])
check("nothing clears in the quiet run, and clearing is empty",
      count(quiet.strength.clearing), 0)

' --- TIER: R14's refusals, each beside its nearest legal neighbour ---------
on error goto next
x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region", "category"], comparison: "period_over_period",
        null: "siblings", max_causes: 2 })
check("max_causes above 1 is refused under the t null",
      contains(error.message, "siblings_permuted"), true)
error.clear()
' THE CONTROL. Without it the refusal above is satisfied by a library that
' refuses max_causes altogether.
x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region", "category"], comparison: "period_over_period",
        null: "siblings_permuted", max_causes: 2, draws: 8 })
check("  and accepted under the permuted one", is_unknown(x), false)
if error then
    error.clear()
end if
x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region", "category"], comparison: "period_over_period",
        null: "siblings_permuted", max_causes: 0, draws: 8 })
check("max_causes below 1 is refused",
      contains(error.message, "at least 1"), true)
error.clear()
x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region"], comparison: "period_over_period",
        null: "siblings_permuted", max_causes: 3, draws: 8 })
check("and one that would leave no population to judge against is refused",
      contains(error.message, "no population left"), true)
error.clear()
on error stop

' --- TIER: R15, WHAT THIS SEARCH COULD HAVE FOUND -------------------------
' `within ordinary variation` is returned in identical words by a search that
' examined a healthy business and by one that could not have found a cell going
' to zero. A Finding must be able to tell those apart.
check("a Finding states the smallest change it could have found",
      is_unknown(planted.search.detectable.change), false)
check("  in the units of the business", planted.search.detectable.change > 0, true)
check("  and as a share of a typical cell",
      planted.search.detectable.share > 0, true)

' THE CONTROL, and the reason the field exists at all: it must be there when
' NOTHING cleared, which is exactly when a reader cannot tell an incapable
' search from a healthy business.
check("it is reported when nothing cleared", quiet.strength.clears, false)
check("  and is a number even then",
      quiet.search.detectable.share > 0, true)

' THE DIFFERENCE. One business of fixed size, cut coarse and fine: the
' threshold barely moves and the detectable share moves enormously. Asserting
' either number alone would pass on a library that computed a constant.
coarse = cut_look(20, 400, 0 - 1, 1)
fine = cut_look(400, 20, 0 - 1, 1)
check("a twentyfold wider search raises the bar by under a fifth",
      fine.search.width < coarse.search.width * 1.2, true)
check("  while the smallest detectable change goes from a fifth of a cell",
      coarse.search.detectable.share < 0.3, true)
check("  to nearly all of one", fine.search.detectable.share > 0.8, true)

' AND IT PREDICTS. Without this the figure is a formula echoing itself: it
' claims a change must be about 94% of a cell to clear, so a 50% collapse in
' the same population must NOT clear and a total one must.
half = cell_zero(cut_look(400, 20, 0, 0.5))
total = cell_zero(cut_look(400, 20, 0, 0.0001))
check("a collapse smaller than the stated bar does not clear", half.clears, false)
check("  and one larger than it does", total.clears, true)
' AND THE STATED NUMBER MUST LIE BETWEEN THEM, or the two checks above are
' facts about the verdicts with nothing tying them to the figure reported.
' (S0 is not exactly a typical cell, so this brackets rather than pins.)
check("  and the stated bar lies between the two",
      fine.search.detectable.share > 0.5 and fine.search.detectable.share < 1,
      true)

' --- TIER: contributors are ranked and complete ---------------------------
check("every cell is a contributor", count(planted.contributors), 20)
ordered = true
for i = 1 to count(planted.contributors) - 1
    if abs(planted.contributors[i - 1].change) < abs(planted.contributors[i].change) then
        ordered = false
    end if
next
check("contributors are ranked by absolute change", ordered, true)
recon = 0
for each c in planted.contributors
    recon = recon + c.change
next
near("and their changes reconstruct the total exactly", recon,
     planted.observation.change, 0.000001)

' --- TIER: R3, associations are not explanations --------------------------
withassoc = insight.explain_change(build(4242, 1, 0),
              { measure: "revenue", period: "period", baseline: 0, current: 1,
                dimensions: ["region", "category"],
                comparison: "period_over_period", null: "siblings",
                associations: ["stock"] })
check("an association is reported", count(withassoc.associations), 1)
check("with a correlation", withassoc.associations[0].measure, "stock")
check("described as movement, not explanation",
      withassoc.associations[0].relationship, "moved with")
check("and explicitly NOT an explanation", withassoc.associations[0].explains, false)

' --- TIER: provenance completeness (§9) ------------------------------------
check("provenance is complete", count(reasoning.provenance_complete(planted)), 0)
check("it names the method", planted.provenance.method, "explain_change/siblings")
check("the rows it read", planted.provenance.rows, 1200)
check("and states its assumptions rather than hiding them",
      count(planted.provenance.assumptions) > 0, true)
check("provenance is clock-free, so a finding is reproducible",
      is_unknown(planted.provenance["as_of"]), true)

' --- TIER: refusals, each beside its nearest legal neighbour ---------------
on error goto next

x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region"], comparison: "period_over_period" })
check("an undeclared null is refused", contains(error.message, "the null must be declared"), true)
error.clear()

x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region"], comparison: "vibes", null: "siblings" })
check("an undeclared comparison is refused by name",
      contains(error.message, "each answers a different"), true)
error.clear()

x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region"], comparison: "period_over_period", null: "whatever" })
check("an unknown null is refused", contains(error.message, "must be declared"), true)
error.clear()

x = insight.explain_change(build(4242, 1, 0),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["nosuch"], comparison: "period_over_period", null: "siblings" })
check("a dimension that is not a column is refused",
      contains(error.message, "no column nosuch"), true)
error.clear()

x = reasoning.finding({ subject: "s", measure: "m", observation: { },
                        search: { cells: 5, width: 2 }, null: { kind: "siblings" },
                        strength: { }, contributors: [], provenance: { },
                        materiality: "high" })
check("a Finding carrying materiality is refused, with the reason",
      contains(error.message, "objectives belong to the decision layer"), true)
error.clear()

x = reasoning.finding({ subject: "s", measure: "m", observation: { },
                        search: { cells: 5, width: 2 }, null: { kind: "siblings" },
                        strength: { }, contributors: [], provenance: { },
                        cause: "the weather" })
check("a Finding carrying a cause is refused",
      contains(error.message, "never a cause"), true)
error.clear()

x = reasoning.finding({ subject: "s", measure: "m", observation: { },
                        search: { width: 2 }, null: { kind: "siblings" },
                        strength: { }, contributors: [], provenance: { } })
check("a Finding with no search width is refused",
      contains(error.message, "cannot state it cannot be judged"), true)
error.clear()

x = reasoning.compare_confidence("confidence", 0.9, "assurance", 0.8)
check("comparing a confidence with an assurance is refused",
      contains(error.message, "share no scale"), true)
error.clear()

on error stop

' THE CONTROL for the refusals: the nearest legal neighbour of each still works.
ok_finding = reasoning.finding({ subject: "s", measure: "m", observation: { },
                                 search: { cells: 5, width: 2 },
                                 null: { kind: "siblings" }, strength: { },
                                 contributors: [], provenance: { },
                                 confidence: 0.9 })
check("a well-formed Finding is accepted", ok_finding.search.cells, 5)
check("and confidence IS allowed on one", ok_finding.confidence, 0.9)
check("comparing two of the same kind is fine",
      reasoning.compare_confidence("confidence", 0.9, "confidence", 0.8), 1)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
