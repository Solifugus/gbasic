' decision.evaluate (docs/automation_recipe_05_what_to_do.md, design §5).
'
' SELF-CHECKING AND FORCED, like the insight fixture and for a sharper reason:
' every defect here produces a DEFENSIBLE-LOOKING RECOMMENDATION -- an option,
' an expected value, a rationale. A golden would record "restock and promote"
' as expected and defend it, whether or not the number underneath it was ever
' established.
'
' THE LOAD-BEARING TIER IS R9. Recipe 5 measured the same intervention over the
' same data giving +7,497 sized off the aggregate and -4,899 sized off the cell
' that cleared -- ACT against DO NOT ACT -- with the Finding having already
' said the aggregate was not established.

load decision
load insight
load reasoning
load frame
load fake

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

' --- two real findings from the insight layer ------------------------------
' `spotty`  -- one cell really collapsed; the AGGREGATE is not established.
' `broad`   -- every cell declined; the aggregate IS established.

function build(seed, plant, slump)
    load fake
    load frame
    rows = []
    i = 0
    for each rg in ["North", "South", "East", "West"]
        for each c in ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
            for d = 1 to 30
                for p = 0 to 1
                    i = i + 1
                    amt = fake.lognormal(seed, i, 1000, 0.5)
                    if p = 1 then
                        if plant = 1 and rg = "North" and c = "Outdoor" then
                            amt = amt * 0.45
                        end if
                        if slump = 1 then
                            amt = amt * 0.6
                        end if
                    end if
                    append(rows, { region: rg, category: c, period: p, revenue: amt })
                next
            next
        next
    next
    return frame.from_rows(rows)
end function

function look(seed, plant, slump)
    load insight
    return insight.explain_change(build(seed, plant, slump),
             { measure: "revenue", period: "period", baseline: 0, current: 1,
               dimensions: ["region", "category"],
               comparison: "period_over_period", null: "siblings" })
end function

spotty = look(4242, 1, 0)
broad = look(31, 0, 1)

check("the spotty finding has a cell that cleared", spotty.strength.clears, true)
check("but its aggregate is NOT established", spotty.shares_reportable, false)
check("the broad finding's aggregate IS established", broad.shares_reportable, true)

ctx = { objectives: [{ measure: "revenue", direction: "maximize" }],
        thresholds: { revenue: 5000 },
        authority: { spend_limit: 5000 } }

options = [{ name: "do nothing", cost: 0, benefit: 0 },
           { name: "send a manager", cost: 2000, recovers: 0.15 },
           { name: "restock and promote", cost: 15000, recovers: 0.6 }]

' --- TIER: R9, THE LOAD-BEARING ONE ---------------------------------------
' Sizing off a quantity the finding declined to establish is refused AT THE
' BOUNDARY, not reported and left for the reader to notice.
on error goto next
x = decision.evaluate(spotty, ctx, options, { sizing: "aggregate",
                                              sensitivity_range: [0, 2] })
check("sizing off an unestablished aggregate is refused",
      contains(error.message, "which\n this finding did not establish")
      or contains(error.message, "did not establish"), true)
check("and the refusal repeats the finding's own reason",
      contains(error.message, "not distinguishable from zero"), true)
error.clear()
on error stop

' THE CONTROL, and it is what stops R9 being "refuse everything": the SAME
' finding, sized off the cell that DID clear, is accepted.
d = decision.evaluate(spotty, ctx, options, { sizing: "leading_cell",
                                              sensitivity_range: [0, 2] })
check("the same finding sized off the cell that cleared is accepted",
      d.sized_off.quantity, "leading_cell")
check("and the decision records that it was established", d.sized_off.established, true)

' And the other control: where the aggregate IS established, it may be used.
db = decision.evaluate(broad, ctx, options, { sizing: "aggregate",
                                              sensitivity_range: [0, 2] })
check("an established aggregate may be sized off", db.sized_off.quantity, "aggregate")

' --- TIER: R6, AUTHORITY IS STATED AND NOT ENFORCED -----------------------
' The recommendation is the best alternative, NOT the best affordable one. A
' layer that quietly returned the affordable one would hide the only choice a
' human needs to make -- and every check above would still pass.
check("the recommendation is the best option overall",
      db.recommendation, "restock and promote")
check("even though it exceeds the spend limit", db.authority_required, true)
check("and the reason names the limit",
      contains(db.authority_reason, "exceeds the spend limit of 5000"), true)
affordable = ""
for each a in db.alternatives
    if a.within_authority and a.name != "do nothing" then
        affordable = a.name
    end if
next
check("a cheaper affordable option existed and was NOT chosen",
      affordable != "" and affordable != db.recommendation, true)
check("every alternative is scored, not just the affordable ones",
      count(db.alternatives), 3)

' --- TIER: materiality is computed HERE, from the context (§4.6) -----------
check("materiality is decided against the declared threshold",
      db.materiality.threshold, 5000)
check("and this change is material", db.materiality.is_material, true)
' A missing threshold gives `unknown`, NEVER false. "We have no threshold" and
' "this does not matter" are different statements and only one of them is true.
nothresh = decision.evaluate(broad, { objectives: ctx.objectives,
                                      authority: { spend_limit: 5000 } },
                             options, { sizing: "aggregate", sensitivity_range: [0, 2] })
check("with no threshold declared, materiality is unknown",
      is_unknown(nothresh.materiality.is_material), true)
check("and it is not false", nothresh.materiality.is_material = false, false)
check("and it says why", contains(nothresh.materiality.why, "no threshold declared"), true)

' --- TIER: assurance is a SENSITIVITY, not a feeling -----------------------
' The charter's own example of a useful decision was "the recommendation is
' highly sensitive to the assumed response". So assurance is the share of a
' DECLARED range over which the recommendation survives, and the crossing is
' named.
check("assurance is reported", is_unknown(db.assurance), false)
check("it is a share of the swept range, so at most 1", db.assurance <= 1, true)
check("the recommendation does not survive the whole range", db.assurance < 1, true)
check("and the crossing is named", count(db.sensitivities) > 0, true)
check("with the option it flips to", is_unknown(db.sensitivities[0].to), false)

' THE ASSERTION THAT WOULD HAVE CAUGHT THE FIRST SWEEP. `assurance < 1` and
' `sensitivities` non-empty are both satisfied by a sweep that never picks the
' recommendation at all -- which is what the first version did, reporting
' assurance 0 while every check here passed. Two things fix that. The sweep
' must AGREE WITH THE POINT ESTIMATE at the nominal assumption, which the
' library now enforces and raises on; and assurance must be a DIFFERENCE
' between a robust recommendation and a marginal one rather than merely a
' number in range.
check("a robust recommendation is assured over most of the range",
      db.assurance > 0.9, true)
check("and its flip is below the nominal assumption, so it survives there",
      db.sensitivities[0].at < 1, true)
marginal = decision.evaluate(spotty, ctx, options,
             { sizing: "leading_cell", sensitivity_range: [0, 2] })
check("a marginal recommendation is much less assured", marginal.assurance < 0.6, true)
check("so assurance distinguishes them", db.assurance > marginal.assurance + 0.3, true)
' A Decision carries `assurance`, never `confidence` -- they answer different
' questions and shared one word in the charter.
check("a Decision carries no confidence", is_unknown(db["confidence"]), true)

' --- TIER: refusals, each beside its nearest legal neighbour ---------------
on error goto next

x = decision.evaluate(spotty, ctx, options, { sensitivity_range: [0, 2] })
check("an undeclared sizing is refused",
      contains(error.message, "pick opposite actions"), true)
error.clear()

x = decision.evaluate(spotty, ctx, options, { sizing: "leading_cell" })
check("a sized decision with no recovery range is refused",
      contains(error.message, "Option A is best"), true)
error.clear()

x = decision.evaluate(spotty, { objectives: ctx.objectives }, options,
                      { sizing: "leading_cell", sensitivity_range: [0, 2] })
check("a context with no authority is refused, because unset means NOTHING may run",
      contains(error.message, "NOTHING may"), true)
error.clear()

x = reasoning.decision({ objective: { }, alternatives: [], recommendation: "a",
                         expected_value: 1, authority_required: false,
                         sized_off: { quantity: "aggregate", established: true },
                         provenance: { }, confidence: 0.9 })
check("a Decision carrying `confidence` is refused, with the reason",
      contains(error.message, "A Decision carries `assurance`"), true)
error.clear()

x = reasoning.decision({ objective: { }, alternatives: [], recommendation: "a",
                         expected_value: 1, authority_required: false,
                         sized_off: { quantity: "aggregate", established: true },
                         provenance: { }, authorized: true })
check("a Decision that decides its own permission is refused",
      contains(error.message, "enforcement happens at"), true)
error.clear()

x = reasoning.decision({ objective: { }, alternatives: [], recommendation: "a",
                         expected_value: 1, authority_required: false,
                         sized_off: { quantity: "aggregate", established: false },
                         provenance: { } })
check("a Decision sized off an unestablished quantity is refused even if built by hand",
      contains(error.message, "declined to establish"), true)
error.clear()

on error stop

' CONTROL for the hand-built refusals.
ok_d = reasoning.decision({ objective: { }, alternatives: [], recommendation: "a",
                            expected_value: 1, authority_required: false,
                            sized_off: { quantity: "aggregate", established: true },
                            provenance: { }, assurance: 0.8 })
check("a well-formed Decision is accepted", ok_d.recommendation, "a")
check("and assurance IS allowed on one", ok_d.assurance, 0.8)

' --- TIER: provenance ------------------------------------------------------
check("the decision records how it was made",
      db.provenance.method, "evaluate/expected_value")
check("and what it swept", db.provenance.parameters.sizing, "aggregate")
check("and states its assumptions", count(db.provenance.assumptions) > 0, true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
