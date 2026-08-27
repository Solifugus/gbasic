' stats event studies. Golden-compared by tests/run_event_study.sh.
'
' EVERY CHECK STATES ITS OWN EXPECTED ANSWER, and here that is unusually
' strong: the series are CONSTRUCTED so the right answer is known exactly
' rather than recorded from a run. The asset is a fixed multiple of the market
' with zero alpha, so a correct market model must recover beta and alpha
' exactly, and an injected shock must come back as the abnormal return, to the
' digit. A golden could only tell you the numbers stopped changing.
'
' The four traps the method actually fails on all yield a plausible NUMBER
' rather than an error, which is why each has its own tier:
'   * calendar days counted where trading days were meant
'   * an event on a day the market was closed
'   * an estimation window overlapping the event it is supposed to be a
'     baseline for
'   * a CAAR averaged over windows of different widths

load stats

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' A deterministic market with spread but no trend, and an asset that is
' exactly 1.5x it. No randomness: the expected answers below are arithmetic,
' not observations.
market_r = []
asset_r = []
i = 0
while i < 200
    m = 0.001 * (mod(i, 7) - 3)
    append(market_r, m)
    append(asset_r, 1.5 * m)
    i += 1
end while
asset_r[150] = asset_r[150] + 0.04      ' the event: a clean +4% shock

print "-- the market model recovers what was put in"
r = stats.abnormal_returns(asset_r, market_r, { event: 150, estimation: 120 })
append(results, check("it fits", true, r.ok))
append(results, check("alpha is 0", 0, round(r.alpha, 8)))
append(results, check("beta is 1.5", 1.5, round(r.beta, 8)))
append(results, check("the abnormal return IS the injected shock", 0.04, round(r.ar[0], 8)))
append(results, check("CAR over a one-day window equals it", 0.04, round(r.car, 8)))
append(results, check("the window is one observation", 1, r.window))
append(results, check("and the estimation window is the size asked for", 120, r.estimation))

' The estimation window must sit strictly BEFORE the event window.
append(results, check("estimation ends before the event window starts", true, r.est_to < r.evt_from))

print ""
print "-- a wider window sums the days it covers"
w = stats.abnormal_returns(asset_r, market_r, { event: 150, pre: 2, post: 2, estimation: 120 })
append(results, check("five observations", 5, w.window))
' Only the event day is abnormal by construction, so the CAR is still 0.04.
append(results, check("CAR still equals the single shock", 0.04, round(w.car, 8)))
append(results, check("a non-event day has no abnormal return", 0, round(w.ar[0], 8)))

print ""
print "-- the other two normal-return models"
ma = stats.abnormal_returns(asset_r, market_r, { event: 150, estimation: 120, model: "market_adjusted" })
append(results, check("market_adjusted subtracts the market itself", true, ma.ok))
append(results, check("its beta is fixed at 1", 1, ma.beta))
mm = stats.abnormal_returns(asset_r, market_r, { event: 150, estimation: 120, model: "mean" })
append(results, check("the mean model ignores the market", 0, mm.beta))

print ""
print "-- TRAP 1: trading days, not calendar days"
' A real series has weekend gaps. Two TRADING days before Monday the 8th is
' Thursday the 4th; two CALENDAR days before it is Saturday the 6th, which the
' market never traded and the series does not contain.
dates = []
for each ds in ["2024-01-02","2024-01-03","2024-01-04","2024-01-05","2024-01-08","2024-01-09","2024-01-10"]
    d{date}= ds
    append(dates, d)
next ds
mon{date}= "2024-01-08"
ew = stats.event_window(dates, mon, 2, 0)
append(results, check("the window starts two TRADING days back", "2024-01-04", string(dates[ew.start])))
append(results, check("the event index is the day itself", "2024-01-08", string(dates[ew.index])))

print ""
print "-- TRAP 2: an event on a day the market was closed"
sat{date}= "2024-01-06"
sw = stats.event_window(dates, sat, 1, 1)
append(results, check("it resolves rather than failing", true, sw.ok))
append(results, check("to the NEXT trading day", "2024-01-08", string(sw.actual)))
append(results, check("and says it moved", true, sw.shifted))
' A day that IS traded must not claim to have moved.
append(results, check("a real trading day is not marked shifted", false, stats.event_window(dates, mon, 1, 1).shifted))

early{date}= "2023-12-01"
append(results, check("a window before the series is refused", false, stats.event_window(dates, early, 2, 0).ok))
late{date}= "2030-01-01"
append(results, check("an event after the series is refused", false, stats.event_window(dates, late, 0, 0).ok))

print ""
print "-- TRAP 3: too little history, and a model fitted on noise"
append(results, check("not enough observations before the event", false, stats.abnormal_returns(asset_r, market_r, { event: 10, estimation: 120 }).ok))
append(results, check("an estimation window too short for a market model", false, stats.abnormal_returns(asset_r, market_r, { event: 150, estimation: 10 }).ok))
append(results, check("an event window running past the data", false, stats.abnormal_returns(asset_r, market_r, { event: 199, post: 5, estimation: 120 }).ok))
append(results, check("an unknown model is named, not defaulted", false, stats.abnormal_returns(asset_r, market_r, { event: 150, estimation: 120, model: "capm" }).ok))

print ""
print "-- TRAP 4: a CAAR over unequal windows has no meaning"
b = []
c = []
i = 0
while i < 400
    m = 0.001 * (mod(i, 7) - 3)
    append(b, m)
    append(c, 1.5 * m)
    i += 1
end while
c[150] = c[150] + 0.03
c[350] = c[350] + 0.02          ' far enough apart that neither contaminates
e1 = stats.abnormal_returns(c, b, { event: 150, pre: 1, post: 1, estimation: 100 })
e2 = stats.abnormal_returns(c, b, { event: 350, pre: 1, post: 1, estimation: 100 })
wide = stats.abnormal_returns(c, b, { event: 350, pre: 4, post: 4, estimation: 100 })
append(results, check("mixing widths is refused", false, stats.event_study([e1, wide]).ok))

agg = stats.event_study([e1, e2])
append(results, check("two events aggregate", true, agg.ok))
append(results, check("CAAR is the mean of the two CARs", 0.025, round(agg.caar, 8)))
append(results, check("with the right count", 2, agg.n))
append(results, check("and the shared window width", 3, agg.window))
append(results, check("one event is not a study", false, stats.event_study([e1]).ok))

print ""
print "-- CONTAMINATION is reported, not hidden"
' The second event's estimation window CONTAINS the first event, so its
' normal-return model is fitted partly on abnormal behaviour. The true CAAR is
' exactly 0.025 and contamination yields 0.02455 -- close enough to read as
' ordinary noise, which is the whole reason it is surfaced.
near = stats.abnormal_returns(c, b, { event: 170, pre: 1, post: 1, estimation: 100 })
dirty = stats.event_study([e1, near])
append(results, check("clean events report no contamination", 0, agg.contaminated))
append(results, check("clustered events do", 1, dirty.contaminated))
append(results, check("and say so in words", true, contains(dirty.note, "contaminated")))
append(results, check("but the study still runs", true, dirty.ok))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
