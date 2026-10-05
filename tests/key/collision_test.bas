' THE REGRESSION THAT MOTIVATED `key`, run through the real library.
'
' Before `key`, insight's cell identity was `key + string(r[d]) + "|"`, so two
' DISTINCT cells could share one key. This is not hypothetical: it was
' demonstrated against the shipped library before anything changed: over a
' population of eight distinct cells it reported SEVEN. This fixture builds
' SIX (two parameterised plus four filler), so the pre-fix answer here is five.
'
' WORSE THAN A WRONG GROUPING: `search.cells` feeds the Bonferroni threshold,
' so a merge corrupts the statistic AND the width it is judged against -- in
' the library whose whole job is deciding whether a deviation is real.
'
' THE CONTROL is the second case: data with NO separator in it must report the
' same count either way, or "the counts match" would be satisfied by a change
' that broke cell identity generally.
load insight
load frame

function cells_for(a_region, a_store, b_region, b_store)
    rows = []
    append(rows, { region: a_region, store: a_store, period: 0, revenue: 100 })
    append(rows, { region: a_region, store: a_store, period: 1, revenue: 100 })
    append(rows, { region: b_region, store: b_store, period: 0, revenue: 100 })
    append(rows, { region: b_region, store: b_store, period: 1, revenue: 100 })
    for each nm in ["D", "E", "F", "G"]
        append(rows, { region: "R" + nm, store: nm, period: 0, revenue: 100 })
        append(rows, { region: "R" + nm, store: nm, period: 1, revenue: 100 })
    next
    f = insight.explain_change(frame.from_rows(rows),
          { measure: "revenue", period: "period", baseline: 0, current: 1,
            dimensions: ["region", "store"],
            comparison: "period_over_period", null: "siblings" })
    return f.search.cells
end function

program main( args )
    n = cells_for("North|East", "A", "North", "East|A")
    if n = 6 then
        print "ok   a separator in the data does not merge two cells (6 of 6)"
    else
        print "MISMATCH six distinct cells reported as " + string(n)
    end if
    c = cells_for("North", "A", "South", "B")
    if c = 6 then
        print "ok   CONTROL separator-free data still counts 6"
    else
        print "MISMATCH CONTROL separator-free data reported " + string(c)
    end if
end program
