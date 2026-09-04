load insight
load frame
load fake

function trial(seed, regions, cats, days)
  load fake
  load frame
  load insight
  rows = []
  cell = 0
  for r = 1 to regions
    for c = 1 to cats
      for d = 1 to days
        for p = 0 to 1
          append(rows, { region: "r" + string(r), category: "c" + string(c),
                         period: p,
                         revenue: fake.lognormal(seed, p * 200000 + cell * 300 + d,
                                                 1000, 0.5) })
        next
      next
      cell = cell + 1
    next
  next
  f = insight.explain_change(frame.from_rows(rows),
        { measure: "revenue", period: "period", baseline: 0, current: 1,
          dimensions: ["region", "category"], comparison: "period_over_period",
          null: "siblings" })
  fired = 0
  for each ct in f.contributors
    if ct.clears then
      fired = fired + 1
    end if
  next
  return { any: fired > 0, cells: f.search.cells, width: f.search.width }
end function

for each cfg in [{ r: 3, c: 4, n: 300 }, { r: 4, c: 5, n: 300 }, { r: 5, c: 8, n: 300 }]
  hits = 0
  w = 0
  cells = 0
  for t = 1 to cfg.n
    res = trial(t * 37, cfg.r, cfg.c, 8)
    cells = res.cells
    w = res.width
    if res.any then
      hits = hits + 1
    end if
  next
  print (string(cells) + " cells  threshold " + string(round(w, 2))
         + "   fired " + string(hits) + "/" + string(cfg.n)
         + " = " + string(round(hits / cfg.n, 3)) + "   (requested alpha 0.05)")
next
