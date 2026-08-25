# The gBASIC chart cookbook

Ten recipes for `stdlib/chart.bas` — charts as **deterministic SVG text**.
Every code block below is a real program in `examples/chart_cookbook/`, and
every output block is its committed golden; `tests/run_chart_cookbook.sh`
fails while this page disagrees with either. The page cannot lie.

A chart here is a *string* of SVG markup: no image library, no canvas, no
renderer object. The same spec produces the identical bytes every run, which
is what makes a chart golden-file testable — and what lets you drop one into
a served page, an email, or a file with plain string handling.

The library refuses rather than guesses: a text y-column, a duplicate bar
category, a stacked bar with negatives, a pie with an unknown share — each is
an error that names its cause, because a plausible wrong picture is worse
than no picture. Design and rationale: [`chart_design.md`](chart_design.md).

## 1. Your first chart

`chart.line_xy` takes two lists and returns a complete SVG document as a
string. Write it to a file and any browser opens it; print it and a pipeline
consumes it; concatenate it into a WebServer response and it renders inline.

<!--CODE:01_first_chart-->

```basic
' Recipe 1 — A chart is a STRING. One call, one complete SVG document.
'
' No image library, no canvas object, no renderer to configure: chart.line_xy
' takes two lists and returns SVG markup as an ordinary string. Print it,
' serve it from a WebServer handler, or write it to a file and open it in any
' browser. The same call produces the identical bytes every run -- charts are
' golden-file testable like everything else.

program main(args)
    load chart from "../../stdlib/chart.bas"

    svg = chart.line_xy([1, 2, 3, 4, 5], [3, 5, 4, 7, 6])

    print left(svg, 44)
    print string(len(svg)) + " chars of SVG in total"

    f {file}= "examples/chart_cookbook/tmp_first.svg"
    write(f, svg)
    print "saved: " + string(bytes(f)) + " bytes"
    delete(f)
end program
```

<!--OUT:01_first_chart-->

```
<svg xmlns="http://www.w3.org/2000/svg" widt
1734 chars of SVG in total
saved: 1734 bytes
```

## 2. Frames and multiple series

The primary calls take a **frame** — a record of equal-length column lists,
the same shape `frame.bas`, the statistics library, and the spreadsheet
pipeline all produce. A list of y-column names makes one series each, colored
from the Okabe–Ito palette (colorblind-safe, print-safe) with a legend. An
`unknown` cell breaks the line: a **gap**, never a silent zero.

<!--CODE:02_frames_and_series-->

```basic
' Recipe 2 — Frames first: name your columns, get multiple series.
'
' The primary calls take the same frame shape frame.bas produces: a record of
' equal-length column lists. A LIST of y-column names makes one series each,
' with its own Okabe-Ito palette color and a legend entry. An `unknown` cell
' BREAKS the line -- a gap where the data is missing, never a drop to zero
' that would invent a number. nan behaves the same way.

program main(args)
    load chart from "../../stdlib/chart.bas"

    quarters = {
        q: [1, 2, 3, 4, 5, 6],
        revenue: [1200, 1500, unknown, 2100, 2400, 2650],
        cost: [900, 950, 1000, 1100, 1150, 1300]
    }
    svg = chart.line(quarters, "q", ["revenue", "cost"])

    ' the gap is real: the revenue path RESTARTS with a mid-path M command,
    ' which only a break produces
    print "two series:  " + string(contains(svg, ">cost</text>"))
    print "gap in path: " + string(contains(svg, " M"))
    print string(len(svg)) + " chars"
end program
```

<!--OUT:02_frames_and_series-->

```
two series:  true
gap in path: true
2036 chars
```

## 3. Dates on the x-axis

Datetime columns plot on a day scale and the tick labels come back as real
dates. Mixing dates and numbers in one x column is refused.

<!--CODE:03_dates_on_the_axis-->

```basic
' Recipe 3 — Dates on the x-axis label as DATES, not day numbers.
'
' A datetime column plots via core duration arithmetic (d - anchor is an
' exact duration since the redesign) and the tick labels come back through
' the datetime renderer, so the axis reads 2026-02-10, never 10 or 46063.

program main(args)
    load chart from "../../stdlib/chart.bas"

    d1 {date}= "2026-01-31"
    trend = {
        day: [d1, d1 + 14 days, d1 + 28 days, d1 + 42 days],
        users: [120, 180, 260, 410]
    }
    svg = chart.line(trend, "day", "users")

    print "labels are dates: " + string(contains(svg, ">2026-02-10</text>"))
    print string(len(svg)) + " chars"
end program
```

<!--OUT:03_dates_on_the_axis-->

```
labels are dates: true
1473 chars
```

## 4. Money at face value

Money values chart directly — the frames the xlsx pipeline emits are full of
them — and axis labels get thousands separators from the library's one
deterministic number formatter.

<!--CODE:04_money_at_face_value-->

```basic
' Recipe 4 — Money plots at face value; the axis gets thousands separators.
'
' Frames that come out of the spreadsheet pipeline carry real money values.
' A chart accepts them directly -- no conversion dance -- and the axis
' formatter writes 1,250 rather than 1250. (Text columns are still refused:
' a plausible wrong picture is worse than an error.)

program main(args)
    load chart from "../../stdlib/chart.bas"

    a{USD}= 1200.50
    b{USD}= 1350.25
    c{USD}= 1180.75
    balances = { month: [1, 2, 3], balance: [a, b, c] }
    svg = chart.line(balances, "month", "balance")

    print "separated axis label: " + string(contains(svg, ">1,250</text>"))
    print string(len(svg)) + " chars"
end program
```

<!--OUT:04_money_at_face_value-->

```
separated axis label: true
1889 chars
```

## 5. Bars, grouped and stacked

Bars anchor at zero — always. Grouped bars handle negatives by drawing
downward; stacked bars refuse them; a duplicated category is refused by name.

<!--CODE:05_bars-->

```basic
' Recipe 5 — Bars anchor at zero; grouped and stacked are one option apart.
'
' A bar's length is its statement, so the y-axis ALWAYS includes zero --
' clipping the baseline lies. Grouped bars may go negative (drawn downward);
' stacked bars with negatives are refused outright, because a naive stack
' overlaps into nonsense that looks fine at a glance. A duplicate category
' is refused by name: aggregation belongs to the frame tooling, and a chart
' inventing a sum is a wrong picture.

program main(args)
    load chart from "../../stdlib/chart.bas"

    eps = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        earned: [1.2, 1.5, -0.4, 1.75],
        paid: [0.5, 0.5, 0.5, 0.6]
    }
    grouped = chart.bar(eps, "q", ["earned", "paid"])
    print "grouped: " + string(len(grouped)) + " chars"

    up = {
        q: ["Q1", "Q2", "Q3", "Q4"],
        earned: [1.2, 1.5, 0.4, 1.75],
        paid: [0.5, 0.5, 0.5, 0.6]
    }
    s = chart.spec("bar", up)
    s = chart.x(s, "q")
    s = chart.y(s, ["earned", "paid"])
    s = chart.options(s, { stacked: true })
    stacked = chart.render(s)
    print "stacked: " + string(len(stacked)) + " chars"
end program
```

<!--OUT:05_bars-->

```
grouped: 2251 chars
stacked: 2272 chars
```

## 6. Histograms

One value column, equal-width bins, a count axis that only labels whole
numbers. `bins:` fixes the bin count; the default is `ceil(sqrt(n))`.

<!--CODE:06_histogram-->

```basic
' Recipe 6 — A histogram bins one value column; the count axis stays whole.
'
' Bin count is the bins: option or ceil(sqrt(n)) -- deterministic either
' way. unknown and nan observations are skipped. The count axis only ever
' labels whole numbers, because half an observation is not a thing.

program main(args)
    load chart from "../../stdlib/chart.bas"

    returns = [0.1, 0.3, 0.2, 0.25, -0.1, 0.15, 0.4, 0.18, 0.22, unknown]
    svg = chart.histogram_xy(returns)
    print "auto bins: " + string(len(svg)) + " chars"

    h = chart.spec("histogram", { r: returns })
    h = chart.x(h, "r")
    h = chart.options(h, { bins: 4 })
    print "fixed bins: " + string(len(chart.render(h))) + " chars"
end program
```

<!--OUT:06_histogram-->

```
auto bins: 1918 chars
fixed bins: 2017 chars
```

## 7. Pie charts

Shares of a whole, from 12 o'clock clockwise in row order, percentages
computed into the legend. The refusals are the point: a negative share is
nonsense and an unknown share misstates every *other* share, so both raise.

<!--CODE:07_pie-->

```basic
' Recipe 7 — A pie shows shares of a whole, which is exactly why it refuses.
'
' A NEGATIVE share is nonsense, and an UNKNOWN share silently misstates
' every other share -- so unlike a line (where a gap is honest absence),
' a pie refuses both, by name. Slices run clockwise from 12 o'clock in row
' order, and the legend always carries the percentages, computed for you.

program main(args)
    load chart from "../../stdlib/chart.bas"

    spend = { dept: ["Eng", "Sales", "Ops"], budget: [6, 3, 1] }
    svg = chart.pie(spend, "dept", "budget")

    print "legend shares: " + string(contains(svg, "Eng 60%")) + " " + string(contains(svg, "Ops 10%"))
    print string(len(svg)) + " chars"
end program
```

<!--OUT:07_pie-->

```
legend shares: true true
815 chars
```

## 8. Heatmaps and correlation matrices

`chart.heatmap(labels, matrix)` colors a square matrix on a diverging
blue–white–red scale. Fix the domain with `heat_min`/`heat_max` so equal
colors mean equal values across every chart you make — a correlation matrix
wants −1..1 regardless of the sample. Unknown cells render gray with no
number. (`chart.heatmap_grid` takes separate row and column labels for the
rectangular case.)

<!--CODE:08_heatmap-->

```basic
' Recipe 8 — A heatmap for the correlation-matrix shape.
'
' chart.heatmap(labels, matrix) colors a square matrix on a diverging
' blue-white-red scale. Fix the domain with heat_min/heat_max -- a
' correlation matrix wants -1..1 regardless of what this sample happened to
' produce, so equal colors mean equal correlations across every chart you
' make. An unknown cell renders GRAY with no number: missing shown as
' missing, never as a value.

program main(args)
    load chart from "../../stdlib/chart.bas"

    names = ["price", "volume", "spread"]
    m = [
        [1,       0.62,    -0.18],
        [0.62,    1,       unknown],
        [-0.18,   unknown, 1]
    ]
    h = chart.spec("heatmap", { rows: names, cols: names, matrix: m })
    h = chart.options(h, { heat_min: -1, heat_max: 1 })
    svg = chart.render(h)

    print "diagonal is the hot stop: " + string(contains(svg, "#b2182b"))
    print "missing cells are gray:   " + string(contains(svg, "#eeeeee"))
    print string(len(svg)) + " chars"
end program
```

<!--OUT:08_heatmap-->

```
diagonal is the hot stop: true
missing cells are gray:   true
2148 chars
```

## 9. Sparklines

A 120×24 axis-less line for table cells and dossiers — small enough that the
whole SVG fits in the output block below.

<!--CODE:09_sparklines-->

```basic
' Recipe 9 — Sparklines: the shape alone, small enough for a table cell.
'
' No axes, no labels, no margins -- a 120x24 line whose only job is to show
' the trend inline. Gaps behave exactly as they do everywhere else.

program main(args)
    load chart from "../../stdlib/chart.bas"

    week = [3, 1, 4, unknown, 5, 9, 2]
    svg = chart.sparkline(week)
    print svg
end program
```

<!--OUT:09_sparklines-->

```
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="24" viewBox="0 0 120 24"><path d="M2 17 L21.33 22 L40.67 14.5 M79.33 12 L98.67 2 L118 19.5" fill="none" stroke="#000000" stroke-width="1.5"/></svg>
```

## 10. The spec layer

The one-liners are wrappers over a **spec record** you can build in steps and
inspect like any other data — titles, fixed bounds for cross-chart
comparability, your own palette, size, markers — then `chart.render(spec)`
for the fragment or `chart.page(spec)` for a standalone HTML document. (The
constructor is `chart.spec`: `new` is a reserved word a library function
cannot be named.)

<!--CODE:10_the_spec_layer-->

```basic
' Recipe 10 — The spec layer: a chart is a record you build up and inspect.
'
' The one-liners are wrappers over spec + render. Build the spec in steps
' when you want titles, fixed bounds (so two dossiers compare on the same
' scale), your own palette, or the chart.page() HTML wrapper for
' save-and-open. The constructor is chart.spec -- `new` is a reserved word
' a library function cannot be named.

program main(args)
    load chart from "../../stdlib/chart.bas"

    df = { m: [1, 2, 3, 4], actual: [10, 14, 9, 16], plan: [12, 12, 12, 12] }
    s = chart.spec("line", df)
    s = chart.x(s, "m")
    s = chart.y(s, ["actual", "plan"])
    s = chart.title(s, "Actual vs plan")
    s = chart.size(s, 480, 240)
    s = chart.options(s, { y_min: 0, y_max: 20, markers: true })

    print "spec is data: kind=" + s.kind + " x=" + s.x + " series=" + string(count(s.y))
    svg = chart.render(s)
    print "title present: " + string(contains(svg, ">Actual vs plan</text>"))

    html = chart.page(s)
    print left(html, 15) + " ... " + string(len(html)) + " chars of standalone page"
end program
```

<!--OUT:10_the_spec_layer-->

```
spec is data: kind=line x=m series=2
title present: true
<!DOCTYPE html> ... 2499 chars of standalone page
```

## Where the pictures come from

Everything upstream already exists in the same tree: `xlsx` reads real
workbooks, `grid`/`consolidate` clean them, `frame` reshapes, `stats`
computes — and now any of those results is one call from a picture. The full
option table and the text-layout policy (exact alignment via `text-anchor`,
estimated space reservation with declared knobs) are in
[`chart_design.md`](chart_design.md) §6b–§7.
