' Design laboratory — Recipe 7: did the action work?
'
' `automation.observe` records an outcome and R7 refuses to cite an unmeasured
' one as evidence. But nothing yet FEEDS an outcome back, so §11's learning
' cycle is bookkeeping rather than a loop. This recipe closes it -- and finds
' that the obvious way to close it teaches the system something false.
'
' THE INTERVENTION IN THIS EXPERIMENT DOES NOTHING AT ALL. Its true effect is
' exactly zero, by construction: no line below alters a single number after the
' decision is taken. Watch what the outcome measurement says about it.
program main()
  load insight
  load frame
  load fake
  load automation

  print "Eighteen months. Each month the process finds the worst cell and"
  print "'intervenes' -- an intervention with a TRUE EFFECT OF ZERO."
  print ""
  print "Cells are split by automation.assign -- deterministic in the cell's"
  print "own key, so a replay makes the same assignments as the live run."
  print ""

  treated_fall = 0
  treated_bounce = 0
  treated_n = 0
  held_fall = 0
  held_bounce = 0
  held_n = 0

  for m = 2 to 18
    now = worst_cell(m)
    nxt = change_of(m + 1, now.path)
    if automation.assign(join(now.path, "/"), 0.5).arm = "treat" then
      treated_fall = treated_fall + now.change
      treated_bounce = treated_bounce + nxt
      treated_n = treated_n + 1
    else
      held_fall = held_fall + now.change
      held_bounce = held_bounce + nxt
      held_n = held_n + 1
    end if
  next

  treated_recovery = 0 - treated_bounce / treated_fall
  held_recovery = 0 - held_bounce / held_fall

  print "  group     n   mean fall     mean bounce    apparent recovery"
  print ("  treated   " + pad(string(treated_n), 4)
         + pad(money(treated_fall / treated_n), 14)
         + pad(money(treated_bounce / treated_n), 15)
         + pct(treated_recovery))
  print ("  holdout   " + pad(string(held_n), 4)
         + pad(money(held_fall / held_n), 14)
         + pad(money(held_bounce / held_n), 15)
         + pct(held_recovery))

  print ""
  print "  WHAT WE LEARNED, measured the obvious way:"
  print "    the intervention recovers " + pct(treated_recovery) + " of the loss."
  print ""
  print "  That is the number a learning loop would store, and it is FALSE."
  print "  The intervention does nothing. What it measures is REGRESSION TO"
  print "  THE MEAN: a cell is picked BECAUSE it was extreme, and extreme"
  print "  observations contain noise, which does not repeat."
  print ""
  print "  WHAT WE LEARNED, against the holdout:"
  print "    treated recovered  " + pct(treated_recovery)
  print "    holdout recovered  " + pct(held_recovery)
  print "    effect of acting   " + pct(treated_recovery - held_recovery)
  print ""
  print "  The cells nobody touched recovered just as well. THAT is the"
  print "  measurement, and it needs something the system had no concept of:"
  print "  occasions on which it deliberately did NOT act."
  print ""
  print "  And note what the honest conclusion is NOT. The difference is"
  print "  small on a handful of observations either way, which is not a"
  print "  measurement of anything -- it is noise, and a holdout this"
  print "  small cannot resolve a small effect either. The defensible"
  print "  statement is that there is no evidence the intervention does"
  print "  anything, which happens to be exactly right: it does nothing."
  print ""
  print "  Recipe 5 ASSUMED a recovery of 60% and warned that its"
  print "  recommendation was sensitive to it. This is where that number was"
  print "  going to come from, and uncontrolled it would have come back 52%."
end program

' The worst-declining cell of a month, via the real process.
function worst_cell(m)
  load insight
  f = look(m)
  worst = f.contributors[0]
  for each c in f.contributors
    if c.change < worst.change then
      worst = c
    end if
  next
  return worst
end function

function change_of(m, path)
  load insight
  f = look(m)
  for each c in f.contributors
    if join(c.path, "/") = join(path, "/") then
      return c.change
    end if
  next
  return 0
end function

function look(m)
  load insight
  load frame
  return insight.explain_change(build(m),
           { measure: "revenue", period: "period", baseline: m - 1, current: m,
             dimensions: ["region", "store", "category"],
             comparison: "period_over_period", null: "siblings" })
end function

' THE INDEX MUST BE A PURE FUNCTION OF (cell, day, period), never of a running
' counter. The first version of this used a counter that advanced across the
' period loop, so a cell's value for month m came out DIFFERENT depending on
' whether it was generated as the baseline of month m+1 or the current of month
' m -- the series was not a series at all, and the experiment measured nothing.
' `fake` is built on exactly this rule (pure functions of seed and index) and
' the generator has to keep it.
function build(m)
  load fake
  load frame
  rows = []
  cell = 0
  for each rg in ["North", "South", "East", "West"]
    for s = 1 to 3
      store = rg + "-" + string(s)
      for each c in ["Outdoor", "Apparel", "Home"]
        for d = 1 to 20
          for p = m - 1 to m
            append(rows, { region: rg, store: store, category: c, period: p,
                           revenue: fake.lognormal(7, p * 100000 + cell * 100 + d,
                                                   1000, 0.5) })
          next
        next
        cell = cell + 1
      next
    next
  next
  return frame.from_rows(rows)
end function

function money(x)
  return string(round(x, 0))
end function

function pct(x)
  return string(round(x * 100, 1)) + "%"
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function
