' Design laboratory — Recipe 5: the first recipe that DECIDES.
'
' Recipes 1-4 all stop at the finding, which leaves two thirds of the
' architecture unexamined (design §15). This one takes the Finding that
' `insight.explain_change` actually produces and asks what should be done
' about it.
'
' It is hand-rolled on purpose, like Recipe 1 was, so that what the decision
' layer must absorb is visible rather than assumed.
program main()
  load insight
  load frame
  load fake

  f = investigate()

  print "THE FINDING"
  print "  aggregate change   " + usd(f.observation.change)
  print "  established?       " + string(f.shares_reportable)
  print "  leading cell       " + join(f.strength.leader, " -> ")
  print "  its change         " + usd(top_change(f))
  print "  established?       " + string(f.strength.clears)
  print ""
  print "  So ONE cell is real and the aggregate is not. That is not a corner"
  print "  case -- it is what the data said, and the decision has to survive it."

  ' The intervention on the table: an emergency restock plus local promotion.
  cost = 15000
  recovery = 0.6

  print ""
  print ("THE DECISION: spend " + usd(cost) + " to recover "
         + string(recovery * 100) + "% of the loss?")
  print ""
  weigh("sized off the aggregate decline", f.observation.change, cost, recovery)
  weigh("sized off the cell that cleared", top_change(f), cost, recovery)

  print ""
  print "  THE SAME INTERVENTION, THE SAME DATA, OPPOSITE ANSWERS. The only"
  print "  difference is which number was taken as the loss -- and the Finding"
  print "  already said the aggregate was not established."

  ' ------------------------------------------------------------------
  ' The second thing the decision layer has to get right: AUTHORITY.
  ' This process may spend 5,000 without asking. Three options are on the
  ' table and the best one costs more than that.
  print ""
  print "AUTHORITY: this process may spend up to " + usd(5000) + " unattended."
  print ""
  options = [{ name: "do nothing", cost: 0, benefit: 0 },
             { name: "send a regional manager", cost: 2000, benefit: 3000 },
             { name: "restock and promote", cost: 15000, benefit: 22497 }]
  best_overall = ""
  best_ev = 0 - 999999999
  best_allowed = ""
  best_allowed_ev = 0 - 999999999
  for each o in options
    ev = o.benefit - o.cost
    within = o.cost <= 5000
    tag = "  needs approval"
    if within then
      tag = "  within authority"
    end if
    print "  " + pad(o.name, 26) + " EV " + pad(usd(ev), 8) + tag
    if ev > best_ev then
      best_ev = ev
      best_overall = o.name
    end if
    if within and ev > best_allowed_ev then
      best_allowed_ev = ev
      best_allowed = o.name
    end if
  next
  print ""
  print "  best option overall:            " + best_overall + " (EV " + usd(best_ev) + ")"
  print "  best option within authority:   " + best_allowed + " (EV " + usd(best_allowed_ev) + ")"
  print ""
  print "  A decision layer that quietly returned the second one would be"
  print "  hiding the only choice a human actually needs to make. The"
  print "  recommendation is the FIRST; authority is a separate field, and it"
  print "  is enforced later, at the action (design R6)."
end program

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function

function weigh(label, loss, cost, recovery)
  benefit = 0 - loss * recovery
  ev = benefit - cost
  print "  " + label + ":"
  print "    loss taken as    " + usd(loss)
  print "    expected benefit " + usd(benefit)
  print "    cost             " + usd(0 - cost)
  print "    expected value   " + usd(ev)
  if ev > 0 then
    print "    -> ACT"
  else
    print "    -> DO NOT ACT"
  end if
  return nothing
end function

function top_change(f)
  return f.contributors[0].change
end function

function usd(x)
  return string(round(x, 0))
end function

function investigate()
  load insight
  load frame
  load fake
  rows = []
  i = 0
  for each rg in ["Northeast", "Southeast", "Midwest", "West"]
    for s = 1 to 3
      store = rg + "-" + string(s)
      for each c in ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
        for d = 1 to 30
          i = i + 1
          for p = 0 to 1
            i = i + 1
            amt = fake.lognormal(4242, i, 1000, 0.5)
            if p = 1 and store = "Northeast-2" and c = "Outdoor" then
              amt = amt * 0.55
            end if
            append(rows, { region: rg, store: store, category: c,
                           period: p, revenue: amt })
          next
        next
      next
    next
  next
  return insight.explain_change(frame.from_rows(rows),
           { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "store", "category"],
             comparison: "period_over_period", null: "siblings" })
end function
