' Design laboratory — Recipe 2: two causes at once.
'
' Recipe 1 established that a drill-down is a search and needs a null. Recipe 6
' fixed the null's worst defect -- a cell was being standardised against a
' spread it was itself part of, which put a ceiling on how extreme anything
' could look -- by leaving the cell out. Leave-one-out removes the cell from
' its own reference and NOTHING ELSE.
'
' This asks what that costs when more than one thing has gone wrong, which is
' the ordinary condition of a business rather than an exotic one.
'
' THE EXPERIMENT HOLDS ONE CELL LITERALLY CONSTANT. Northeast / Outdoor
' collapses 75% in every run below, by the same amount, from the same numbers.
' The only thing that changes between runs is how many OTHER cells collapsed
' beside it -- cells it has nothing to do with, in other regions and other
' categories. If the verdict on Northeast / Outdoor moves, nothing about
' Northeast / Outdoor caused it to.
program main()
  load insight

  print "=================================================================="
  print " PART 1 — the default: one cause is assumed, silently"
  print "=================================================================="
  print ""
  print "  The watched cell is Northeast / Outdoor and it is broken in all"
  print "  four runs, identically. `others` counts how many UNRELATED cells"
  print "  are broken at the same time."
  print ""
  print "  others  watched change   pop. mean   pop. sd      z   verdict"
  print "  ------  --------------   ---------   -------   ----   -------"
  for each others in [0, 1, 2, 4]
    row(look(others, 1, "siblings", 200), others)
  next
  print ""
  print "  The watched cell never changed. The population around it did, and"
  print "  that is enough to take it from a finding to nothing at all."

  print ""
  print "=================================================================="
  print " PART 2 — allowing for more than one"
  print "=================================================================="
  print ""
  print "  `max_causes` excludes the other candidates from the REFERENCE. It"
  print "  does not bless them as findings; it declines to let them define"
  print "  what ordinary looks like. The permuted null is required, because"
  print "  trimming changes the statistic and the t formula is a formula for"
  print "  the untrimmed one."
  print ""
  print "  others  max_causes   watched z   threshold   verdict"
  print "  ------  ----------   ---------   ---------   -------"
  compare(1, 2)
  compare(2, 3)
  compare(4, 5)
  print ""
  print "  The bar rises with what you allow for. That is the trade and it"
  print "  cannot be avoided: a population that may contain five broken cells"
  print "  is a population in which any one of them is less surprising."

  print ""
  print "=================================================================="
  print " PART 3 — the refusal"
  print "=================================================================="
  print ""
  on error goto next
  x = look(2, 3, "siblings", 200)
  if error then
    print "  " + error.message
    error.clear()
  end if
  on error stop

  print ""
  print "=================================================================="
  print " WHAT THIS MEASURED"
  print "=================================================================="
  print ""
  print "  1. THE TEST FINDS A PROBLEM ONLY WHILE IT IS NEARLY THE ONLY"
  print "     PROBLEM. Four unrelated cells collapsing beside the watched one"
  print "     halve its z and take it below the bar, without one number in"
  print "     that cell changing. Both terms of the standardisation move at"
  print "     once: the reference mean slides towards the anomaly and the"
  print "     reference spread inflates."
  print ""
  print "  2. TWO OBVIOUS REPAIRS WERE MEASURED AND BOTH FAILED. Sequential"
  print "     peeling -- test the most extreme, remove it, test the next --"
  print "     does not help, because the FIRST test is the most contaminated"
  print "     and it is the one that decides whether anything is reported."
  print "     A robust median/MAD scale does not help either: at 24 cells the"
  print "     MAD itself rose 28% between one cause and two, and once its own"
  print "     null threshold is measured honestly (4.18, against the t"
  print "     formula's 3.49) the robust statistic is FURTHER from clearing."
  print ""
  print "  3. WHAT WORKS IS A DECLARED CHOICE, NOT A BETTER DEFAULT."
  print "     Excluding the other candidates from the reference restores the"
  print "     statistic to a property of the cell: the trimmed z sits at"
  print "     -5.5 in all three runs, where the default fell from -3.65 to"
  print "     -1.86. But the bar rises with what is allowed for -- 4.35,"
  print "     4.84, 6.46 -- and by five causes in twenty-four cells it has"
  print "     outrun the evidence. That third row is the honest half: this"
  print "     is a trade with a limit, not a repair."
  print ""
  print "  4. SO THE NUMBER OF THINGS THAT MAY BE WRONG AT ONCE IS THE"
  print "     CALLER'S TO STATE, like the null and the comparison. Its"
  print "     default of 1 is not a new assumption -- it is the one that was"
  print "     always being made, now written down where it can be argued"
  print "     with, and recorded in the Finding as `search.max_causes`."
end program

function look(others, max_causes, which_null, draws)
  load insight
  return insight.explain_change(build(others),
           { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "category"],
             comparison: "period_over_period", null: which_null,
             max_causes: max_causes, draws: draws })
end function

function watched(f)
  for each c in f.contributors
    if c.path[0] = "Northeast" and c.path[1] = "Outdoor" then
      return c
    end if
  next
  return nothing
end function

function row(f, others)
  w = watched(f)
  verdict = "nothing"
  if w.clears then
    verdict = "FOUND"
  end if
  print ("  " + pad(string(others), 6) + "  " + pad(string(round(w.change, 0)), 14)
         + "   " + pad(string(round(f.null.mean, 0)), 9)
         + "   " + pad(string(round(f.null.sd, 0)), 7)
         + "   " + pad(string(round(w.z, 2)), 4) + "   " + verdict)
  return nothing
end function

' The trimmed runs permute, which costs a pass per draw per cell.
function compare(others, max_causes)
  f = look(others, max_causes, "siblings_permuted", 200)
  w = watched(f)
  verdict = "nothing"
  if w.clears then
    verdict = "FOUND"
  end if
  print ("  " + pad(string(others), 6) + "  " + pad(string(max_causes), 10)
         + "   " + pad(string(round(w.z, 2)), 9)
         + "   " + pad(string(round(f.search.width, 2)), 9)
         + "   " + verdict)
  return nothing
end function

function pad(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function

' ---------------------------------------------------------------- the data
' Twenty-four cells, two periods, thirty days each, lognormal revenue. The
' watched cell is always broken; `others` more cells are broken beside it,
' chosen from a fixed list so that run-to-run the SAME cells are added and
' nothing is reshuffled underneath.
function build(others)
  load fake
  load frame
  regions = ["Northeast", "Southeast", "Midwest", "West"]
  cats = ["Outdoor", "Apparel", "Home", "Grocery", "Electronics", "Toys"]
  broken = ["Northeast/Outdoor"]
  spare = ["Southeast/Apparel", "Midwest/Home", "West/Grocery",
           "Southeast/Toys", "Midwest/Electronics"]
  for j = 0 to others - 1
    append(broken, spare[j])
  next
  rows = []
  ri = 0
  for each rg in regions
    ci = 0
    for each c in cats
      hit = contains(broken, rg + "/" + c)
      for d = 1 to 30
        for p = 0 to 1
          amt = fake.lognormal(4242, ri * 100000 + ci * 10000 + d * 10 + p,
                               1000, 0.5)
          if p = 1 and hit then
            amt = amt * 0.25
          end if
          append(rows, { region: rg, category: c, period: p, revenue: amt })
        next
      next
      ci = ci + 1
    next
    ri = ri + 1
  next
  return frame.from_rows(rows)
end function
