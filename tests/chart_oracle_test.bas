' The chart ORACLE (chart_design.md §9): under FIXED margins and FIXED axis
' bounds, every pixel coordinate is hand-computable, so this test asserts
' literals computed on paper — not values the renderer itself produced. A
' golden records whatever we emit; this tier knows what we SHOULD emit.
'
' Setup: 300x200, margins l=40 r=10 t=10 b=20, x in [0,10], y in [0,100].
' Plot box: x 40..290 (250 px), y 10..180 (170 px).
'   (0,0)    -> cx = 40,              cy = 180
'   (5,50)   -> cx = 40 + 125 = 165,  cy = 180 - 85 = 95
'   (10,100) -> cx = 290,             cy = 10
' X ticks: raw step 10/8 = 1.25 -> nice 2 -> 0,2,4,6,8,10.
' Y ticks: raw step 100/6 = 16.67 -> nice 20 -> 0,20,...,100.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
    end if
    return 0
end function

program main(args)
    load chart from "../stdlib/chart.bas"

    df = { x: [0, 5, 10], y: [0, 50, 100] }
    s = chart.spec("scatter", df)
    s = chart.x(s, "x")
    s = chart.y(s, "y")
    s = chart.options(s, {
        width: 300, height: 200,
        margin_left: 40, margin_right: 10, margin_top: 10, margin_bottom: 20,
        x_min: 0, x_max: 10, y_min: 0, y_max: 100,
        grid: false, legend: false
    })
    svg = chart.render(s)

    q = chr(34)
    x = check("point (0,0) cx      ", contains(svg, "cx=" + q + "40" + q), true)
    x = check("point (0,0) cy      ", contains(svg, "cy=" + q + "180" + q), true)
    x = check("point (5,50) cx     ", contains(svg, "cx=" + q + "165" + q), true)
    x = check("point (5,50) cy     ", contains(svg, "cy=" + q + "95" + q), true)
    x = check("point (10,100) cx   ", contains(svg, "cx=" + q + "290" + q), true)
    x = check("point (10,100) cy   ", contains(svg, "cy=" + q + "10" + q), true)
    x = check("x tick 4 labeled    ", contains(svg, ">4</text>"), true)
    x = check("y tick 60 labeled   ", contains(svg, ">60</text>"), true)
    x = check("no tick 3 (step is 2)", contains(svg, ">3</text>"), false)

    ' Determinism: the same spec renders the identical string.
    x = check("render twice equal  ", chart.render(s) = svg, true)

    ' The estimate never leaks into alignment: y labels are anchored at the
    ' axis minus a fixed pad, whatever their length.
    x = check("y labels anchor end ", contains(svg, "text-anchor=" + q + "end" + q), true)

    ' --- BAR band math (Phase 2), computed on paper -------------------------
    ' 300x200, margins l=40 r=10 t=10 b=20 -> plot x 40..290 (250), y 10..180.
    ' 2 categories -> band 125, pad 12.5, inner 100; 2 grouped series -> 50
    ' each. Category 0: series 0 at x=52.5, series 1 at x=102.5; category 1
    ' series 0 at x=177.5. y fixed 0..100: v=50 -> top 95, height 85 (bars
    ' anchor at zero, sy0=180).
    bf = { c: ["A", "B"], u: [50, 100], v: [25, 75] }
    b = chart.spec("bar", bf)
    b = chart.x(b, "c")
    b = chart.y(b, ["u", "v"])
    b = chart.options(b, {
        width: 300, height: 200,
        margin_left: 40, margin_right: 10, margin_top: 10, margin_bottom: 20,
        y_min: 0, y_max: 100, grid: false, legend: false
    })
    bs = chart.render(b)
    x = check("bar band c0 s0 x    ", contains(bs, "x=" + q + "52.5" + q), true)
    x = check("bar band c0 s1 x    ", contains(bs, "x=" + q + "102.5" + q), true)
    x = check("bar band c1 s0 x    ", contains(bs, "x=" + q + "177.5" + q), true)
    x = check("bar v=50 top        ", contains(bs, "y=" + q + "95" + q + " width=" + q + "50" + q + " height=" + q + "85" + q), true)
    x = check("bar v=100 full      ", contains(bs, "y=" + q + "10" + q + " width=" + q + "50" + q + " height=" + q + "170" + q), true)

    ' --- HISTOGRAM bins (Phase 2), computed on paper ------------------------
    ' values [0,1,2,3], bins 2 -> lo 0, hi 3, width 1.5, counts [2,2].
    ' x ticks: raw 3/8 -> nice 0.5 -> axis 0..3, so sx(0)=40, sx(1.5)=165,
    ' sx(3)=290. y ticks whole: 0..2 step 1, sy(2)=10. Both rects are
    ' 125 wide, 170 tall, tops at y=10.
    h = chart.spec("histogram", { r: [0, 1, 2, 3] })
    h = chart.x(h, "r")
    h = chart.options(h, {
        width: 300, height: 200, bins: 2,
        margin_left: 40, margin_right: 10, margin_top: 10, margin_bottom: 20,
        grid: false
    })
    hs = chart.render(h)
    x = check("hist bin0 rect      ", contains(hs, "x=" + q + "40" + q + " y=" + q + "10" + q + " width=" + q + "125" + q + " height=" + q + "170" + q), true)
    x = check("hist bin1 rect      ", contains(hs, "x=" + q + "165" + q + " y=" + q + "10" + q + " width=" + q + "125" + q + " height=" + q + "170" + q), true)
    x = check("hist count axis 2   ", contains(hs, ">2</text>"), true)

    ' --- PIE cardinals (Phase 4), no trig trusted ---------------------------
    ' 300x200, margin_top fixed 20 -> cy = 20 + (200-20-12)/2 = 104, r =
    ' 84*0.85 = 71.4 (fits: half-width 138). Four equal shares from 12
    ' o'clock: the arc endpoints are the CARDINAL points -- top (150,32.6),
    ' right (221.4,104), bottom (150,175.4), left (78.6,104) -- computable
    ' with no trigonometry at all, so this asserts the Taylor sine from
    ' outside it.
    ps = chart.spec("pie", { k: ["a", "b", "c", "d"], v: [1, 1, 1, 1] })
    ps = chart.x(ps, "k")
    ps = chart.y(ps, "v")
    ps = chart.options(ps, { width: 300, height: 200, margin_top: 20 })
    pv = chart.render(ps)
    x = check("pie top cardinal    ", contains(pv, "L150 32.6 "), true)
    x = check("pie right cardinal  ", contains(pv, " 221.4 104 "), true)
    x = check("pie bottom cardinal ", contains(pv, " 150 175.4 "), true)
    x = check("pie left cardinal   ", contains(pv, " 78.6 104 "), true)
    x = check("pie legend share    ", contains(pv, "a 25%"), true)

    ' --- HEATMAP cells + colors (Phase 4), hand-lerped ----------------------
    ' 300x200, margins l=40 r=10 t=20 b=10, 2x2 -> cells 125x85 at x 40/165,
    ' y 20/105. Domain fixed 0..1: t=0 -> the exact lo stop #2166ac; t=1 ->
    ' #b2182b; t=0.5 -> the exact mid #f7f7f7; t=0.25 -> halfway lo..mid,
    ' hand-computed byte by byte: #8cafd2.
    hm = chart.spec("heatmap", { rows: ["r1", "r2"], cols: ["c1", "c2"],
                                 matrix: [[0, 0.5], [0.25, 1]] })
    hm = chart.options(hm, { width: 300, height: 200, heat_min: 0, heat_max: 1,
                             margin_left: 40, margin_right: 10,
                             margin_top: 20, margin_bottom: 10 })
    hv = chart.render(hm)
    x = check("heat cell(0,0) rect ", contains(hv, "x=" + q + "40" + q + " y=" + q + "20" + q + " width=" + q + "125" + q + " height=" + q + "85" + q), true)
    x = check("heat cell(1,1) rect ", contains(hv, "x=" + q + "165" + q + " y=" + q + "105" + q), true)
    x = check("heat t=0 lo color   ", contains(hv, "#2166ac"), true)
    x = check("heat t=1 hi color   ", contains(hv, "#b2182b"), true)
    x = check("heat t=0.5 mid color", contains(hv, "#f7f7f7"), true)
    x = check("heat t=0.25 lerped  ", contains(hv, "#8cafd2"), true)

    ' --- SPARKLINE endpoints (Phase 4), two-point arithmetic ----------------
    ' 120x24, pad 2: [0,10] -> M2 22 (v=0 at the bottom) to L118 2 (v=10 top).
    sv = chart.sparkline([0, 10])
    x = check("sparkline path      ", contains(sv, "M2 22 L118 2"), true)

    ' --- AREA polygon (Phase 4), zero-anchored ------------------------------
    ' Same fixed frame as the scatter oracle: x 0..10 -> 40..290, y 0..100 ->
    ' 180..10. area_xy of (0,0) and (10,100): the fill polygon runs from the
    ' baseline at (40,180) up to (290,10) and closes at (290,180).
    af = { x: [0, 10], y: [0, 100] }
    aspec = chart.spec("area", af)
    aspec = chart.x(aspec, "x")
    aspec = chart.y(aspec, "y")
    aspec = chart.options(aspec, {
        width: 300, height: 200,
        margin_left: 40, margin_right: 10, margin_top: 10, margin_bottom: 20,
        x_min: 0, x_max: 10, y_min: 0, y_max: 100, grid: false, legend: false
    })
    av = chart.render(aspec)
    x = check("area polygon closes ", contains(av, "L290 10 L290 180 Z"), true)
    x = check("area is translucent ", contains(av, "fill-opacity"), true)
end program
