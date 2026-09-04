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
                        if slump = 1 then
                            amt = amt * 0.6
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
' every check above. A population where every cell really did fall must report.
check("a real aggregate decline DOES report shares", slumped.shares_reportable, true)
check("and its contributors carry one", is_unknown(slumped.contributors[0].share), false)
total_share = 0
for each c in slumped.contributors
    total_share = total_share + c.share
next
near("the shares sum to 1 when they are reportable", total_share, 1, 0.0000001)

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
