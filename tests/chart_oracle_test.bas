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
end program
