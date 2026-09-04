load insight
load frame
load fake

function trial(seed, nullkind, regions, cats)
  load fake
  load frame
  load insight
  rows = []
  cell = 0
  for r = 1 to regions
    for c = 1 to cats
      for d = 1 to 8
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
          null: nullkind })
  for each ct in f.contributors
    if ct.clears then
      return true
    end if
  next
  return false
end function

function pad(t, w)
  out = t
  while len(out) < w
    out = out + " "
  end while
  return out
end function

for each cfg in [{ k: "siblings", r: 4, c: 5 }, { k: "siblings_permuted", r: 4, c: 5 },
                 { k: "siblings", r: 5, c: 8 }, { k: "siblings_permuted", r: 5, c: 8 }]
  hits = 0
  for t = 1 to 200
    if trial(t * 37, cfg.k, cfg.r, cfg.c) then
      hits = hits + 1
    end if
  next
  print (pad(cfg.k, 20) + string(cfg.r * cfg.c) + " cells: "
         + string(hits) + "/200 = " + string(round(hits / 200, 3)))
next

