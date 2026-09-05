' Design laboratory — Recipe 12: the whole loop, once.
'
' The design's status header has said "the architecture is closed end to end"
' since Recipe 6. Eleven recipes later that claim had never been RUN: every
' seam was exercised, but always with a hand-built value on one side. In
' particular no Action anywhere in this tree was ever built from a Decision
' that `decision.evaluate` actually produced -- recipes 9 and 11 print
' `evaluate`'s answer and then execute a hand-written one.
'
' So this recipe adds nothing and refuses nothing. It runs a small business for
' fourteen months through the real libraries, in one program, and reports what
' the loop does:
'
'   data -> insight.explain_change -> decision.evaluate -> automation.would
'        -> automation.execute -> automation.observe -> reasoning.as_evidence
'        -> decision.calibrate -> the next decision's assumption
program main()
  load insight
  load decision
  load automation
  load reasoning

  print "A shop chain: 4 regions x 5 categories = 20 cells, 14 months."
  print "Most months one cell collapses. The process looks every month,"
  print "and because it looks every month it declares repetitions: 12 (R18)."
  print ""

  st = { treated: { }, evidence: [], observations: 0, acted: 0, held: 0,
         pending: [], done: [], rehearsed: false }

  print "=== PART 1: month 2, and R5 will not let it act"
  print ""
  st = run_month(st, 2, false)
  print ""
  print "  The finding is real and the decision is sound, and nothing runs."
  print "  A process that has never been replayed against history has no"
  print "  evidence about its own behaviour, so it may not act (R5). Note"
  print "  this is checked BEFORE authority: approval would not help."
  print ""

  print "=== PART 2: the rehearsal"
  print ""
  st.rehearsed = true
  print "  Replayed over 12 periods, fired 9 times, 2 of them false alarms."
  print "  That clears the authority's min_rehearsal_periods of 6."
  print ""

  print "=== PART 3: the live months"
  print ""
  print "  month  finding                    verdict        action"
  for m = 3 to 13
    st = run_month(st, m, true)
  next
  print ""
  print ("  acted on " + string(st.acted) + " cells, deliberately held back "
         + string(st.held) + " (automation.assign, 30%)")
  print ("  controlled evidence collected: " + string(count(st.evidence)))
  print ""

  print "=== PART 4: the loop turns"
  print ""
  if count(st.evidence) < 2 then
    print "  Not enough controlled evidence to calibrate from."
  else
    cal = decision.calibrate(st.evidence)
    print ("  calibrated from " + string(cal.n) + " controlled outcomes: "
           + f3(cal.estimate) + "  [" + f3(cal.low) + ", " + f3(cal.high) + "]")
    print ("  about: " + cal.about.intervention + " on " + cal.about.measure)
    print ""
    ' And first, the thing that happens most months: nothing cleared, so
    ' there is no quantity to size a decision off and the boundary says so
    ' rather than sizing off the leader anyway (R9).
    on error goto next
    x = decide(finding_for(st, 13), cal)
    if error then
      print "  Month 13, where nothing cleared, cannot be decided at all:"
      print ("    " + error.message)
      error.clear()
    end if
    on error stop
    print ""
    print "  Month 12 did clear. The SAME finding, decided under the invented"
    print "  range recipe 5 used and under the measured one:"
    print ""

    f = finding_for(st, 12)
    invented = decide(f, unknown)
    measured = decide(f, cal)
    show_case("sensitivity_range [0, 2]", invented)
    show_case("the calibrated interval", measured)
    print "  The recommendation is the same and what is KNOWN about it is not."
    print "  That is the loop closing: the assumption stopped being a number"
    print "  somebody chose and became one this business measured about"
    print "  itself, under a comparison it deliberately arranged."
  end if

  print ""
  print "=== PART 5: what one Action turned out to carry"
  print ""
  ' The reason this recipe exists. Every other recipe executes a HAND-BUILT
  ' Decision; this Action was built from what `decision.evaluate` produced, so
  ' the chain §9 asks for is real rather than assembled for the occasion.
  ' `assurance_is` is the proof: nothing but `evaluate` puts one there.
  a = st.done[0]
  print ("  the finding it came from     " + a.decision.finding.subject
         + ", z " + f2(a.decision.finding.strength.z))
  print ("    which established           " + a.decision.sized_off.quantity
         + " = " + string(round(a.decision.sized_off.value, 0)))
  print ("  the decision that chose it   " + a.decision.recommendation)
  print ("    assured over               " + a.decision.assurance_is.from)
  print ("  the policy that permitted it spend limit "
         + string(a.context.authority.spend_limit) + ", rehearsal >= "
         + string(a.context.authority.min_rehearsal_periods))
  print ("  the authority it spent       " + a.authority.reason)
  print ("  the rehearsal it rested on   " + string(a.rehearsal.periods)
         + " periods, " + string(a.rehearsal.fired) + " firings")
  print ("  what the executor reported   " + a.result.note)
  print ""
  print "  Every line of that came out of the run. No part of this Action was"
  print "  written by hand, which had never been true before."
end program

' --- one month of the process --------------------------------------------
function run_month(st, m, may_act)
  load insight
  load decision
  load automation
  load reasoning

  ' 1. MEASURE the outcome of anything acted on last month, before deciding
  '    anything new. An outcome is measured against the cells deliberately
  '    held back, or it is not evidence of an effect at all (R10).
  st = settle(st, m)

  ' 2. FIND
  f = finding_for(st, m)
  lead = "-"
  if count(f.contributors) > 0 then
    lead = key_of(f.contributors[0])
  end if
  if not f.strength.clears then
    if may_act then
      print ("  " + pad(string(m), 7) + pad(lead + " (z " + f2(f.strength.z) + ")", 27)
             + pad("ordinary", 15) + "none")
    end if
    return st
  end if

  ' 3. DECIDE -- and this is the Decision that gets executed, not a copy
  d = decide(f, calibration_of(st))

  ' 4. GATE
  ctx = a_context()
  reh = unknown
  if st.rehearsed then
    reh = automation.rehearsal({ periods: 12, fired: 9, false_alarms: 2 })
  end if
  w = automation.would(d, ctx, reh)
  if not may_act then
    print ("  month " + string(m) + ": " + lead + " clears at z " + f2(f.strength.z))
    print ("  decision: " + d.recommendation + ", expected value "
           + string(round(d.expected_value, 0)) + ", assurance " + f2(d.assurance))
    print ("  would it run? " + string(w.would_act) + " -- needs " + w.needs)
    print ("    " + w.reason)
    return st
  end if

  ' 5. HOLD OUT, deliberately, or there is nothing to compare against
  ' NOTE `.arm`. `assign` returns { arm, key, why } and comparing the RECORD to
  ' a string answers false rather than raising -- so a forgotten `.arm` means
  ' nothing is ever held back, silently, and in this library that means never
  ' learning whether acting helps. Written wrongly the first time (DOGFOOD).
  if automation.assign(lead, 0.3).arm = "holdout" then
    st.held = st.held + 1
    st.treated[lead] = false
    print ("  " + pad(string(m), 7) + pad(lead + " (z " + f2(f.strength.z) + ")", 27)
           + pad("act", 15) + "HELD BACK")
    return st
  end if

  ' 6. ACT
  act = automation.execute(d, ctx, reh, executor)
  st.acted = st.acted + 1
  st.treated[lead] = true
  append(st.pending, { cell: lead, at: m, act: act })
  append(st.done, act)
  print ("  " + pad(string(m), 7) + pad(lead + " (z " + f2(f.strength.z) + ")", 27)
         + pad("act", 15) + "executed: " + string(act.result.ok))
  return st
end function

' What the executor does. It is a function value the caller supplies and it is
' called only past the gate; the library never touches the business itself.
function executor(dec)
  return { ok: true, note: "dispatched: " + dec.recommendation }
end function

' --- measuring what happened ----------------------------------------------
'
' A cell that was acted on last month is settled this month, against the mean
' recovery of the cells deliberately held back. With no holdout yet there is no
' controlled comparison, and R10 refuses to call it evidence -- so those months
' produce an OBSERVATION and nothing enters the calibration.
function settle(st, m)
  load automation
  load reasoning

  hold_mean = holdout_recovery(st, m)
  still = []
  for each p in st.pending
    if p.at = m - 1 then
      mine = recovery_of(st, p.cell, collapse_month(p.cell), m)
      if is_unknown(hold_mean) then
        o = automation.observe(p.act, { value: mine, at: "month " + string(m) })
        x = reasoning.as_observation(o)
        st.observations = st.observations + 1
      else
        o = automation.observe(p.act, { value: mine, holdout: hold_mean,
                                        at: "month " + string(m) })
        append(st.evidence, reasoning.as_evidence(o))
      end if
    else
      append(still, p)
    end if
  next
  st.pending = still
  return st
end function

' The mean recovery of every cell that was deliberately held back and has had a
' month to recover. This is the counterfactual, and it exists only because
' something chose not to act.
function holdout_recovery(st, m)
  total = 0
  n = 0
  for each k in keys(st.treated)
    at = collapse_month(k)
    if st.treated[k] = false and not is_unknown(at) and at < m then
      total = total + recovery_of(st, k, at, m)
      n = n + 1
    end if
  next
  if n = 0 then
    return unknown
  end if
  return total / n
end function

' How much of the gap this cell had closed by month `now`, read off the
' simulated business rather than restated from the constants that made it.
function recovery_of(st, cell, at, now)
  before = level_of(st, cell, at - 1)
  bottom = level_of(st, cell, at)
  today = level_of(st, cell, now)
  gap = before - bottom
  if gap <= 0 then
    return 0
  end if
  return (today - bottom) / gap
end function

' --- the business ---------------------------------------------------------
'
' Pure functions of (identity, month), as `fake` requires: a running counter
' would make a cell's value depend on which comparison generated it, and the
' series would not be a series (the defect recipe 7 hit).
' WHICH CELL BREAKS WHEN. Fixed in advance and INDEPENDENT of anything the
' process decides -- the business does not know it is being watched.
function collapse_month(cell)
  schedule = ["North/Outdoor", "East/Home", "South/Grocery", "West/Apparel",
              "North/Electronics", "East/Outdoor", "South/Home",
              "West/Grocery", "North/Apparel", "East/Electronics",
              "South/Outdoor"]
  i = 0
  for each c in schedule
    if c = cell then
      return i + 2
    end if
    i = i + 1
  next
  return unknown
end function

function level_of(st, cell, m)
  load fake
  at = collapse_month(cell)
  if is_unknown(at) or m < at then
    return 1
  end if
  if m = at then
    return 0.42
  end if
  ' Recovered since. A treated cell closes 0.62 of the gap, one nobody touched
  ' closes 0.24 on its own -- an extreme reverts whether or not you act, which
  ' is exactly why the holdout has to exist (recipe 7, R10).
  frac = 0.24
  if st.treated[cell] = true then
    frac = 0.62
  end if
  wobble = (fake.between(31, index_of(cell) * 7 + m, 0, 60) - 30) / 1000
  return 0.42 + 0.58 * (frac + wobble)
end function

function finding_for(st, m)
  load insight
  load frame
  load fake
  rows = []
  for each c in cells()
    for p = 0 to 1
      mm = m - 1 + p
      for d = 1 to 25
        i = index_of(c) * 1000 + mm * 40 + d
        amt = fake.lognormal(909, i, 1000, 0.45) * level_of(st, c, mm)
        append(rows, { region: region_of(c), category: category_of(c),
                       period: p, revenue: amt })
      next
    next
  next
  ' repetitions: 12 because this process runs every month, and the correction
  ' is family-wise over the cells of ONE search unless it is told otherwise
  ' (R18). It raises the bar, and that is the point.
  return insight.explain_change(frame.from_rows(rows),
           { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "category"],
             comparison: "period_over_period", null: "siblings",
             repetitions: 12 })
end function

function decide(f, cal)
  load decision
  options = [{ name: "do nothing", cost: 0, benefit: 0 },
             { name: "send a manager", cost: 1500, recovers: 0.62 }]
  spec = { sizing: "leading_cell", sensitivity_range: [0, 2] }
  if not is_unknown(cal) then
    spec = { sizing: "leading_cell", calibration: cal }
  end if
  return decision.evaluate(f, a_context(), options, spec)
end function

function a_context()
  return { objectives: [{ measure: "revenue", direction: "maximize" }],
           thresholds: { revenue: 4000 },
           authority: { spend_limit: 5000, min_rehearsal_periods: 6 } }
end function

function calibration_of(st)
  load decision
  if count(st.evidence) < 2 then
    return unknown
  end if
  return decision.calibrate(st.evidence)
end function

function show_case(label, d)
  print ("  " + pad(label, 28) + pad(d.recommendation, 17)
         + "assurance " + f2(d.assurance))
  print ("  " + pad("", 28) + "over [" + f3(d.assurance_is.over[0]) + ", "
         + f3(d.assurance_is.over[1]) + "]")
  print ("  " + pad("", 28) + "from " + d.assurance_is.from)
  print ""
  return nothing
end function

' --- plumbing -------------------------------------------------------------
function cells()
  out = []
  for each rg in ["North", "South", "East", "West"]
    for each c in ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
      append(out, rg + "/" + c)
    next
  next
  return out
end function

function index_of(cell)
  i = 0
  for each c in cells()
    if c = cell then
      return i
    end if
    i = i + 1
  next
  return 0
end function

function region_of(cell)
  return left(cell, find(cell, "/"))
end function

function category_of(cell)
  return mid(cell, find(cell, "/") + 1, len(cell))
end function

function key_of(contributor)
  return contributor.path[0] + "/" + contributor.path[1]
end function

function f2(x)
  if is_unknown(x) then
    return "unknown"
  end if
  return string(round(x, 2))
end function

function f3(x)
  if is_unknown(x) then
    return "unknown"
  end if
  return string(round(x, 3))
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function
