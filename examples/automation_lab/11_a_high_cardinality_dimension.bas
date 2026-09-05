' Design laboratory — Recipe 4: a high-cardinality dimension.
'
' The depth plan predicted what this recipe would find: "search width
' dominates; almost nothing should survive." R1 made the search width a
' first-class part of every Finding on exactly that reasoning -- a z that is
' remarkable across four regions is unremarkable across two hundred product
' families -- so cutting a business by SKU rather than by region ought to raise
' the bar until nothing clears.
'
' It is the wrong prediction, and the measurement says so plainly.
'
' THE BUSINESS IS THE SAME SIZE IN EVERY RUN. The same money, the same number
' of transactions; only the cut changes. Twenty-four coarse cells, then two
' hundred and forty, then twelve hundred. What changes with the cut is not
' really the threshold -- it is how much of the business is left in each cell.
program main()
  load insight

  print "=================================================================="
  print " THE SAME BUSINESS, CUT THREE WAYS"
  print "=================================================================="
  print ""
  print "  cells   per cell   threshold   quiet run   planted z   found?"
  print "  -----   --------   ---------   ---------   ---------   ------"
  ' Each cut is surveyed ONCE and both tables are printed from the same
  ' findings; at twelve hundred cells a run is not cheap.
  cuts = []
  for each ncells in [24, 240, 1200]
    per_cell = 12000 / ncells
    append(cuts, { n: ncells, per_cell: per_cell,
                   quiet: look(ncells, per_cell, 0 - 1, 1),
                   planted: look(ncells, per_cell, 0, 0.25) })
  next
  for each cut in cuts
    survey(cut)
  next
  print ""
  print "  Fifty times the search width buys an 18% higher bar. The"
  print "  Bonferroni correction is the CHEAP part -- it grows like the"
  print "  square root of a logarithm and it was never going to dominate"
  print "  anything. What collapses is the planted cell's z, and it"
  print "  collapses because the cell it lives in has almost nothing in it."

  print ""
  print "=================================================================="
  print " WHAT A CHANGE WOULD HAVE HAD TO BE, TO BE FOUND"
  print "=================================================================="
  print ""
  print "  cells   a typical cell bills   smallest detectable change"
  print "  -----   --------------------   --------------------------"
  for each cut in cuts
    power(cut)
  next
  print ""
  print "  Read the last row twice. At twelve hundred cells the smallest"
  print "  change that could clear the bar is larger than a typical cell's"
  print "  entire revenue, so NO DECLINE IN SUCH A CELL, HOWEVER COMPLETE,"
  print "  COULD EVER BE REPORTED. The cell could go to zero and the answer"
  print "  would still be `within ordinary variation` -- in exactly the"
  print "  words the library uses when a business is healthy."

  print ""
  print "=================================================================="
  print " WHAT THIS MEASURED"
  print "=================================================================="
  print ""
  print "  1. THE PREDICTION WAS WRONG, AND IN A USEFUL DIRECTION. Search"
  print "     width does not dominate. Widening the search fifty-fold moves"
  print "     the threshold from 3.49 to 4.11, because sqrt(2 ln n) grows"
  print "     about as slowly as anything in statistics. Paying for the"
  print "     multiplicity of a wide search is nearly free."
  print ""
  print "  2. WHAT DOMINATES IS SUPPORT. Cut the same business finer and"
  print "     each cell holds less of it, so each cell's ordinary variation"
  print "     grows relative to its size. The planted cell loses 75% of its"
  print "     revenue in all three runs and its z falls from -14.6 to -3.3."
  print "     Nothing about that cell changed between the runs; only how"
  print "     much of the business was left in it."
  print ""
  print "  3. AND THE LIBRARY HAD NO WAY TO SAY SO. `within ordinary"
  print "     variation` is returned identically by a search that examined a"
  print "     healthy business and by one that could not have found a cell"
  print "     going to zero. That silence is what R15 fixes: a Finding now"
  print "     states the smallest change it could have found, in the units"
  print "     of the business and as a share of a typical cell. It is"
  print "     reported whether or not anything cleared, because `nothing"
  print "     cleared` is precisely when a reader needs it."
end program

' ---------------------------------------------------------------- reporting
function survey(cut)
  load insight
  ncells = cut.n
  per_cell = cut.per_cell
  quiet = cut.quiet
  planted = cut.planted
  fa = 0
  for each c in quiet.contributors
    if c.clears then
      fa = fa + 1
    end if
  next
  z0 = 0
  hit = "no"
  for each c in planted.contributors
    if c.path[0] = "S0" then
      z0 = c.z
      if c.clears then
        hit = "YES"
      end if
    end if
  next
  ' At the finest cut something else clears instead, which is worth naming.
  other = ""
  for each c in planted.contributors
    if c.clears and c.path[0] != "S0" then
      other = "  (" + c.path[0] + " cleared instead)"
    end if
  next
  print ("  " + pad(string(ncells), 5) + "   " + pad(string(per_cell), 8)
         + "   " + pad(string(round(quiet.search.width, 2)), 9)
         + "   " + pad(string(fa) + " cleared", 9)
         + "   " + pad(string(round(z0, 2)), 9)
         + "   " + hit + other)
  return nothing
end function

function power(cut)
  load insight
  d = cut.quiet.search.detectable
  print ("  " + pad(string(cut.n), 5) + "   "
         + pad(string(round(d.typical_cell, 0)), 20) + "   "
         + pad(string(round(d.change, 0)), 12)
         + "  (" + string(round(d.share * 100, 0)) + "% of a cell)")
  return nothing
end function

function pad(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function

function look(ncells, per_cell, collapse_cell, frac)
  load insight
  return insight.explain_change(build(ncells, per_cell, collapse_cell, frac),
    { measure: "revenue", period: "period", baseline: 0, current: 1,
      dimensions: ["sku"], comparison: "period_over_period",
      null: "siblings" })
end function

' ---------------------------------------------------------------- the data
' A business of FIXED total size, cut into more and more cells: total
' transactions per period stays at 12,000 however many SKUs they are spread
' across. Revenue is lognormal with a wide sigma, which is what transaction
' amounts look like and what makes a thin cell so unruly.
function build(ncells, per_cell, collapse_cell, frac)
  load fake
  load frame
  rows = []
  for c = 0 to ncells - 1
    for d = 1 to per_cell
      for p = 0 to 1
        amt = fake.lognormal(4242, c * 100000 + d * 10 + p, 1000, 0.7)
        if p = 1 and c = collapse_cell then
          amt = amt * frac
        end if
        append(rows, { sku: "S" + string(c), period: p, revenue: amt })
      next
    next
  next
  return frame.from_rows(rows)
end function
