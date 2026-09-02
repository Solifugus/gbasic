' Automation Reasoning — design-laboratory Recipe 1: sales decline investigation.
'
' docs/automation_reasoning_design.md §11 proposes that the system perform
' automatically what an analyst does by hand in a dashboard:
'
'     Revenue declined 8.1%.
'     67% of the decline is attributable to:
'         Northeast -> Syracuse -> Outdoor -> Product family X
'
' THIS PROGRAM DOES THAT, with today's libraries and no new ones, so the
' design lessons are measured rather than imagined. It runs the SAME
' decomposition twice:
'
'   RUN A — a population with a REAL decline planted in one known cell.
'   RUN B — a population with NO planted decline at all. Only noise.
'
' Run B is the whole point. Keep the two outputs side by side.
program main()
  load fake

  print "=================================================================="
  print " RUN A — one cell really did decline"
  print "=================================================================="
  investigate(build(4242, true))

  print ""
  print "=================================================================="
  print " RUN B — nothing declined. Same code, same thresholds."
  print "=================================================================="
  investigate(build(77, false))
end program

' ---------------------------------------------------------------- the data
' Two periods of daily revenue across region / store / category. Revenue is
' lognormal, which is what real transaction amounts look like.
function build(seed, plant_it)
  load fake
  regions = ["Northeast", "Southeast", "Midwest", "West"]
  cats = ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
  rows = []
  i = 0
  for each rg in regions
    for s = 1 to 3
      store = rg + "-" + string(s)
      for each c in cats
        for d = 1 to 30
          i = i + 1
          ' Period 0 = baseline, period 1 = current.
          for p = 0 to 1
            i = i + 1
            amt = fake.lognormal(seed, i, 1000, 0.5)
            ' THE PLANTED CAUSE: in run A, one cell loses 45% in period 1.
            if plant_it and p = 1 and rg = "Northeast" and store = "Northeast-2" and c = "Outdoor" then
              amt = amt * 0.55
            end if
            append(rows, { region: rg, store: store, category: c,
                           period: p, revenue: amt })
          next
        next
      next
    next
  next
  return rows
end function

' ------------------------------------------------------- the decomposition
' For each level of a dimension: how much of the TOTAL change happened there?
function investigate(rows)
  base = total(rows, "", "", 0)
  now = total(rows, "", "", 1)
  change = now - base
  pct = change / base * 100

  print ""
  print "revenue  baseline " + money0(base) + "   current " + money0(now)
  print "change   " + money0(change) + "  (" + string(round(pct, 1)) + "%)"
  print ""

  ' Drill: region, then store within the worst region, then category.
  worst_region = drill(rows, "region", change, "", "", "by region")
  worst_store = drill(rows, "store", change, "region", worst_region, "within " + worst_region)
  worst_cat = drill_store(rows, change, worst_store)

  print ""
  print "  CONCLUSION: " + worst_region + " -> " + worst_store + " -> " + worst_cat

  ' ------------------------------------------------------------------
  ' THE PART THE DESIGN DOCUMENT DOES NOT HAVE, and the reason both runs
  ' above look identical: a drill-down is a SEARCH, and a search always
  ' returns a winner. To know whether the winner means anything we need a
  ' null -- what does the biggest cell change look like when nothing is
  ' happening? That null is available FROM THE DATA ITSELF: every other
  ' cell is a sample of "ordinary movement".
  against_the_null(rows, worst_region, worst_store, worst_cat)
  return nothing
end function

function against_the_null(rows, rg, store, cat)

  changes = []
  winner = 0
  for each r in cells(rows)
    b = cell_total(rows, r.store, r.category, 0)
    n = cell_total(rows, r.store, r.category, 1)
    d = n - b
    append(changes, d)
    if r.store = store and r.category = cat then
      winner = d
    end if
  next
  m = mean(changes)
  sd = stdev(changes)
  z = (winner - m) / sd
  ranked = sort(changes)
  print ""
  print "  against the null (" + string(count(changes)) + " leaf cells searched):"
  print "    ordinary cell change: mean " + money0(m) + ", sd " + money0(sd)
  print "    the winning cell:     " + money0(winner) + "  (z = " + string(round(z, 2)) + ")"
  print "    most extreme decline of any cell: " + money0(ranked[0])

  ' THE THRESHOLD IS NOT A CONSTANT. It is decided by HOW WIDE THE SEARCH WAS.
  ' Take the largest of n draws from a standard normal and you expect roughly
  ' sqrt(2 ln n) even when nothing is happening -- so the same z that is
  ' remarkable across 4 regions is unremarkable across 60 cells, and a
  ' decomposition that does not know its own search width cannot set this cut
  ' at all. That is a requirement on the API, not a detail of this program.
  n = count(changes)
  expected_worst = sqrt(2 * log(n))
  print ("    searching " + string(n) + " cells, the worst of them lands near z = -"
         + string(round(expected_worst, 2)) + " when NOTHING is happening")
  if 0 - z > expected_worst then
    print ("    -> z " + string(round(z, 2)) + " is beyond that. The cell is worth explaining.")
  else
    print ("    -> z " + string(round(z, 2)) + " does not clear it. A search over "
           + string(n) + " cells")
    print "       returns a winner whether or not anything happened. This is that winner."
  end if
  return nothing
end function

function cells(rows)
  seen = { }
  out = []
  for each r in rows
    k = r.store + "|" + r.category
    if is_unknown(seen[k]) then
      seen[k] = true
      append(out, { store: r.store, category: r.category })
    end if
  next
  return out
end function

function cell_total(rows, store, cat, p)
  t = 0
  for each r in rows
    if r.period = p and r.store = store and r.category = cat then
      t = t + r.revenue
    end if
  next
  return t
end function

function drill(rows, axis, change, filter_dim, filter_val, label)
  vals = distinct(rows, axis, filter_dim, filter_val)
  print "  " + label + ":"
  best = ""
  best_share = 0 - 999999
  for each v in vals
    b = total_at(rows, axis, v, filter_dim, filter_val, 0)
    n = total_at(rows, axis, v, filter_dim, filter_val, 1)
    share = (n - b) / change * 100
    print ("    " + pad(v, 16) + " change " + pad(money0(n - b), 12)
           + "  = " + string(round(share, 1)) + "% of the total change")
    if share > best_share then
      best_share = share
      best = v
    end if
  next
  print "    -> largest contributor: " + best + " (" + string(round(best_share, 1)) + "%)"
  return best
end function

function drill_store(rows, change, store)
  vals = distinct(rows, "category", "store", store)
  print "  within " + store + ":"
  best = ""
  best_share = 0 - 999999
  for each v in vals
    b = total_at(rows, "category", v, "store", store, 0)
    n = total_at(rows, "category", v, "store", store, 1)
    share = (n - b) / change * 100
    print ("    " + pad(v, 16) + " change " + pad(money0(n - b), 12)
           + "  = " + string(round(share, 1)) + "% of the total change")
    if share > best_share then
      best_share = share
      best = v
    end if
  next
  print "    -> largest contributor: " + best + " (" + string(round(best_share, 1)) + "%)"
  return best
end function

' ------------------------------------------------------------- primitives
function total(rows, axis, val, p)
  return total_at(rows, axis, val, "", "", p)
end function

function total_at(rows, axis, val, filter_dim, filter_val, p)
  t = 0
  for each r in rows
    if r.period = p then
      if axis = "" or r[axis] = val then
        if filter_dim = "" or r[filter_dim] = filter_val then
          t = t + r.revenue
        end if
      end if
    end if
  next
  return t
end function

function distinct(rows, axis, filter_dim, filter_val)
  seen = { }
  out = []
  for each r in rows
    if filter_dim = "" or r[filter_dim] = filter_val then
      v = r[axis]
      if is_unknown(seen[v]) then
        seen[v] = true
        append(out, v)
      end if
    end if
  next
  return sort(out)
end function

function money0(x)
  return string(round(x, 0))
end function

function pad(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
