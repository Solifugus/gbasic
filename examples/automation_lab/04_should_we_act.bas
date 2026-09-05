' Design laboratory — Recipe 6: the first recipe that ACTS.
'
' Recipe 5 crossed into `decision`. Nothing had crossed into `automation`,
' which left two of the design's refusals never tested at all:
'
'   R5  a process that has never been replayed against history may not act;
'   R6  authority is ENFORCED at the action -- and only the STATING half had
'       ever been exercised.
'
' The interesting half is not the gate. It is what the replay TELLS you.
program main()
  load insight
  load frame
  load fake
  load reasoning
  load automation

  print "=== §19's questions, asked of two configurations of the SAME process"
  print ""
  coarse = replay(1, ["Outdoor", "Apparel", "Home"])
  fine = replay(3, ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"])

  print ""
  print "                        cells  fires  caught the real one  false alarms"
  show("by region x category", coarse)
  show("plus store", fine)

  print ""
  print "  The coarse configuration MISSES the collapse it exists to find,"
  print "  and the one time it does fire, it is wrong. Every alarm it raises"
  print "  in a year is a false one. The fine configuration catches the real"
  print "  event and raises nothing else."
  print ""
  print "  Same code, same data, same year. Nothing in the source says which"
  print "  of the two you have -- only the replay does, which is why R5 makes"
  print "  it a precondition rather than a feature to add later."

  ' ------------------------------------------------------------------
  print ""
  print "=== the gates, with the fine configuration's rehearsal"
  print ""
  rehearsal = automation.rehearsal({ periods: 12, fired: fine.fires,
                                     false_alarms: fine.false_alarms,
                                     missed: 0 })
  ctx = { authority: { spend_limit: 5000, min_rehearsal_periods: 12 } }
  approved = { authority: { spend_limit: 5000, min_rehearsal_periods: 12 },
               approval: { by: "regional director", at: "2026-09-02" } }

  within = a_decision(false)
  beyond = a_decision(true)

  gate("within authority, never rehearsed", within, ctx, unknown)
  gate("within authority, rehearsed", within, ctx, rehearsal)
  gate("beyond authority, rehearsed, unapproved", beyond, ctx, rehearsal)
  gate("beyond authority, rehearsed, approved", beyond, approved, rehearsal)

  print ""
  print "  Note the first line. An unrehearsed process is refused BEFORE"
  print "  authority is even considered, and approval does not substitute:"
  print "  a human asked to approve it has nothing to approve on, because"
  print "  nobody knows how often it fires or how often it is wrong."
end program

function gate(label, dec, context, rehearsal)
  load automation
  w = automation.would(dec, context, rehearsal)
  verdict = "REFUSED"
  if w.would_act then
    verdict = "acts"
  end if
  print "  " + pad(label, 42) + pad(verdict, 9) + w.reason
  return nothing
end function

function a_decision(needs_authority)
  load reasoning
  return reasoning.decision({
    objective: { measure: "revenue", direction: "maximize" },
    alternatives: [], recommendation: "restock and promote",
    expected_value: 7497, authority_required: needs_authority,
    authority_reason: "cost 15000 exceeds the spend limit of 5000",
    sized_off: { quantity: "leading_cell", established: true, value: 0 - 16835 },
    finding: { subject: "Northeast/Outdoor", measure: "revenue" },
    provenance: { method: "recipe 5", rows: 1, parameters: { }, assumptions: [] } })
end function

' Replay twelve months. A real collapse is planted in month 7 and nowhere else.
function replay(stores, cats)
  fires = 0
  false_alarms = 0
  caught = false
  cells = 0
  for m = 1 to 12
    f = one_month(m, stores, cats)
    cells = f.search.cells
    if f.strength.clears then
      fires = fires + 1
      if m = 7 then
        caught = true
      else
        false_alarms = false_alarms + 1
      end if
    end if
  next
  return { cells: cells, fires: fires, caught: caught, false_alarms: false_alarms }
end function

function one_month(m, stores, cats)
  load insight
  load frame
  load fake
  rows = []
  i = 0
  for each rg in ["North", "South", "East", "West"]
    for s = 1 to stores
      store = rg + "-" + string(s)
      for each c in cats
        for d = 1 to 30
          for p = m - 1 to m
            i = i + 1
            amt = fake.lognormal(2026, p * 1000000 + i, 1000, 0.5)
            if p = 7 and store = "North-1" and c = "Outdoor" then
              amt = amt * 0.45
            end if
            append(rows, { region: rg, store: store, category: c,
                           period: p, revenue: amt })
          next
        next
      next
    next
  next
  dims = ["region", "store", "category"]
  if stores = 1 then
    dims = ["region", "category"]
  end if
  return insight.explain_change(frame.from_rows(rows),
           { measure: "revenue", period: "period", baseline: m - 1, current: m,
             dimensions: dims, comparison: "period_over_period", null: "siblings" })
end function

function show(label, r)
  print ("  " + pad(label, 22) + pad(string(r.cells), 7) + pad(string(r.fires), 7)
         + pad(string(r.caught), 21) + string(r.false_alarms))
  return nothing
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function
