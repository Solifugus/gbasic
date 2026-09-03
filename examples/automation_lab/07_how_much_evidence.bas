' Design laboratory — Recipe 9: how much evidence is enough?
'
' Recipe 5 ASSUMED a recovery of 0.6 and flagged its recommendation as
' sensitive to exactly that figure. Recipe 7 showed where such a figure comes
' from, and that measuring it without a control gives the wrong one. This is
' the third step, and the one that turns the loop: take controlled evidence,
' produce the assumption, and decide again.
'
' "How much evidence is enough" HAS NO ANSWER IN THE ABSTRACT. It has an answer
' relative to a DECISION: enough is when the interval the evidence supports no
' longer spans the point at which the recommendation changes.
program main()
  load decision
  load reasoning
  load automation
  load fake

  print "Sending a manager costs 2000 against a loss of 16835, so the action"
  print "pays for itself at a recovery of 0.119. That is the BREAK-EVEN, and"
  print "the whole question is whether the evidence can put the truth on one"
  print "side of it."
  print ""
  print "=== CASE A: the true incremental effect is 0.35, well clear of it"
  print ""
  weigh_with(2, 0.35)
  weigh_with(5, 0.35)
  weigh_with(12, 0.35)

  print "=== CASE B: the true incremental effect is 0.15, just above it"
  print ""
  weigh_with(2, 0.15)
  weigh_with(5, 0.15)
  weigh_with(12, 0.15)
  weigh_with(30, 0.15)

  print ""
  print "  ENOUGH EVIDENCE IS NOT A NUMBER OF OBSERVATIONS. In case A even TWO"
  print "  observations settle it, because the truth is nowhere near the"
  print "  break-even. In case B thirty do not, because it sits a hair above."
  print "  Sufficiency is a fact about the DISTANCE from the decision boundary,"
  print "  not about the sample size, and any rule of thumb -- \"at least"
  print "  thirty\" -- is wrong in both directions at once."
  print ""
  print "  AND NOTE CASE B AT n=5. It reports DECISIVE and case B at n=12 does"
  print "  not. That is not a bug and it is not progress reversing: it is what"
  print "  happens when you re-ask a marginal question as evidence arrives."
  print "  An interval that straddles a boundary will sometimes clear it by"
  print "  chance, and a system that acts the first time it does is choosing"
  print "  the moment the noise flattered it."
  print ""
  print "  THE WAY OUT OF AN UNDECIDABLE DECISION IS NOT MORE DATA. In case B"
  print "  the decision is marginal because the action costs nearly what it"
  print "  recovers. A cheaper action moves the break-even and settles the"
  print "  question that no amount of measurement was going to settle."
end program

function weigh_with(n, true_effect)
  load decision
  load reasoning

  ev = evidence_for(n, true_effect)
  cal = decision.calibrate(ev)

  ctx = { objectives: [{ measure: "revenue", direction: "maximize" }],
          thresholds: { revenue: 5000 },
          authority: { spend_limit: 5000 } }
  options = [{ name: "do nothing", cost: 0, benefit: 0 },
             { name: "send a manager", cost: 2000, recovers: cal.estimate }]

  d = decision.evaluate(finding_of(), ctx, options,
        { sizing: "leading_cell", calibration: cal })

  print ("  n = " + pad(string(n), 4) + "estimate " + f3(cal.estimate)
         + "   95% interval " + f3(cal.low) + " to " + f3(cal.high))
  print ("         recommendation " + pad(d.recommendation, 16)
         + "assurance " + f2(d.assurance))
  if d.assurance >= 1 then
    print "         DECISIVE: the recommendation holds across the whole"
    print "         interval the evidence supports."
  else
    print "         NOT DECISIVE: inside the interval the evidence supports,"
    print "         the recommendation changes. More evidence, or a human."
    for each s in d.sensitivities
      print ("           flips at scale " + string(round(s.at, 2))
             + ": " + s.from + " -> " + s.to)
    next
  end if
  print ""
  return nothing
end function

' Controlled observations, built through the real chain: a gated action, an
' outcome measured against a holdout, read back as evidence. R10 refuses
' anything uncontrolled, so a calibration cannot be assembled any other way.
function evidence_for(n, true_effect)
  load automation
  load reasoning
  load fake

  dec = a_decision()
  reh = automation.rehearsal({ periods: 60, fired: n, false_alarms: 0 })
  ctx = { authority: { spend_limit: 5000, min_rehearsal_periods: 12 } }
  out = []
  for i = 1 to n
    act = automation.execute(dec, ctx, reh, noop)
    ' The treated cell recovered this much; the held-back one this much.
    ' The difference is what acting was worth, and it is 0.15 plus noise.
    holdout = 0.30 + fake.between(555, i * 2, 0, 20) / 100
    treated = holdout + true_effect + (fake.between(777, i * 3, 0, 40) - 20) / 100
    obs = automation.observe(act, { value: treated, holdout: holdout,
                                    at: "2026-0" + string(1 + mod(i, 9)) + "-01" })
    append(out, reasoning.as_evidence(obs))
  next
  return out
end function

function noop(dec)
  return { ok: true }
end function

function a_decision()
  load reasoning
  return reasoning.decision({
    objective: { measure: "revenue", direction: "maximize" },
    alternatives: [], recommendation: "send a manager",
    expected_value: 500, authority_required: false,
    sized_off: { quantity: "leading_cell", established: true, value: 0 - 16835 },
    provenance: { method: "recipe 5", rows: 1, parameters: { }, assumptions: [] } })
end function

function finding_of()
  load reasoning
  return reasoning.finding({
    subject: "revenue", measure: "revenue",
    observation: { baseline: 2040764, current: 2003269, change: 0 - 37495,
                   change_pct: 0 - 0.018 },
    search: { dimensions: ["region", "store", "category"], cells: 60,
              width: 3.53, alpha: 0.05, correction: "bonferroni" },
    null: { kind: "siblings", mean: 0 - 625, sd: 4703, threshold: 3.53,
            standardized: "leave_one_out", df: 58 },
    strength: { z: 0 - 3.9, clears: true,
                leader: ["Northeast", "Northeast-2", "Outdoor"] },
    contributors: [{ path: ["Northeast", "Northeast-2", "Outdoor"],
                     change: 0 - 16835, share: unknown, z: 0 - 3.9, clears: true }],
    shares_reportable: false,
    shares_withheld_because: "the net change is not distinguishable from zero",
    provenance: { method: "recipe 1", rows: 3600, parameters: { },
                  assumptions: [] } })
end function

function f2(x)
  if is_unknown(x) then
    return "unknown"
  end if
  return string(round(x, 2))
end function

function f3(x)
  return string(round(x, 3))
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function
