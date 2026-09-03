' Design laboratory — Recipe 8: why might it be happening?
'
' §4's ladder is OBSERVATION -> ASSOCIATION -> CAUSAL HYPOTHESIS -> TEST ->
' supported or rejected explanation. Everything built so far stops at the
' first rung: `insight.explain_change` says WHERE a change is concentrated and
' refuses to say why, and R3 has been pointing at machinery that did not exist.
'
' THIS DOES NOT DETERMINE A CAUSE, and it is built so that it cannot. Every
' hypothesis it handles carries `explains: false` for its whole life. What it
' produces is a QUESTION: here is what each candidate would predict, here is
' how well that matches what actually happened, and here is the observation
' that would tell the survivors apart.
program main()
  load insight
  load reasoning
  load frame
  load fake

  f = investigate()
  print "THE FINDING"
  print "  leading cell   " + join(f.strength.leader, " -> ")
  print "  cells that cleared their threshold: " + string(cleared(f))
  print ""

  hs = [
    reasoning.hypothesis({
      name: "supply constraint across store Northeast-2",
      predicts: { store: "Northeast-2" },
      discriminator: "inventory availability in that store's OTHER categories" }),
    reasoning.hypothesis({
      name: "an Outdoor category problem everywhere",
      predicts: { category: "Outdoor" },
      discriminator: "Outdoor performance in the other eleven stores" }),
    reasoning.hypothesis({
      name: "a Northeast regional demand shock",
      predicts: { region: "Northeast" },
      discriminator: "the other two Northeast stores" }),
    reasoning.hypothesis({
      name: "a stock-out of Outdoor goods at Northeast-2",
      predicts: { store: "Northeast-2", category: "Outdoor" },
      discriminator: "on-hand inventory for that line at that store" }),
    reasoning.hypothesis({
      name: "a competitor opened near Northeast-2 selling outdoor goods",
      predicts: { store: "Northeast-2", category: "Outdoor" },
      discriminator: "footfall and competitor pricing near that store" })
  ]

  w = insight.weigh(f, hs)

  print "WEIGHED AGAINST THE PATTERN"
  print "  hypothesis                                       predicts hit over agree"
  for each h in w.hypotheses
    print ("  " + pad(left(h.name, 46), 48) + pad(string(h.predicted), 9)
           + pad(string(h.hit), 4) + pad(string(h.over_predicted), 5)
           + string(round(h.agreement, 2)))
  next
  print ""
  print "  " + w.agreement_is

  print ""
  print "  Note what ranks and why. The narrowest hypothesis consistent with"
  print "  the data wins, because a hypothesis that predicts fifteen cells and"
  print "  explains one has OVER-PREDICTED fourteen. Parsimony is not imposed"
  print "  here; it falls out of comparing predicted cells with affected ones."

  print ""
  print "WHAT THIS DATA CANNOT DECIDE"
  for each t in w.indistinguishable
    print "  '" + left(t.a, 44) + "'"
    print "  '" + left(t.b, 44) + "'"
    print "  predict exactly the same cells. Ranking them would invent a"
    print "  preference the evidence does not support."
  next

  print ""
  print "  leader                " + left(w.leader, 50)
  print "  separable from rivals " + string(w.leader_is_separable)
  print ""
  print "  NEXT TEST"
  print "    " + w.next_test
  print ""
  print "  That is the output. Not a cause -- a question, and the observation"
  print "  that would answer it. Every hypothesis above still says"
  print "  explains: " + string(w.hypotheses[0].explains) + "."
end program

function cleared(f)
  n = 0
  for each c in f.contributors
    if c.clears then
      n = n + 1
    end if
  next
  return n
end function

function investigate()
  load insight
  load frame
  load fake
  rows = []
  cell = 0
  for each rg in ["Northeast", "Southeast", "Midwest", "West"]
    for s = 1 to 3
      store = rg + "-" + string(s)
      for each c in ["Outdoor", "Apparel", "Home", "Grocery", "Electronics"]
        for d = 1 to 60
          for p = 0 to 1
            amt = fake.lognormal(4242, p * 100000 + cell * 100 + d, 1000, 0.5)
            if p = 1 and store = "Northeast-2" and c = "Outdoor" then
              amt = amt * 0.4
            end if
            append(rows, { region: rg, store: store, category: c,
                           period: p, revenue: amt })
          next
        next
        cell = cell + 1
      next
    next
  next
  return insight.explain_change(frame.from_rows(rows),
           { measure: "revenue", period: "period", baseline: 0, current: 1,
             dimensions: ["region", "store", "category"],
             comparison: "period_over_period", null: "siblings" })
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function
