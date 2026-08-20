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
end program
