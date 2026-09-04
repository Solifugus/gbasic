' Design laboratory — Recipe 3: a seasonal measure.
'
' Every null this library offers is CROSS-SECTIONAL: `siblings` and
' `siblings_permuted` both ask "is this cell unlike the others RIGHT NOW".
' Neither looks at time. The provenance of every Finding records the
' assumption in as many words —
'
'     "ordinary movement is stationary across cells"
'
' — and nothing anywhere checks it. Design §7 says both nulls are therefore
' wrong under seasonality and leaves the question open. This is that question,
' run rather than argued.
'
' The measure is retail revenue and the comparison is DECEMBER TO JANUARY,
' which is the most ordinary seasonal comparison in commerce. Toys fall by
' five sixths every January and nothing is wrong. Four runs:
'
'   RUN A — December to January, NOTHING planted.
'   RUN B — December to January, a real 45% collapse planted in West/Grocery.
'   RUN C — the SAME planted data compared JANUARY TO JANUARY.
'   RUN D — January to January with nothing planted. The control.
'
' Runs A and B are the experiment: same code, same thresholds, one of them
' has a real problem in it. Runs C and D exist so that "the library failed"
' cannot be confused with "the data holds nothing to find".
program main()
  load insight

  banner("RUN A", "December -> January.  Nothing is wrong.")
  report(look(false, "dec", "jan", "period_over_period"))

  banner("RUN B", "December -> January.  West / Grocery really collapsed 45%.")
  report(look(true, "dec", "jan", "period_over_period"))

  banner("RUN C", "January -> January.  The SAME collapsed population.")
  report(look(true, "prior_jan", "jan", "versus_last_year"))

  banner("RUN D", "January -> January.  Nothing planted.  The control.")
  report(look(false, "prior_jan", "jan", "versus_last_year"))

  banner("RUN E", "Run C again, with null: siblings_permuted.")
  report(look2(true, "prior_jan", "jan", "siblings_permuted"))

  banner("RUN F", "Run D again, with null: siblings_permuted.")
  report(look2(false, "prior_jan", "jan", "siblings_permuted"))

  print ""
  print "=================================================================="
  print " WHAT THIS MEASURED"
  print "=================================================================="
  print ""
  print "  1. SEASONALITY DOES NOT PRODUCE FALSE ALARMS HERE. IT PRODUCES"
  print "     BLINDNESS. Runs A and B agree on every number that matters:"
  print "     the same leader, the same z to two decimals, nothing clearing"
  print "     either time. One of them has a cell that lost 45% of its"
  print "     revenue. That cell's z went from +0.52 to -0.09 -- the"
  print "     collapse made it look MORE ordinary, because the seasonal"
  print "     spread it is judged against is so wide that a real failure"
  print "     lands in the middle of it."
  print ""
  print "  2. WITHOUT R13 THE HEADLINE IS WORSE THAN THE SILENCE. The net"
  print "     test establishes a 50.1% decline at t = -4.49 and shares were"
  print "     duly reported: a confident attribution of an ordinary January"
  print "     to whichever cell sells the most toys. The signature is in"
  print "     the data and costs nothing to look for -- 22 of 24 cells moved"
  print "     the same way, which is a one-in-thirty-thousand event if a"
  print "     cell is as likely to rise as to fall. A movement common to"
  print "     every cell is not explained by any of them."
  print ""
  print "  3. THE FIX IS THE COMPARISON, NOT THE NULL. Run C is the same"
  print "     data, the same library and the same threshold, asked January"
  print "     against January: West / Grocery, z -4.91, rank 1, clears."
  print "     Design §7 had declared `versus_last_year` for two months and"
  print "     `reasoning.comparisons()` returned one value, so the remedy"
  print "     for seasonality could not be spelled. That is fixed here."
  print ""
  print "  4. AND THE NULL STILL MATTERS, FOR THE OTHER FAULT. Run D plants"
  print "     nothing and Southeast / Home clears anyway at z 3.59 -- the"
  print "     miscalibration already measured for the t threshold, seen"
  print "     live. Runs E and F repeat C and D under siblings_permuted:"
  print "     the false one goes away and the true one survives. That is a"
  print "     confirmation on a population the permuted null was never"
  print "     tuned against."
end program

function look2(plant_it, base_period, now_period, which_null)
  load insight
  return insight.explain_change(build(plant_it),
           { measure: "revenue", period: "period",
             baseline: base_period, current: now_period,
             dimensions: ["region", "category"],
             comparison: "versus_last_year", null: which_null })
end function

function banner(tag, what)
  print ""
  print "=================================================================="
  print " " + tag + " — " + what
  print "=================================================================="
  return nothing
end function

function look(plant_it, base_period, now_period, comparison)
  load insight
  return insight.explain_change(build(plant_it),
           { measure: "revenue", period: "period",
             baseline: base_period, current: now_period,
             dimensions: ["region", "category"],
             comparison: comparison, null: "siblings" })
end function

' ---------------------------------------------------------------- reporting
function report(f)
  print ("  change              " + string(round(f.observation.change, 0))
         + "   (" + string(round(f.observation.change_pct * 100, 1)) + "%)")
  print ("  net t               " + string(round(f.null.net_t, 2))
         + "   over " + string(f.search.cells) + " cells")
  cm = f.null.common_movement
  print ("  moved together      " + string(cm.down) + " down / "
         + string(cm.up) + " up   sign p " + string(round(cm.p, 5)))
  if f.shares_reportable then
    print "  shares              REPORTED"
  else
    print "  shares              WITHHELD"
    print "    because " + f.shares_withheld_because
  end if
  print "  threshold           z " + string(round(f.search.width, 2))
  print ("  strength.leader     " + join(f.strength.leader, " / ")
         + "   z " + string(round(f.strength.z, 2)))
  if f.strength.clears then
    print "  strength.clears     YES — beyond ordinary variation"
  else
    print "  strength.clears     no"
  end if

  ' What `strength` reports is contributors[0], which is the biggest MOVER,
  ' and the most anomalous cell need not in principle be the biggest mover.
  ' In practice they barely can differ -- a leave-one-out z is the change
  ' divided by a spread that is nearly the same for every cell -- and across
  ' all six runs here they never did. Printed anyway, so that if they ever
  ' part company the golden moves and somebody looks.
  best = f.contributors[0]
  fired = 0
  for each c in f.contributors
    if abs(c.z) > abs(best.z) then
      best = c
    end if
    if c.clears then
      fired = fired + 1
    end if
  next
  print ("  most anomalous      " + join(best.path, " / ")
         + "   z " + string(round(best.z, 2)))
  print "  cells clearing      " + string(fired)

  ' Where did the cell that really collapsed end up?
  rank = 0
  seen = 0
  planted_z = unknown
  for each c in f.contributors
    seen = seen + 1
    if c.path[0] = "West" and c.path[1] = "Grocery" then
      rank = seen
      planted_z = c.z
    end if
  next
  print ("  West / Grocery      rank " + string(rank) + " by size, z "
         + string(round(planted_z, 2)))
  return nothing
end function

' ---------------------------------------------------------------- the data
'
' Twenty-four cells, four periods, twenty days each. Revenue is lognormal
' about a level that is the cell's own base times its category's SEASONAL
' INDEX for that month. The index is the same in both years, which is the
' generous case: seasonality that is perfectly stable and therefore perfectly
' removable by a year-over-year comparison.
'
' The generator index is a pure function of (cell, period, day) so that a
' cell's December is the same December whichever comparison asked for it.
function build(plant_it)
  load fake
  load frame
  regions = ["Northeast", "Southeast", "Midwest", "West"]
  cats = ["Toys", "Electronics", "Apparel", "Grocery", "Home", "Outdoor"]
  periods = ["prior_dec", "prior_jan", "dec", "jan"]
  rows = []
  ri = 0
  for each rg in regions
    ci = 0
    for each c in cats
      level = 900 + ri * 120 + ci * 40
      pi = 0
      for each p in periods
        idx = seasonal_index(c, p)
        ' A mild, uniform 3% of real growth between the two years, so the
        ' year-over-year control is not trivially zero.
        growth = 1
        if p = "dec" or p = "jan" then
          growth = 1.03
        end if
        for d = 1 to 20
          i = ri * 100000 + ci * 10000 + pi * 100 + d
          amt = fake.lognormal(83, i, level * idx * growth, 0.35)
          if plant_it and p = "jan" and rg = "West" and c = "Grocery" then
            amt = amt * 0.55
          end if
          append(rows, { region: rg, category: c, period: p, revenue: amt })
        next
        pi = pi + 1
      next
      ci = ci + 1
    next
    ri = ri + 1
  next
  return frame.from_rows(rows)
end function

' December and January multipliers. These are the ordinary shape of the
' trade, not a defect: toys triple into Christmas and collapse after it.
function seasonal_index(category, period)
  is_dec = period = "dec" or period = "prior_dec"
  if category = "Toys" then
    if is_dec then
      return 3
    end if
    return 0.5
  end if
  if category = "Electronics" then
    if is_dec then
      return 2
    end if
    return 0.7
  end if
  if category = "Apparel" then
    if is_dec then
      return 1.4
    end if
    return 0.9
  end if
  if category = "Grocery" then
    if is_dec then
      return 1.15
    end if
    return 1
  end if
  if category = "Home" then
    if is_dec then
      return 1.1
    end if
    return 1
  end if
  if is_dec then
    return 0.6
  end if
  return 0.5
end function
