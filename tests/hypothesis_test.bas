' insight.weigh + reasoning.hypothesis (recipe 8, design §4's causal ladder, R3).
'
' SELF-CHECKING AND FORCED. Every defect here produces a RANKED LIST OF
' PLAUSIBLE EXPLANATIONS -- names, numbers, an order. A golden would record
' whichever story came top as expected and defend it, which is precisely the
' failure this layer exists to prevent.
'
' THE LOAD-BEARING TIER IS R11: two hypotheses that predict the SAME CELLS are
' not separated by this data, and ordering them would invent a preference the
' evidence does not support. Its control is a set of hypotheses that DO predict
' differently, or "refuse to rank" would be satisfied by refusing to rank
' anything.

load insight
load reasoning

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

' --- a finding built by hand ------------------------------------------------
' Twelve cells across two dimensions. Exactly one cleared: North/Outdoor.
' Hand-built rather than produced by explain_change because these tiers test
' `weigh`, and a fixture that has to tune a planted effect to make a cell clear
' is testing two things at once.

function cell(rg, cat, chg, clears)
    return { path: [rg, cat], change: chg, share: unknown,
             z: chg / 1000, clears: clears }
end function

contributors = []
for each rg in ["North", "South", "East"]
    for each c in ["Outdoor", "Apparel", "Home", "Grocery"]
        hit = rg = "North" and c = "Outdoor"
        amount = 0 - 500
        if hit then
            amount = 0 - 9000
        end if
        append(contributors, cell(rg, c, amount, hit))
    next
next

finding = reasoning.finding({
    subject: "revenue", measure: "revenue",
    observation: { baseline: 100000, current: 88000, change: 0 - 12000,
                   change_pct: 0 - 0.12 },
    search: { dimensions: ["region", "category"], cells: 12, width: 3.7,
              alpha: 0.05, correction: "bonferroni" },
    null: { kind: "siblings", mean: 0 - 1000, sd: 2000, threshold: 3.7,
            standardized: "leave_one_out", df: 10 },
    strength: { z: 0 - 9, clears: true, leader: ["North", "Outdoor"] },
    contributors: contributors,
    provenance: { method: "handmade", rows: 12, parameters: { }, assumptions: [] } })

function h(name, predicts, disc)
    return reasoning.hypothesis({ name: name, predicts: predicts,
                                  discriminator: disc })
end function

' --- TIER: parsimony falls out of the comparison ---------------------------
' Not imposed. A hypothesis predicting twelve cells and explaining one has
' over-predicted eleven, and set agreement says so on its own.
broad = [h("everything", { }, "d1"),
         h("the whole North region", { region: "North" }, "d2"),
         h("Outdoor everywhere", { category: "Outdoor" }, "d3"),
         h("Outdoor in North", { region: "North", category: "Outdoor" }, "d4")]
w = insight.weigh(finding, broad)
check("one cell cleared", w.affected_cells, 1)
check("the narrowest consistent hypothesis leads", w.leader, "Outdoor in North")
check("and its agreement is total", w.hypotheses[0].agreement, 1)
check("the broadest is last", w.hypotheses[3].name, "everything")
check("  having predicted every cell", w.hypotheses[3].predicted, 12)
check("  hit the one", w.hypotheses[3].hit, 1)
check("  and over-predicted the rest", w.hypotheses[3].over_predicted, 11)
check("the counts reconstruct", w.hypotheses[3].hit + w.hypotheses[3].over_predicted,
      w.hypotheses[3].predicted)
check("nothing is left unexplained by the leader", w.hypotheses[0].unexplained, 0)

' NO PROBABILITY IS CLAIMED. The charter showed "confidence .91" for a cause;
' nothing in this data supports a probability that a hypothesis is TRUE.
check("agreement says what it is", contains(w.agreement_is, "NOT a probability"), true)
check("and every hypothesis still says it explains nothing",
      w.hypotheses[0].explains, false)

' --- TIER: R11, the one that matters --------------------------------------
rivals = [h("a stock-out at North/Outdoor",
            { region: "North", category: "Outdoor" },
            "on-hand inventory for that line"),
          h("a competitor opened near North selling outdoor goods",
            { region: "North", category: "Outdoor" },
            "footfall and competitor pricing"),
          h("the whole North region", { region: "North" }, "the other categories")]
r = insight.weigh(finding, rivals)
check("the two identical predictions are reported as indistinguishable",
      count(r.indistinguishable), 1)
check("  naming both", r.indistinguishable[0].a != r.indistinguishable[0].b, true)
check("  and both discriminators", count(r.indistinguishable[0].separate_them_by), 2)
check("the leader is flagged NOT separable", r.leader_is_separable, false)
check("and the next test says the data cannot decide",
      contains(r.next_test, "cannot separate them"), true)
check("  offering both observations",
      contains(r.next_test, "on-hand inventory") and contains(r.next_test, "footfall"), true)

' THE CONTROL. Without it, "refuses to rank" is satisfied by a library that
' refuses to rank anything. Hypotheses that predict DIFFERENTLY are separable
' and the next test is the leader's own discriminator.
distinct = [h("Outdoor in North", { region: "North", category: "Outdoor" },
              "on-hand inventory for that line"),
            h("the whole North region", { region: "North" }, "the other categories"),
            h("Outdoor everywhere", { category: "Outdoor" }, "the other regions")]
d = insight.weigh(finding, distinct)
check("distinct predictions are NOT reported as indistinguishable",
      count(d.indistinguishable), 0)
check("the leader is separable", d.leader_is_separable, true)
check("and the next test names its own discriminator",
      contains(d.next_test, "on-hand inventory for that line"), true)
check("  as a confirmation rather than a tie-break",
      contains(d.next_test, "confirm the leader"), true)

' --- TIER: refusals, each beside its nearest legal neighbour ---------------
on error goto next

x = reasoning.hypothesis({ name: "a story", predicts: { region: "North" } })
check("a hypothesis with no discriminator is refused",
      contains(error.message, "is a story"), true)
error.clear()

x = reasoning.hypothesis({ name: "vague", discriminator: "something" })
check("a hypothesis that predicts nothing is refused",
      contains(error.message, "needs a predicts"), true)
error.clear()

x = insight.weigh(finding, [h("bad axis", { supplier: "acme" }, "d")])
check("predicting on a dimension the finding never searched is refused",
      contains(error.message, "not one of the dimensions"), true)
error.clear()

quiet_contributors = []
for each rg in ["North", "South"]
    append(quiet_contributors, cell(rg, "Outdoor", 0 - 100, false))
next
quiet = reasoning.finding({
    subject: "revenue", measure: "revenue",
    observation: { baseline: 100, current: 99, change: 0 - 1, change_pct: 0 - 0.01 },
    search: { dimensions: ["region", "category"], cells: 2, width: 9 },
    null: { kind: "siblings", mean: 0, sd: 1, threshold: 9 },
    strength: { z: 0 - 1, clears: false, leader: ["North", "Outdoor"] },
    contributors: quiet_contributors,
    provenance: { method: "handmade", rows: 2, parameters: { }, assumptions: [] } })
x = insight.weigh(quiet, broad)
check("weighing against a finding where NOTHING cleared is refused",
      contains(error.message, "rank stories by how little they claim"), true)
error.clear()

x = insight.weigh(finding, [{ name: "raw record", predicts: { } }])
check("a hypothesis not built by reasoning.hypothesis is refused",
      contains(error.message, "must be built with"), true)
error.clear()

on error stop

' CONTROL for the refusals.
ok_h = reasoning.hypothesis({ name: "fine", predicts: { region: "North" },
                              discriminator: "the other regions" })
check("a well-formed hypothesis is accepted", ok_h.name, "fine")
check("and is born explaining nothing", ok_h.explains, false)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
