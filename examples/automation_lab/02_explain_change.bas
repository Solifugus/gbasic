' Design laboratory — Recipe 1, done again through the library.
'
' 01_sales_decline.bas hand-rolls the decomposition and shows that a naive one
' cannot tell a planted cause from noise. This is the SAME two populations
' through `insight.explain_change`, and it is here for two reasons.
'
' It is a CROSS-CHECK. Two independent implementations over identical data must
' agree about where the change is concentrated -- the hand-rolled one has no
' null model and the library one does, so they must agree on the WHERE and
' disagree only about whether it MEANS anything.
'
' And it is the measurement behind design §4.7: everything recipe 1 wrote by
' hand -- grouping, distinct values, filtered totals, ranking -- is one call.
program main()
  load insight
  load frame
  load fake

  print "RUN A - a real 45% collapse planted in Northeast-2 / Outdoor"
  report(look(4242, true))
  print ""
  print "RUN B - nothing planted anywhere"
  report(look(77, false))
end program

function look(seed, plant_it)
  load insight
  return insight.explain_change(build(seed, plant_it),
           { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "store", "category"],
             comparison: "period_over_period", null: "siblings" })
end function

function report(f)
  print ("  change            " + string(round(f.observation.change, 0))
         + "  (" + string(round(f.observation.change_pct * 100, 1)) + "%)")
  print "  cells searched    " + string(f.search.cells)
  print ("  threshold         z " + string(round(f.search.width, 2))
         + "  (requested alpha " + string(f.search.alpha_requested) + ")")
  print "  threshold from    " + f.null.calibration.method
  print "  leading cell      " + join(f.strength.leader, " -> ")
  print "  its z             " + string(round(f.strength.z, 2))
  if f.strength.clears then
    print "  VERDICT           beyond ordinary variation. Worth explaining."
  else
    print "  VERDICT           within ordinary variation for this many cells."
  end if
  print "  shares reportable " + string(f.shares_reportable)
  if not f.shares_reportable then
    print "    because " + f.shares_withheld_because
  end if
  return nothing
end function

function build(seed, plant_it)
  load fake
  load frame
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
            amt = fake.lognormal(seed, i, 1000, 0.5)
            if plant_it and p = 1 and store = "Northeast-2" and c = "Outdoor" then
              amt = amt * 0.55
            end if
            append(rows, { region: rg, store: store, category: c,
                           period: p, revenue: amt })
          next
        next
      next
    next
  next
  return frame.from_rows(rows)
end function
