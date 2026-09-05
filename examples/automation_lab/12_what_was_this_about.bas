' Design laboratory — Recipe 11: what was this evidence about?
'
' Recipe 9 turned the loop: controlled outcomes become the assumption a later
' decision rests on. It measured ONE intervention, so the question this recipe
' asks never came up -- and it is the question a real learning loop faces on
' its first day, because a real organisation runs several campaigns at once
' and stores every measured action in one place.
'
' `decision.calibrate` takes evidence and returns an interval. An effect is a
' bare number. Bare numbers average happily, and the arithmetic has no way to
' know it is being handed observations of two different things.
program main()
  load decision
  load reasoning
  load automation
  load stats

  print "A year of automation. Two campaigns, both gated, both measured"
  print "against a holdout, both read back through reasoning.as_evidence --"
  print "so R10 is satisfied and every observation here is controlled."
  print ""
  print "  send a manager   costs 2000, and truly recovers 0.35"
  print "  cut the price    costs 2000, and truly recovers 0.05"
  print ""
  print "Against a loss of 16835 an action of cost 2000 pays for itself at a"
  print "recovery of 0.119. So the first is worth doing and the second is not,"
  print "and the whole job of a calibration is to say so."
  print ""

  mgr = campaign("send a manager", "revenue", 12, 0.35, 101)
  cut = campaign("cut the price", "revenue", 12, 0.05, 202)

  print "=== PART A: each campaign calibrated from its own evidence"
  print ""
  judge("send a manager", decision.calibrate(mgr))
  judge("cut the price", decision.calibrate(cut))

  print "  Both right. This is what the library does when it is asked one"
  print "  question at a time."
  print ""
  print "=== PART B: one pool -- \"how well do our interventions work?\""
  print ""
  print "  Nothing in the shape of the data objects. Twenty-four controlled"
  print "  outcomes, all revenue, all from gated actions, all measured the"
  print "  same way. A loop that stores every measured action and calibrates"
  print "  from the store produces exactly this."
  print ""
  pooled = pool(concat(mgr, cut))
  judge("send a manager", pooled)
  judge("cut the price", pooled)

  print "  THE PRICE CUT IS NOW RECOMMENDED, and it recovers 0.05 against a"
  print "  break-even of 0.119. The estimate is not a compromise between two"
  print "  answers -- it is the answer to a question nobody asked, and it is"
  print "  wrong in opposite directions for the two campaigns that made it."
  print ""
  print "=== PART C: and more evidence does not help"
  print ""
  print "     n     estimate    95% interval          width"
  for each k in [6, 12, 24, 60]
    p = pool(concat(campaign("send a manager", "revenue", k, 0.35, 101),
                    campaign("cut the price", "revenue", k, 0.05, 202)))
    print ("  " + pad(string(p.n), 6) + f3(p.estimate) + "       "
           + f3(p.low) + " to " + f3(p.high) + "     " + f3(p.high - p.low))
  next
  print ""
  print "  The interval collapses towards the midpoint of two truths, which is"
  print "  the effect of nothing. THE STANDARD ERROR FALLS WITH THE SIZE OF THE"
  print "  POOL, so the more incommensurable evidence is gathered the more"
  print "  confident the wrong number becomes -- and by n=120 the interval no"
  print "  longer contains either truth at all:"
  print ""
  big = pool(concat(campaign("send a manager", "revenue", 60, 0.35, 101),
                    campaign("cut the price", "revenue", 60, 0.05, 202)))
  judge("cut the price", big)
  print "  DECISIVE, from 120 controlled observations, about an intervention"
  print "  that loses money every time it is taken. That is the shape recipe 9"
  print "  taught us to trust -- a narrow interval well clear of the break-even"
  print "  -- and here it is being produced by a defect."
  print ""
  print "=== PART D: what the evidence can be made to say"
  print ""

  on error goto next
  x = decision.calibrate(concat(mgr, cut))
  if error then
    print "  pooling two interventions:"
    print ("    " + error.message)
    error.clear()
  end if
  x = decision.calibrate(concat(mgr, campaign("send a manager", "days_to_pay",
                                              12, 0.35, 303)))
  if error then
    print ""
    print "  pooling two measures:"
    print ("    " + error.message)
    error.clear()
  end if
  on error stop

  print ""
  print "  AND THE CONTROL, because a rule that refuses everything is not a"
  print "  rule. The same intervention on the same measure in DIFFERENT PLACES"
  print "  is exactly what a calibration is for, and it still pools:"
  both = concat(campaign("send a manager", "revenue", 6, 0.35, 101),
                campaign("send a manager", "revenue", 6, 0.35, 999))
  c = decision.calibrate(both)
  print ("    " + string(c.n) + " outcomes from two regions, estimate "
         + f3(c.estimate) + ", about " + c.about.intervention
         + " on " + c.about.measure)
  print ""
  print "  WHAT MADE THE REFUSAL POSSIBLE WAS NOT A NEW MEASUREMENT. It was"
  print "  the chain: a Decision now names the Finding it came from, an Action"
  print "  carries the Decision and the policy that permitted it, and evidence"
  print "  read back off an Action can therefore say what it was about. §9"
  print "  had asked for that from the start and called it \"not a logging"
  print "  feature\" -- this is what it was for."
end program

' --- a campaign, run through the real chain --------------------------------
'
' A gated action per period, an outcome measured against a holdout, read back
' as evidence. Nothing here is hand-built: R10 refuses anything uncontrolled
' and R16 needs the Action to know what it was about, so both properties come
' from the pipeline rather than from the fixture asserting them.
function campaign(intervention, measure, n, true_effect, seed)
  load automation
  load reasoning
  load fake

  dec = a_decision(intervention, measure)
  reh = automation.rehearsal({ periods: 60, fired: n, false_alarms: 0 })
  ctx = { objectives: [{ measure: measure, direction: "maximize" }],
          thresholds: { }, authority: { spend_limit: 5000,
                                        min_rehearsal_periods: 12 } }
  out = []
  for i = 1 to n
    act = automation.execute(dec, ctx, reh, noop)
    holdout = 0.30 + fake.between(seed, i * 2, 0, 20) / 100
    treated = holdout + true_effect + (fake.between(seed + 1, i, 0, 40) - 20) / 100
    obs = automation.observe(act, { value: treated, holdout: holdout,
                                    at: "2026-0" + string(1 + mod(i, 9)) + "-01" })
    append(out, reasoning.as_evidence(obs))
  next
  return out
end function

function noop(dec)
  return { ok: true }
end function

' The pooled calibration, computed here rather than by decision.calibrate,
' because after R16 the library will not compute it. This is the number a
' learning loop got before the refusal existed -- shown so that the refusal
' can be judged against what it costs.
function pool(evidences)
  load stats
  effects = []
  for each e in evidences
    append(effects, e.effect)
  next
  n = count(effects)
  m = mean(effects)
  se = stdev(effects) / sqrt(n)
  crit = stats.t_quantile(0.975, n - 1)
  return { n: n, estimate: m, low: m - crit * se, high: m + crit * se,
           level: 0.95 }
end function

function judge(intervention, cal)
  load decision

  ctx = { objectives: [{ measure: "revenue", direction: "maximize" }],
          thresholds: { revenue: 5000 },
          authority: { spend_limit: 5000 } }
  options = [{ name: "do nothing", cost: 0, benefit: 0 },
             { name: intervention, cost: 2000, recovers: cal.estimate }]
  d = decision.evaluate(finding_of("revenue"), ctx, options,
                        { sizing: "leading_cell", calibration: cal })
  print ("  " + pad(intervention, 18) + "estimate " + f3(cal.estimate)
         + "   interval " + f3(cal.low) + " to " + f3(cal.high))
  verdict = "DO NOT ACT"
  if d.recommendation != "do nothing" then
    verdict = "ACT"
  end if
  print ("  " + pad("", 18) + "recommendation " + pad(d.recommendation, 16)
         + verdict + "   assurance " + f2(d.assurance))
  print ""
  return nothing
end function

function a_decision(intervention, measure)
  load reasoning
  return reasoning.decision({
    objective: { measure: measure, direction: "maximize" },
    alternatives: [], recommendation: intervention,
    expected_value: 500, authority_required: false,
    sized_off: { quantity: "leading_cell", established: true, value: 0 - 16835 },
    finding: finding_of(measure),
    provenance: { method: "recipe 11", rows: 1, parameters: { },
                  assumptions: [] } })
end function

function finding_of(measure)
  load reasoning
  return reasoning.finding({
    subject: "Northeast/Outdoor", measure: measure,
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

function concat(a, b)
  out = []
  for each x in a
    append(out, x)
  next
  for each x in b
    append(out, x)
  next
  return out
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
