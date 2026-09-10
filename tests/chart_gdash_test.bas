' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' chart -- an ordinal x, formatted axis labels, and identifiable marks.
' The three asks in gdash's docs/gdash7_platform_ask_charts.md.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced by the defect
' itself. A categorical x used to produce a COMPLETE, PLAUSIBLE SVG with no
' data in it -- axes, gridlines, fourteen text elements, and the "no data"
' note that belongs to an empty result set. A golden would have recorded that
' as expected and defended it. gdash's own test asserted
' `contains(svg, "<svg")`, WHICH AN EMPTY FRAME SATISFIES, and the blank chart
' survived six phases behind it.
'
' So the load-bearing tier asserts a DIFFERENCE: the same series over a
' numeric x and over a categorical x must draw the SAME NUMBER OF MARKS.
' "A chart came out" is satisfied by the bug.

load chart

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function marks(svg, pat)
    return count(match_all(svg, pat))
end function

function build(kind, df, xcol, ycols, opts)
    return chart.render(chart.options(chart.y(chart.x(chart.spec(kind, df), xcol), ycols), opts))
end function

print "-- ORDINAL X: the same data, two kinds of axis"
num = { t: [1.0, 2.0, 3.0], v: [4.0, 8.0, 6.0] }
cat = { t: ["Jan", "Feb", "Mar"], v: [4.0, 8.0, 6.0] }
' THE LOAD-BEARING ASSERTION. Counting marks in ONE chart proves nothing --
' the broken renderer produced a perfectly well-formed SVG. What separates a
' working ordinal axis from the silence is that the two agree.
for each kind in ["line", "area"]
    a = build(kind, num, "t", ["v"], {})
    b = build(kind, cat, "t", ["v"], {})
    check(kind + ": a categorical x draws what a numeric x draws",
          marks(b, "<path"), marks(a, "<path"))
end for
sa = build("scatter", num, "t", ["v"], {})
sb = build("scatter", cat, "t", ["v"], {})
check("scatter: a categorical x draws what a numeric x draws",
      marks(sb, "<circle"), marks(sa, "<circle"))
check("and one mark per row", marks(sb, "<circle"), 3)
' THE CATEGORIES REACH THE AXIS, or the marks could be drawn at positions
' nothing explains.
for each nm in ["Jan", "Feb", "Mar"]
    check("the axis is labelled '" + nm + "'", contains(sb, ">" + nm + "<"), true)
end for
' THE CONTROL, and it is what stops the whole fix being "delete the note":
' a frame that really IS empty must still say so.
empty = build("line", { t: [], v: [] }, "t", ["v"], {})
check("CONTROL: an empty frame still says no data", contains(empty, "no data"), true)
check("and a categorical frame does NOT", contains(sb, "no data"), false)

print ""
print "-- the positions are the ones `bar` uses"
' A LINE AND A BAR OF THE SAME FRAME MUST LINE UP, which is the moment this
' matters: a dashboard showing both would otherwise put the January point
' somewhere other than over the January column. Band centres, not an
' edge-to-edge spread. The y bound is pinned so both charts get the same left
' margin and only the x placement can differ.
bf = { m: ["Jan", "Feb", "Mar", "Apr"], rev: [4.0, 8.0, 6.0, 9.0] }
lm = build("line", bf, "m", ["rev"], { y_min: 0, markers: true })
bm = build("bar", bf, "m", ["rev"], {})
lxs = []
for each seg in match_all(lm, "circle cx=\"[0-9.]+\"")
    append(lxs, number(replace(replace(seg.text, "circle cx=\"", ""), "\"", "")))
end for
bxs = []
for each seg in match_all(bm, "rect x=\"[0-9.]+\" y=\"[0-9.-]+\" width=\"[0-9.]+\"")
    parts = match_all(seg.text, "[0-9.]+")
    append(bxs, number(parts[0].text) + (number(parts[2].text) / 2))
end for
check("four line markers and four bars", string(count(lxs)) + "," + string(count(bxs)), "4,4")
aligned = count(lxs) = count(bxs) and count(lxs) > 0
i = 0
while i < count(lxs) and i < count(bxs)
    if round(lxs[i], 2) != round(bxs[i], 2) then
        aligned = false
    end if
    i = i + 1
end while
check("every line marker sits on its bar's centre", aligned, true)

print ""
print "-- a mixed column is refused in BOTH directions"
' Ordinal is what a column that is ENTIRELY non-numeric becomes. One holding
' both has no axis that is honest about it, and quietly picking one is how a
' scale comes to mean two things.
on error goto next
build("line", { t: ["Jan", 2.0], v: [1.0, 2.0] }, "t", ["v"], {})
check("categories mixed with numbers", contains(error.message, "mixes categories and numbers"), true)
error.clear()
d1 {date}= "2026-01-01"
d2 {date}= "2026-02-01"
build("line", { t: [d1, "Jan"], v: [1.0, 2.0] }, "t", ["v"], {})
check("dates mixed with categories", contains(error.message, "mixes dates"), true)
error.clear()
' THE THREE CONTROLS. Without them "mixed is refused" is satisfied by a
' library that refuses every x column there is.
check("CONTROL: all-numeric is fine", marks(build("line", num, "t", ["v"], {}), "<path"), 1)
check("CONTROL: all-categorical is fine", marks(build("line", cat, "t", ["v"], {}), "<path"), 1)
dts = { t: [d1, d2], v: [1.0, 2.0] }
check("CONTROL: all-dates is fine", marks(build("line", dts, "t", ["v"], {}), "<path"), 1)
on error stop

print ""
print "-- a repeated category: allowed on a line, refused on a bar"
' A DELIBERATE DIFFERENCE WITH A REASON. A bar would have to INVENT A SUM for
' the second row and the picture would silently misstate it. A line, area or
' scatter invents nothing -- it draws both rows at the position their category
' occupies -- and refusing would also block a categorical scatter, which is an
' ordinary chart. Asserted as a difference, since either half alone is
' satisfied by a library with one blanket rule.
dup = { t: ["Jan", "Jan", "Feb"], v: [1.0, 5.0, 3.0] }
check("a line draws every row", marks(build("scatter", dup, "t", ["v"], {}), "<circle"), 3)
check("and the axis names each category once", marks(build("scatter", dup, "t", ["v"], {}), ">Jan<"), 1)
on error goto next
build("bar", dup, "t", ["v"], {})
check("a bar still refuses it", contains(error.message, "appears twice"), true)
error.clear()
on error stop

print ""
print "-- AXIS LABELS a caller can make agree with the numbers beside them"
' A money axis read 5,339.65 where the table beside it read $5,339.65, and a
' percent axis read 0.42 where the table read 42% -- the one place a viewer
' looks to read the scale was the one place the numbers disagreed.
money = build("line", { m: ["Jan", "Feb"], rev: [4000.0, 8000.0] }, "m", ["rev"],
              { y_prefix: "$", y_decimals: 2 })
check("a money axis carries its currency", contains(money, ">$4,000.00<"), true)
' AN EXPLICIT `decimals` MEANS FIXED PLACES. The derived count trims trailing
' zeros, which is right for a number nobody asked about and wrong for one a
' caller named: `$4,000` from `y_decimals: 2` is a setting that appears not to
' work.
check("and the requested places are not trimmed away", contains(money, ">$4,000<"), false)
frac = { m: ["Jan", "Feb"], r: [0.42, 0.58] }
pct = build("line", frac, "m", ["r"], { y_scale: 100, y_suffix: "%", y_decimals: 0 })
check("a fraction axis reads as a percentage", contains(pct, ">50%<"), true)
' THE SCALE IS DISPLAY ONLY AND MOVES NO GEOMETRY. Without this the option is
' indistinguishable from charting a different number, which is a way to lie
' rather than a way to label. THE MARGIN IS PINNED for the comparison: a wider
' label legitimately reserves more room, and that layout response would
' otherwise read as the data having moved -- which is what the first draft of
' this check reported.
opinned = { y_scale: 100, y_suffix: "%", y_decimals: 0, margin_left: 60 }
pctp = build("line", frac, "m", ["r"], opinned)
plainp = build("line", frac, "m", ["r"], { margin_left: 60 })
check("and the plotted path is unchanged by it",
      match_all(pctp, "<path[^>]*")[0].text, match_all(plainp, "<path[^>]*")[0].text)
' THE CONTROL: the default is exactly what it was.
check("CONTROL: an unformatted axis is a bare number",
      contains(build("line", num, "t", ["v"], {}), ">4<"), true)
on error goto next
build("line", cat, "t", ["v"], { x_decimals: 2 })
check("x_decimals on a categorical axis is refused, not ignored",
      contains(error.message, "not numeric"), true)
error.clear()
check("CONTROL: x_decimals on a numeric axis is accepted",
      contains(build("scatter", num, "t", ["v"], { x_decimals: 2 }), ">1.00<"), true)
build("line", cat, "t", ["v"], { x_min: 0 })
check("x_min on a categorical axis is refused too", contains(error.message, "categorical"), true)
error.clear()
on error stop

print ""
print "-- MARKS THAT SAY WHAT THEY ARE"
grp = { m: ["North", "South"], rev: [4.0, 8.0], cost: [2.0, 3.0] }
plainbar = build("bar", grp, "m", ["rev", "cost"], {})
keyed = build("bar", grp, "m", ["rev", "cost"], { mark_keys: true })
' OFF BY DEFAULT, and asserted as a byte comparison rather than an absence:
' the core stays static and JS-free, and every existing golden is untouched.
check("the option is off by default", contains(plainbar, "data-"), false)
check("and turning it on changes only the marks",
      len(keyed) > len(plainbar) and marks(keyed, "<rect") = marks(plainbar, "<rect"), true)
check("every bar names its series and its category",
      marks(keyed, "data-series=\"rev\" data-key=\"North\""), 1)
check("including the second series", marks(keyed, "data-series=\"cost\" data-key=\"South\""), 1)
' THE LEGEND SWATCH IS NOT A DATA MARK. gdash named this exactly: matching on
' DOM order is "true until a legend adds swatch rects", and it is the reason
' an attribute is needed at all -- so a swatch carrying one would reintroduce
' the ambiguity it removes.
check("four bars carry an attribute", marks(keyed, "<rect[^>]*data-"), 4)
check("and six rects are drawn in all", marks(keyed, "<rect"), 6)
check("so the two legend swatches carry nothing",
      marks(keyed, "<rect") - marks(keyed, "<rect[^>]*data-"), 2)
' Every other data mark.
pieout = build("pie", grp, "m", ["rev"], { mark_keys: true })
check("a pie slice names its category", marks(pieout, "<path[^>]*data-key=\"North\""), 1)
lineout = build("line", grp, "m", ["rev"], { mark_keys: true, markers: true })
check("a line path names its series", marks(lineout, "<path[^>]*data-series=\"rev\""), 1)
check("and a point names the x it sits on", marks(lineout, "<circle[^>]*data-key=\"South\""), 1)
hspec = chart.spec("heatmap", { rows: ["r1"], cols: ["c1", "c2"], matrix: [[1.0, 2.0]] })
hm = chart.render(chart.options(hspec, { mark_keys: true }))
check("a heatmap cell names its row and column", marks(hm, "data-series=\"c2\" data-key=\"r1\""), 1)
hs = build("histogram", { v: [1.0, 2.0, 3.0, 4.0] }, "v", [], { mark_keys: true, bins: 2 })
check("a histogram bar names its bin", marks(hs, "<rect[^>]*data-key=") > 0, true)
' A CATEGORY IS AUTHOR DATA AND GOES INTO AN ATTRIBUTE, so it is escaped where
' it lands -- an unescaped quote would end the attribute and the rest of the
' element would become markup.
hostile = build("bar", { m: ["a\"b<c&d"], v: [1.0] }, "m", ["v"], { mark_keys: true })
check("a hostile category is escaped in the attribute",
      contains(hostile, "data-key=\"a&quot;b&lt;c&amp;d\""), true)

print ""
print "-- an unknown option is refused BY NAME"
' `options` used to merge whatever record it was given, so `ylabel` instead of
' `y_label` was accepted and did nothing -- the same silent shape as a
' categorical x drawing an empty chart, and reported in the same breath.
on error goto next
chart.options(chart.spec("line", num), { ylabel: "x" })
check("a near-miss names the option meant", contains(error.message, "did you mean 'y_label'"), true)
error.clear()
chart.options(chart.spec("line", num), { nonsense: 1 })
check("and one that is nobody's typo is still refused", contains(error.message, "no option 'nonsense'"), true)
error.clear()
' THE STRUCTURAL HALF: a spec whose `opts` were written directly rather than
' through `options` must not smuggle a typo past.
chart.render({ kind: "line", df: num, x: "t", y: ["v"], opts: { xmin: 0 } })
check("a hand-built spec is checked too", contains(error.message, "did you mean 'x_min'"), true)
error.clear()
on error stop
' THE CONTROL, without which "unknown options are refused" is satisfied by a
' library that refuses every option there is.
check("CONTROL: a real option is still accepted",
      contains(build("line", num, "t", ["v"], { title: "ok", grid: false }), ">ok<"), true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
