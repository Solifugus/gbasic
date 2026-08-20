' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' chart.bas — charts as deterministic SVG text (chart_design.md, Phase 1).
'
' A chart is a STRING of SVG markup, produced by pure gBASIC arithmetic and
' string assembly: no image library, no C, no timestamps, no randomness — the
' same spec renders the identical string every run, so a chart is golden-file
' testable like everything else in the tree.
'
' Phase 1 surface: line and scatter (single and multi-series), frames first
' (a record of equal-length columns, unknown = missing) with *_xy list forms
' as the escape hatch. The constructor is `chart.spec`, NOT `chart.new` —
' a library cannot DEFINE a function named `new` (keyword), proven at design
' review (chart_design.md §13).
'
' Layout policy (§6b): alignment is EXACT via text-anchor — the renderer
' aligns, this library never needs to know where text ends. Space RESERVATION
' is a declared estimate (len * char_ratio * font_size) with every knob in
' the options record and every margin individually overridable.
'
' Refusals (§8): a plausible wrong picture is worse than an error. Non-numeric
' y columns, more series than palette colors, and unequal column lengths are
' refused BY NAME. unknown and nan break a line (a gap, never a zero) and are
' skipped in scatter. Unsorted x plots in row order: your order is your
' statement.
library chart

    ' ------------------------------------------------------------- utilities

    ' Every string of user origin passes through here at the point it enters
    ' the markup. This is what makes "no inline JavaScript" true rather than
    ' aspirational when the SVG lands in a served page.
    function _escape(text)
        s = string(text)
        s = replace(s, "&", "&amp;")
        s = replace(s, "<", "&lt;")
        s = replace(s, ">", "&gt;")
        s = replace(s, chr(34), "&quot;")
        s = replace(s, "'", "&apos;")
        return s
    end function

    ' Fixed-rule number formatting: `d` decimal places (trailing zeros
    ' trimmed), thousands separators only when `sep` is true. All numbers in
    ' the SVG go through here — one formatter, no locale, no drift.
    function _fmt(v, d, sep)
        neg = v < 0
        a = v
        if neg then a = 0 - v
        p = pow(10, d)
        scaled = round(a * p, 0)
        ip = floor(scaled / p)
        fp = scaled - (ip * p)
        s = string(ip)
        if sep then
            grouped = ""
            while len(s) > 3
                grouped = "," + right(s, 3) + grouped
                s = left(s, len(s) - 3)
            end while
            s = s + grouped
        end if
        if d > 0 and fp > 0 then
            fs = string(fp)
            while len(fs) < d
                fs = "0" + fs
            end while
            while ends_with(fs, "0")
                fs = left(fs, len(fs) - 1)
            end while
            s = s + "." + fs
        end if
        if neg and scaled > 0 then s = "-" + s
        return s
    end function

    ' SVG coordinates: two decimals, no separators.
    function _coord(v)
        return _fmt(v, 2, false)
    end function

    ' Estimated text width (§6b): reservation only — never used for alignment.
    function _estw(text, opts)
        return len(string(text)) * opts.char_ratio * opts.font_size
    end function

    ' A value that can be plotted: numbers and money plot at face value; a nan
    ' or infinity is treated exactly like unknown (a gap is honest, a spike to
    ' infinity is not a picture).
    function _plottable(v)
        if is_unknown(v) then return unknown
        t = type(v)
        if t = "money" then return number(string(v))
        if t != "number" then return unknown
        if v != v then return unknown
        if v = number("inf") or v = 0 - number("inf") then return unknown
        return v
    end function

    ' -------------------------------------------------- trig + color (Phase 4)

    function _pi()
        return 3.141592653589793
    end function

    ' The interpreter has no trig builtins, so pie slices use a Taylor sine:
    ' range-reduced to [-pi, pi], nine terms — beyond double precision there,
    ' and DETERMINISTIC (same arithmetic every run, which is all a golden
    ' needs). The oracle tier does not trust this: equal quarter-slices put
    ' arc endpoints on exact cardinal points, hand-computable with no trig.
    function _sin(v)
        two_pi = 2 * _pi()
        k = round(v / two_pi, 0)
        v = v - (k * two_pi)
        term = v
        total = v
        i = 1
        while i <= 9
            term = term * (0 - (v * v)) / ((2 * i) * ((2 * i) + 1))
            total = total + term
            i = i + 1
        end while
        return total
    end function

    function _cos(v)
        return _sin(v + (_pi() / 2))
    end function

    ' "#rrggbb" -> [r, g, b]
    function _rgb(c)
        h = replace(string(c), "#", "")
        b = hex_decode(h)
        return [byte_at(b, 0), byte_at(b, 1), byte_at(b, 2)]
    end function

    ' Three-stop linear interpolation (lo -> mid -> hi) at t in [0,1],
    ' returned as "#rrggbb". Integer rounding keeps it deterministic.
    function _lerp3(lo, mid, hi, t)
        a = _rgb(lo)
        b = _rgb(mid)
        tt = t * 2
        if t > 0.5 then
            a = _rgb(mid)
            b = _rgb(hi)
            tt = (t - 0.5) * 2
        end if
        r = round(a[0] + ((b[0] - a[0]) * tt), 0)
        g = round(a[1] + ((b[1] - a[1]) * tt), 0)
        bl = round(a[2] + ((b[2] - a[2]) * tt), 0)
        return "#" + hex_encode(from_bytes([r, g, bl]))
    end function

    ' ------------------------------------------------------------ nice ticks

    function _nice_step(raw)
        mag = pow(10, floor(log10(raw)))
        norm = raw / mag
        stp = 10
        if norm <= 1 then
            stp = 1
        else
            if norm <= 2 then
                stp = 2
            else
                if norm <= 2.5 then
                    stp = 2.5
                else
                    if norm <= 5 then stp = 5
                end if
            end if
        end if
        return stp * mag
    end function

    ' Loose labeling over [lo, hi]: axis bounds extended to multiples of a
    ' nice step, at most maxn+1 labels after deterministic thinning. When
    ' whole is true the step is forced to an integer (date axes label whole
    ' days). Ticks are computed multiplicatively (t0 + i*step), never by
    ' accumulation, so there is no floating drift between runs.
    function _ticks(lo, hi, maxn, whole)
        if lo = hi then
            lo = lo - 1
            hi = hi + 1
        end if
        raw = (hi - lo) / maxn
        stp = _nice_step(raw)
        if whole and stp < 1 then stp = 1
        axlo = floor(lo / stp) * stp
        axhi = ceil(hi / stp) * stp
        n = round((axhi - axlo) / stp, 0) + 1
        keep = 1
        if n > maxn + 1 then keep = ceil(n / (maxn + 1))
        ticks = []
        i = 0
        while i < n
            append(ticks, axlo + (i * stp))
            i = i + keep
        end while
        decimals = 0
        if stp != floor(stp) then
            decimals = 1
            if (stp * 10) != floor(stp * 10) then decimals = 2
        end if
        return { lo: axlo, hi: axhi, ticks: ticks, stp: stp, decimals: decimals }
    end function

    ' --------------------------------------------------------------- options

    function _defaults()
        return {
            width: 640, height: 360,
            title: unknown, x_label: unknown, y_label: unknown,
            palette: ["#000000", "#E69F00", "#56B4E9", "#009E73",
                      "#F0E442", "#0072B2", "#D55E00", "#CC79A7"],
            grid: true, legend: unknown, markers: false,
            stacked: false, bins: unknown,
            heat_min: unknown, heat_max: unknown,
            heat_lo: "#2166ac", heat_mid: "#f7f7f7", heat_hi: "#b2182b",
            cell_values: true,
            x_min: unknown, x_max: unknown, y_min: unknown, y_max: unknown,
            font_size: 12, char_ratio: 0.6,
            margin_left: unknown, margin_right: unknown,
            margin_top: unknown, margin_bottom: unknown,
            max_ticks_x: 8, max_ticks_y: 6
        }
    end function

    function _merge(base, over)
        out = base
        for each k in keys(over)
            out[k] = over[k]
        end for
        return out
    end function

    ' ------------------------------------------------------------ spec layer

    ' The constructor. (Named `spec`, not `new`: a library cannot define a
    ' function called `new` — see the design doc.)
    function spec(kind, df)
        ok = kind = "line" or kind = "scatter" or kind = "bar" or kind = "histogram"
        if kind = "area" or kind = "pie" or kind = "heatmap" or kind = "sparkline" then ok = true
        if not ok then
            error "chart: unsupported kind '" + kind + "' (line, scatter, area, bar, histogram, pie, heatmap, sparkline)"
        end if
        return { kind: kind, df: df, x: unknown, y: [], opts: {} }
    end function

    function x(s, name)
        out = s
        out.x = name
        return out
    end function

    function y(s, names)
        out = s
        if is_array(names) then
            out.y = names
        else
            out.y = [names]
        end if
        return out
    end function

    function title(s, text)
        out = s
        out.opts = _merge(out.opts, { title: text })
        return out
    end function

    function size(s, w, h)
        out = s
        out.opts = _merge(out.opts, { width: w, height: h })
        return out
    end function

    function options(s, rec)
        out = s
        out.opts = _merge(out.opts, rec)
        return out
    end function

    ' ------------------------------------------------------------ validation

    function _column(df, name, nrows)
        if not has(df, name) then
            error "chart: no column '" + name + "' in the frame"
        end if
        col = df[name]
        if not is_array(col) then
            error "chart: column '" + name + "' is not a list"
        end if
        if count(col) != nrows then
            error "chart: column '" + name + "' has " + string(count(col)) + " rows; the x column has " + string(nrows)
        end if
        return col
    end function

    ' ---------------------------------------------------------------- render

    function render(s)
        opts = _merge(_defaults(), s.opts)

        if s.kind = "heatmap" then
            return _render_heatmap(s, opts)
        end if
        if is_unknown(s.x) then
            error "chart: no x column set"
        end if
        if s.kind = "sparkline" then
            return _render_sparkline(s, opts)
        end if
        if s.kind = "pie" then
            return _render_pie(s, opts)
        end if
        if s.kind = "bar" then
            return _render_bar(s, opts)
        end if
        if s.kind = "histogram" then
            return _render_hist(s, opts)
        end if
        if count(s.y) = 0 then
            error "chart: no y column set"
        end if
        if count(s.y) > count(opts.palette) then
            error "chart: " + string(count(s.y)) + " series but the palette has " + string(count(opts.palette)) + " colors; pass a longer palette: option"
        end if

        if not has(s.df, s.x) then
            error "chart: no column '" + s.x + "' in the frame"
        end if
        xcol = s.df[s.x]
        nrows = count(xcol)

        ' X values: numeric, or datetimes reduced to day counts from the
        ' earliest date (core duration arithmetic — no private tricks).
        is_date = false
        anchor = unknown
        for each v in xcol
            if not is_unknown(v) then
                if type(v) = "datetime" then
                    is_date = true
                    ' Block form on purpose: an inline `if` immediately before
                    ' an `else` line captures it (nearest-unmatched rule).
                    if is_unknown(anchor) or v < anchor then
                        anchor = v
                    end if
                else
                    if is_date then
                        error "chart: column '" + s.x + "' mixes dates and numbers"
                    end if
                end if
            end if
        end for

        xs = []
        for each v in xcol
            if is_unknown(v) then
                append(xs, unknown)
            else
                if is_date then
                    if type(v) != "datetime" then
                        error "chart: column '" + s.x + "' mixes dates and numbers"
                    end if
                    dur = v - anchor
                    append(xs, dur.total_seconds / 86400)
                else
                    append(xs, _plottable(v))
                end if
            end if
        end for

        ' Y series, validated by name.
        series = []
        for each name in s.y
            col = _column(s.df, name, nrows)
            vals = []
            i = 0
            while i < nrows
                v = col[i]
                if not is_unknown(v) then
                    t = type(v)
                    if t != "number" and t != "money" then
                        error "chart: column '" + name + "' holds " + t + " values; charts plot numbers (categories belong on a bar chart's x axis)"
                    end if
                end if
                append(vals, _plottable(v))
                i = i + 1
            end while
            append(series, { name: name, vals: vals })
        end for

        ' Data ranges over every finite point.
        have_data = false
        dxlo = 0
        dxhi = 1
        dylo = 0
        dyhi = 1
        for each ser in series
            i = 0
            while i < nrows
                xv = xs[i]
                yv = ser.vals[i]
                if not is_unknown(xv) and not is_unknown(yv) then
                    if not have_data then
                        dxlo = xv
                        dxhi = xv
                        dylo = yv
                        dyhi = yv
                        have_data = true
                    else
                        if xv < dxlo then dxlo = xv
                        if xv > dxhi then dxhi = xv
                        if yv < dylo then dylo = yv
                        if yv > dyhi then dyhi = yv
                    end if
                end if
                i = i + 1
            end while
        end for

        if s.kind = "area" then
            ' the fill anchors at zero, so the axis must include it
            if dylo > 0 then dylo = 0
            if dyhi < 0 then dyhi = 0
        end if
        if not is_unknown(opts.x_min) then dxlo = opts.x_min
        if not is_unknown(opts.x_max) then dxhi = opts.x_max
        if not is_unknown(opts.y_min) then dylo = opts.y_min
        if not is_unknown(opts.y_max) then dyhi = opts.y_max

        tx = _ticks(dxlo, dxhi, opts.max_ticks_x, is_date)
        ty = _ticks(dylo, dyhi, opts.max_ticks_y, false)

        ' Tick label text (dates label as real dates via anchor + days).
        xlabels = []
        for each t in tx.ticks
            if is_date then
                append(xlabels, string(anchor + (1 day) * round(t, 0)))
            else
                append(xlabels, _fmt(t, tx.decimals, true))
            end if
        end for
        ylabels = []
        for each t in ty.ticks
            append(ylabels, _fmt(t, ty.decimals, true))
        end for

        ' Margins (§6b): derived from the LONGEST formatted tick label unless
        ' explicitly overridden. Estimation reserves space; text-anchor aligns.
        ywmax = 0
        for each lbl in ylabels
            w = _estw(lbl, opts)
            if w > ywmax then ywmax = w
        end for
        show_legend = count(series) > 1
        if not is_unknown(opts.legend) then show_legend = opts.legend

        mleft = opts.margin_left
        if is_unknown(mleft) then
            mleft = ywmax + 10
            if not is_unknown(opts.y_label) then mleft = mleft + opts.font_size + 8
        end if
        mright = opts.margin_right
        if is_unknown(mright) then mright = 14
        mtop = opts.margin_top
        if is_unknown(mtop) then
            mtop = 12
            if not is_unknown(opts.title) then mtop = mtop + round(opts.font_size * 1.5, 0) + 6
            if show_legend then mtop = mtop + opts.font_size + 8
        end if
        mbottom = opts.margin_bottom
        if is_unknown(mbottom) then
            mbottom = opts.font_size + 12
            if not is_unknown(opts.x_label) then mbottom = mbottom + opts.font_size + 8
        end if

        px0 = mleft
        px1 = opts.width - mright
        py0 = mtop
        py1 = opts.height - mbottom

        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(opts.width, 0, false) + chr(34) + " height=" + chr(34) + _fmt(opts.height, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(opts.width, 0, false) + " " + _fmt(opts.height, 0, false) + chr(34) + " font-family=" + chr(34) + "sans-serif" + chr(34) + " font-size=" + chr(34) + _fmt(opts.font_size, 0, false) + chr(34) + ">")

        ' Grid (horizontal, at y ticks).
        if opts.grid then
            for each t in ty.ticks
                gy = _scale_y(t, ty, py0, py1)
                append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + gy + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + gy + chr(34) + " stroke=" + chr(34) + "#dddddd" + chr(34) + "/>")
            end for
        end if

        ' Axes.
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py1) + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py0) + chr(34) + " x2=" + chr(34) + _coord(px0) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")

        ' Tick labels: y right-aligned against the axis (text-anchor="end"),
        ' x centered under the tick (text-anchor="middle") — exact alignment,
        ' no measurement.
        i = 0
        while i < count(ty.ticks)
            gy = _scale_y(ty.ticks[i], ty, py0, py1)
            append(parts, "<text x=" + chr(34) + _coord(px0 - 6) + chr(34) + " y=" + chr(34) + gy + chr(34) + " text-anchor=" + chr(34) + "end" + chr(34) + " dominant-baseline=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(ylabels[i]) + "</text>")
            i = i + 1
        end while
        i = 0
        while i < count(tx.ticks)
            gx = _scale_x(tx.ticks[i], tx, px0, px1)
            append(parts, "<text x=" + chr(34) + gx + chr(34) + " y=" + chr(34) + _coord(py1 + opts.font_size + 4) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(xlabels[i]) + "</text>")
            i = i + 1
        end while

        ' Series.
        if have_data then
            si = 0
            for each ser in series
                color = opts.palette[si]
                if s.kind = "area" then
                    ' one translucent polygon per gap-run, anchored at zero
                    base = _scale_y(0, ty, py0, py1)
                    runx = []
                    runpts = []
                    i = 0
                    while i <= nrows
                        flush = i = nrows
                        if not flush then
                            xv = xs[i]
                            yv = ser.vals[i]
                            if is_unknown(xv) or is_unknown(yv) then
                                flush = true
                            else
                                append(runx, _scale_x(xv, tx, px0, px1))
                                append(runpts, _scale_x(xv, tx, px0, px1) + " " + _scale_y(yv, ty, py0, py1))
                            end if
                        end if
                        if flush and count(runpts) >= 2 then
                            poly = "M" + runx[0] + " " + base + " L" + join(runpts, " L") + " L" + runx[count(runx) - 1] + " " + base + " Z"
                            append(parts, "<path d=" + chr(34) + poly + chr(34) + " fill=" + chr(34) + color + chr(34) + " fill-opacity=" + chr(34) + "0.35" + chr(34) + " stroke=" + chr(34) + "none" + chr(34) + "/>")
                        end if
                        if flush then
                            runx = []
                            runpts = []
                        end if
                        i = i + 1
                    end while
                end if
                if s.kind = "line" or s.kind = "area" then
                    d = []
                    pen = false
                    i = 0
                    while i < nrows
                        xv = xs[i]
                        yv = ser.vals[i]
                        if is_unknown(xv) or is_unknown(yv) then
                            pen = false
                        else
                            cmd = "L"
                            if not pen then cmd = "M"
                            append(d, cmd + _scale_x(xv, tx, px0, px1) + " " + _scale_y(yv, ty, py0, py1))
                            pen = true
                        end if
                        i = i + 1
                    end while
                    if count(d) > 0 then
                        append(parts, "<path d=" + chr(34) + join(d, " ") + chr(34) + " fill=" + chr(34) + "none" + chr(34) + " stroke=" + chr(34) + color + chr(34) + " stroke-width=" + chr(34) + "1.5" + chr(34) + "/>")
                    end if
                end if
                if s.kind = "scatter" or opts.markers then
                    i = 0
                    while i < nrows
                        xv = xs[i]
                        yv = ser.vals[i]
                        if not is_unknown(xv) and not is_unknown(yv) then
                            append(parts, "<circle cx=" + chr(34) + _scale_x(xv, tx, px0, px1) + chr(34) + " cy=" + chr(34) + _scale_y(yv, ty, py0, py1) + chr(34) + " r=" + chr(34) + "3" + chr(34) + " fill=" + chr(34) + color + chr(34) + "/>")
                        end if
                        i = i + 1
                    end while
                end if
                si = si + 1
            end for
        else
            append(parts, "<text x=" + chr(34) + _coord((px0 + px1) / 2) + chr(34) + " y=" + chr(34) + _coord((py0 + py1) / 2) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#888888" + chr(34) + ">no data</text>")
        end if

        ' Legend: one row above the plot, a swatch and a name per series.
        if show_legend and have_data then
            lx = px0
            lyy = mtop - opts.font_size + 2
            li = 0
            for each ser in series
                color = opts.palette[li]
                li = li + 1
                append(parts, "<rect x=" + chr(34) + _coord(lx) + chr(34) + " y=" + chr(34) + _coord(lyy - 9) + chr(34) + " width=" + chr(34) + "10" + chr(34) + " height=" + chr(34) + "10" + chr(34) + " fill=" + chr(34) + color + chr(34) + "/>")
                append(parts, "<text x=" + chr(34) + _coord(lx + 14) + chr(34) + " y=" + chr(34) + _coord(lyy) + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(ser.name) + "</text>")
                lx = lx + 14 + _estw(ser.name, opts) + 16
            end for
        end if

        ' Title and axis labels.
        if not is_unknown(opts.title) then
            append(parts, "<text x=" + chr(34) + _coord(opts.width / 2) + chr(34) + " y=" + chr(34) + _coord(opts.font_size + 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " font-size=" + chr(34) + _fmt(round(opts.font_size * 1.2, 0), 0, false) + chr(34) + " fill=" + chr(34) + "#111111" + chr(34) + ">" + _escape(opts.title) + "</text>")
        end if
        if not is_unknown(opts.x_label) then
            append(parts, "<text x=" + chr(34) + _coord((px0 + px1) / 2) + chr(34) + " y=" + chr(34) + _coord(opts.height - 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.x_label) + "</text>")
        end if
        if not is_unknown(opts.y_label) then
            append(parts, "<text x=" + chr(34) + _coord(opts.font_size) + chr(34) + " y=" + chr(34) + _coord((py0 + py1) / 2) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " transform=" + chr(34) + "rotate(-90 " + _coord(opts.font_size) + " " + _coord((py0 + py1) / 2) + ")" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.y_label) + "</text>")
        end if

        append(parts, "</svg>")
        return join(parts, "")
    end function

    function _scale_x(v, tx, px0, px1)
        return _coord(px0 + ((v - tx.lo) * (px1 - px0) / (tx.hi - tx.lo)))
    end function

    function _scale_y(v, ty, py0, py1)
        return _coord(py1 - ((v - ty.lo) * (py1 - py0) / (ty.hi - ty.lo)))
    end function

    ' ------------------------------------------------------------ one-liners

    ' -------------------------------------------------------- bar (Phase 2)
    '
    ' Category x-axis: evenly spaced bands, one per category IN ROW ORDER.
    ' Bars anchor at ZERO — the y range always includes 0, because a bar's
    ' length is its statement and clipping the baseline lies. Grouped bars may
    ' go negative (drawn downward); STACKED bars with negatives are refused —
    ' a naive stack draws overlapping nonsense that looks fine at a glance.
    ' A duplicate category is refused by name: a chart inventing a sum is a
    ' wrong picture, and aggregation belongs to the frame tooling.
    function _render_bar(s, opts)
        if count(s.y) = 0 then
            error "chart: no y column set"
        end if
        if count(s.y) > count(opts.palette) then
            error "chart: " + string(count(s.y)) + " series but the palette has " + string(count(opts.palette)) + " colors; pass a longer palette: option"
        end if
        if not has(s.df, s.x) then
            error "chart: no column '" + s.x + "' in the frame"
        end if
        xcol = s.df[s.x]
        nrows = count(xcol)

        cats = []
        for each v in xcol
            if is_unknown(v) then
                error "chart: column '" + s.x + "' has an unknown category; a bar needs a name"
            end if
            key = string(v)
            if contains(cats, key) then
                error "chart: category '" + key + "' appears twice; aggregate before charting"
            end if
            append(cats, key)
        end for
        ncat = count(cats)

        stacked = opts.stacked

        series = []
        for each name in s.y
            col = _column(s.df, name, nrows)
            vals = []
            i = 0
            while i < nrows
                v = col[i]
                if not is_unknown(v) then
                    t = type(v)
                    if t != "number" and t != "money" then
                        error "chart: column '" + name + "' holds " + t + " values; charts plot numbers (categories belong on a bar chart's x axis)"
                    end if
                end if
                pv = _plottable(v)
                if stacked and not is_unknown(pv) then
                    if pv < 0 then
                        error "chart: stacked bars with negative values are refused (a naive stack overlaps); use grouped bars"
                    end if
                end if
                append(vals, pv)
                i = i + 1
            end while
            append(series, { name: name, vals: vals })
        end for

        ' Y range: always includes zero.
        dylo = 0
        dyhi = 0
        if stacked then
            i = 0
            while i < nrows
                total = 0
                for each ser in series
                    v = ser.vals[i]
                    if not is_unknown(v) then total = total + v
                end for
                if total > dyhi then dyhi = total
                i = i + 1
            end while
        else
            for each ser in series
                for each v in ser.vals
                    if not is_unknown(v) then
                        if v < dylo then dylo = v
                        if v > dyhi then dyhi = v
                    end if
                end for
            end for
        end if
        if not is_unknown(opts.y_min) then dylo = opts.y_min
        if not is_unknown(opts.y_max) then dyhi = opts.y_max
        if dylo = dyhi then dyhi = dylo + 1
        ty = _ticks(dylo, dyhi, opts.max_ticks_y, false)

        ylabels = []
        for each t in ty.ticks
            append(ylabels, _fmt(t, ty.decimals, true))
        end for
        ywmax = 0
        for each lbl in ylabels
            w = _estw(lbl, opts)
            if w > ywmax then ywmax = w
        end for
        show_legend = count(series) > 1
        if not is_unknown(opts.legend) then show_legend = opts.legend

        mleft = opts.margin_left
        if is_unknown(mleft) then
            mleft = ywmax + 10
            if not is_unknown(opts.y_label) then mleft = mleft + opts.font_size + 8
        end if
        mright = opts.margin_right
        if is_unknown(mright) then mright = 14
        mtop = opts.margin_top
        if is_unknown(mtop) then
            mtop = 12
            if not is_unknown(opts.title) then mtop = mtop + round(opts.font_size * 1.5, 0) + 6
            if show_legend then mtop = mtop + opts.font_size + 8
        end if
        mbottom = opts.margin_bottom
        if is_unknown(mbottom) then
            mbottom = opts.font_size + 12
            if not is_unknown(opts.x_label) then mbottom = mbottom + opts.font_size + 8
        end if

        px0 = mleft
        px1 = opts.width - mright
        py0 = mtop
        py1 = opts.height - mbottom

        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(opts.width, 0, false) + chr(34) + " height=" + chr(34) + _fmt(opts.height, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(opts.width, 0, false) + " " + _fmt(opts.height, 0, false) + chr(34) + " font-family=" + chr(34) + "sans-serif" + chr(34) + " font-size=" + chr(34) + _fmt(opts.font_size, 0, false) + chr(34) + ">")

        if opts.grid then
            for each t in ty.ticks
                gy = _scale_y(t, ty, py0, py1)
                append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + gy + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + gy + chr(34) + " stroke=" + chr(34) + "#dddddd" + chr(34) + "/>")
            end for
        end if
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py1) + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py0) + chr(34) + " x2=" + chr(34) + _coord(px0) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")

        i = 0
        while i < count(ty.ticks)
            gy = _scale_y(ty.ticks[i], ty, py0, py1)
            append(parts, "<text x=" + chr(34) + _coord(px0 - 6) + chr(34) + " y=" + chr(34) + gy + chr(34) + " text-anchor=" + chr(34) + "end" + chr(34) + " dominant-baseline=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(ylabels[i]) + "</text>")
            i = i + 1
        end while

        ' Category labels, centered per band, deterministically thinned.
        band = (px1 - px0) / ncat
        keep = 1
        if ncat > opts.max_ticks_x + 1 then keep = ceil(ncat / (opts.max_ticks_x + 1))
        ci = 0
        while ci < ncat
            cx = px0 + (ci * band) + (band / 2)
            append(parts, "<text x=" + chr(34) + _coord(cx) + chr(34) + " y=" + chr(34) + _coord(py1 + opts.font_size + 4) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(cats[ci]) + "</text>")
            ci = ci + keep
        end while

        ' Bars.
        sy0 = _scale_y(0, ty, py0, py1)
        inner = band * 0.8
        pad = band * 0.1
        nser = count(series)
        ci = 0
        while ci < ncat
            if stacked then
                cum = 0
                si = 0
                while si < nser
                    v = series[si].vals[ci]
                    if not is_unknown(v) then
                        if v > 0 then
                            ytop = _scale_y(cum + v, ty, py0, py1)
                            ybase = _scale_y(cum, ty, py0, py1)
                            bx = px0 + (ci * band) + pad
                            append(parts, "<rect x=" + chr(34) + _coord(bx) + chr(34) + " y=" + chr(34) + ytop + chr(34) + " width=" + chr(34) + _coord(inner) + chr(34) + " height=" + chr(34) + _coord(number(ybase) - number(ytop)) + chr(34) + " fill=" + chr(34) + opts.palette[si] + chr(34) + "/>")
                            cum = cum + v
                        end if
                    end if
                    si = si + 1
                end while
            else
                barw = inner / nser
                si = 0
                while si < nser
                    v = series[si].vals[ci]
                    if not is_unknown(v) then
                        if v != 0 then
                            yv = _scale_y(v, ty, py0, py1)
                            ry = yv
                            rh = number(sy0) - number(yv)
                            if rh < 0 then
                                ry = sy0
                                rh = 0 - rh
                            end if
                            bx = px0 + (ci * band) + pad + (si * barw)
                            append(parts, "<rect x=" + chr(34) + _coord(bx) + chr(34) + " y=" + chr(34) + string(ry) + chr(34) + " width=" + chr(34) + _coord(barw) + chr(34) + " height=" + chr(34) + _coord(rh) + chr(34) + " fill=" + chr(34) + opts.palette[si] + chr(34) + "/>")
                        end if
                    end if
                    si = si + 1
                end while
            end if
            ci = ci + 1
        end while

        ' Legend, title, axis labels — same chrome as line/scatter.
        if show_legend then
            lx = px0
            lyy = mtop - opts.font_size + 2
            li = 0
            for each ser in series
                color = opts.palette[li]
                li = li + 1
                append(parts, "<rect x=" + chr(34) + _coord(lx) + chr(34) + " y=" + chr(34) + _coord(lyy - 9) + chr(34) + " width=" + chr(34) + "10" + chr(34) + " height=" + chr(34) + "10" + chr(34) + " fill=" + chr(34) + color + chr(34) + "/>")
                append(parts, "<text x=" + chr(34) + _coord(lx + 14) + chr(34) + " y=" + chr(34) + _coord(lyy) + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(ser.name) + "</text>")
                lx = lx + 14 + _estw(ser.name, opts) + 16
            end for
        end if
        if not is_unknown(opts.title) then
            append(parts, "<text x=" + chr(34) + _coord(opts.width / 2) + chr(34) + " y=" + chr(34) + _coord(opts.font_size + 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " font-size=" + chr(34) + _fmt(round(opts.font_size * 1.2, 0), 0, false) + chr(34) + " fill=" + chr(34) + "#111111" + chr(34) + ">" + _escape(opts.title) + "</text>")
        end if
        if not is_unknown(opts.x_label) then
            append(parts, "<text x=" + chr(34) + _coord((px0 + px1) / 2) + chr(34) + " y=" + chr(34) + _coord(opts.height - 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.x_label) + "</text>")
        end if
        if not is_unknown(opts.y_label) then
            append(parts, "<text x=" + chr(34) + _coord(opts.font_size) + chr(34) + " y=" + chr(34) + _coord((py0 + py1) / 2) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " transform=" + chr(34) + "rotate(-90 " + _coord(opts.font_size) + " " + _coord((py0 + py1) / 2) + ")" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.y_label) + "</text>")
        end if

        append(parts, "</svg>")
        return join(parts, "")
    end function

    ' -------------------------------------------------- histogram (Phase 2)
    '
    ' One value column binned into equal-width bins over the data range; the
    ' count axis labels whole numbers only. Bin count is the option `bins:`
    ' or ceil(sqrt(n)) — deterministic either way. unknown and nan skipped.
    function _render_hist(s, opts)
        if not has(s.df, s.x) then
            error "chart: no column '" + s.x + "' in the frame"
        end if
        col = s.df[s.x]
        vals = []
        for each v in col
            if not is_unknown(v) then
                t = type(v)
                if t != "number" and t != "money" then
                    error "chart: column '" + s.x + "' holds " + t + " values; a histogram bins numbers"
                end if
            end if
            pv = _plottable(v)
            if not is_unknown(pv) then
                append(vals, pv)
            end if
        end for
        nv = count(vals)

        lo = 0
        hi = 1
        if nv > 0 then
            lo = min(vals)
            hi = max(vals)
        end if
        if lo = hi then
            lo = lo - 0.5
            hi = hi + 0.5
        end if
        nbins = opts.bins
        if is_unknown(nbins) then nbins = ceil(sqrt(nv))
        if nbins < 1 then nbins = 1
        bw = (hi - lo) / nbins

        counts = []
        k = 0
        while k < nbins
            append(counts, 0)
            k = k + 1
        end while
        for each v in vals
            k = floor((v - lo) / bw)
            if k >= nbins then k = nbins - 1
            if k < 0 then k = 0
            counts[k] = counts[k] + 1
        end for
        cmax = 1
        for each c in counts
            if c > cmax then cmax = c
        end for

        tx = _ticks(lo, hi, opts.max_ticks_x, false)
        ty = _ticks(0, cmax, opts.max_ticks_y, true)

        xlabels = []
        for each t in tx.ticks
            append(xlabels, _fmt(t, tx.decimals, true))
        end for
        ylabels = []
        for each t in ty.ticks
            append(ylabels, _fmt(t, ty.decimals, true))
        end for
        ywmax = 0
        for each lbl in ylabels
            w = _estw(lbl, opts)
            if w > ywmax then ywmax = w
        end for

        mleft = opts.margin_left
        if is_unknown(mleft) then
            mleft = ywmax + 10
            if not is_unknown(opts.y_label) then mleft = mleft + opts.font_size + 8
        end if
        mright = opts.margin_right
        if is_unknown(mright) then mright = 14
        mtop = opts.margin_top
        if is_unknown(mtop) then
            mtop = 12
            if not is_unknown(opts.title) then mtop = mtop + round(opts.font_size * 1.5, 0) + 6
        end if
        mbottom = opts.margin_bottom
        if is_unknown(mbottom) then
            mbottom = opts.font_size + 12
            if not is_unknown(opts.x_label) then mbottom = mbottom + opts.font_size + 8
        end if

        px0 = mleft
        px1 = opts.width - mright
        py0 = mtop
        py1 = opts.height - mbottom

        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(opts.width, 0, false) + chr(34) + " height=" + chr(34) + _fmt(opts.height, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(opts.width, 0, false) + " " + _fmt(opts.height, 0, false) + chr(34) + " font-family=" + chr(34) + "sans-serif" + chr(34) + " font-size=" + chr(34) + _fmt(opts.font_size, 0, false) + chr(34) + ">")

        if opts.grid then
            for each t in ty.ticks
                gy = _scale_y(t, ty, py0, py1)
                append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + gy + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + gy + chr(34) + " stroke=" + chr(34) + "#dddddd" + chr(34) + "/>")
            end for
        end if
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py1) + chr(34) + " x2=" + chr(34) + _coord(px1) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")
        append(parts, "<line x1=" + chr(34) + _coord(px0) + chr(34) + " y1=" + chr(34) + _coord(py0) + chr(34) + " x2=" + chr(34) + _coord(px0) + chr(34) + " y2=" + chr(34) + _coord(py1) + chr(34) + " stroke=" + chr(34) + "#333333" + chr(34) + "/>")

        i = 0
        while i < count(ty.ticks)
            gy = _scale_y(ty.ticks[i], ty, py0, py1)
            append(parts, "<text x=" + chr(34) + _coord(px0 - 6) + chr(34) + " y=" + chr(34) + gy + chr(34) + " text-anchor=" + chr(34) + "end" + chr(34) + " dominant-baseline=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(ylabels[i]) + "</text>")
            i = i + 1
        end while
        i = 0
        while i < count(tx.ticks)
            gx = _scale_x(tx.ticks[i], tx, px0, px1)
            append(parts, "<text x=" + chr(34) + gx + chr(34) + " y=" + chr(34) + _coord(py1 + opts.font_size + 4) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(xlabels[i]) + "</text>")
            i = i + 1
        end while

        if nv > 0 then
            k = 0
            while k < nbins
                c = counts[k]
                if c > 0 then
                    xleft = _scale_x(lo + (k * bw), tx, px0, px1)
                    xright = _scale_x(lo + ((k + 1) * bw), tx, px0, px1)
                    ytop = _scale_y(c, ty, py0, py1)
                    append(parts, "<rect x=" + chr(34) + xleft + chr(34) + " y=" + chr(34) + ytop + chr(34) + " width=" + chr(34) + _coord(number(xright) - number(xleft)) + chr(34) + " height=" + chr(34) + _coord(py1 - number(ytop)) + chr(34) + " fill=" + chr(34) + opts.palette[0] + chr(34) + " stroke=" + chr(34) + "#ffffff" + chr(34) + " stroke-width=" + chr(34) + "1" + chr(34) + "/>")
                end if
                k = k + 1
            end while
        else
            append(parts, "<text x=" + chr(34) + _coord((px0 + px1) / 2) + chr(34) + " y=" + chr(34) + _coord((py0 + py1) / 2) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#888888" + chr(34) + ">no data</text>")
        end if

        if not is_unknown(opts.title) then
            append(parts, "<text x=" + chr(34) + _coord(opts.width / 2) + chr(34) + " y=" + chr(34) + _coord(opts.font_size + 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " font-size=" + chr(34) + _fmt(round(opts.font_size * 1.2, 0), 0, false) + chr(34) + " fill=" + chr(34) + "#111111" + chr(34) + ">" + _escape(opts.title) + "</text>")
        end if
        if not is_unknown(opts.x_label) then
            append(parts, "<text x=" + chr(34) + _coord((px0 + px1) / 2) + chr(34) + " y=" + chr(34) + _coord(opts.height - 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.x_label) + "</text>")
        end if
        if not is_unknown(opts.y_label) then
            append(parts, "<text x=" + chr(34) + _coord(opts.font_size) + chr(34) + " y=" + chr(34) + _coord((py0 + py1) / 2) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " transform=" + chr(34) + "rotate(-90 " + _coord(opts.font_size) + " " + _coord((py0 + py1) / 2) + ")" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(opts.y_label) + "</text>")
        end if

        append(parts, "</svg>")
        return join(parts, "")
    end function

    ' -------------------------------------------------------- pie (Phase 4)
    '
    ' A pie shows SHARES OF A WHOLE, which forces three refusals: a NEGATIVE
    ' share is nonsense; an UNKNOWN share silently misstates every other
    ' share, so it is refused rather than skipped (unlike a line's gap, where
    ' absence is honest); a whole of zero has nothing to show. Slices start
    ' at 12 o'clock and run clockwise, in row order.
    function _render_pie(s, opts)
        if count(s.y) != 1 then
            error "chart: a pie takes exactly one value column"
        end if
        if not has(s.df, s.x) then
            error "chart: no column '" + s.x + "' in the frame"
        end if
        labels_col = s.df[s.x]
        nrows = count(labels_col)
        vcol = _column(s.df, s.y[0], nrows)
        if nrows > count(opts.palette) then
            error "chart: " + string(nrows) + " slices but the palette has " + string(count(opts.palette)) + " colors; pass a longer palette: option"
        end if

        cats = []
        vals = []
        total = 0
        i = 0
        while i < nrows
            lv = labels_col[i]
            if is_unknown(lv) then
                error "chart: column '" + s.x + "' has an unknown category; a slice needs a name"
            end if
            key = string(lv)
            if contains(cats, key) then
                error "chart: category '" + key + "' appears twice; aggregate before charting"
            end if
            append(cats, key)
            v = vcol[i]
            if not is_unknown(v) then
                t = type(v)
                if t != "number" and t != "money" then
                    error "chart: column '" + s.y[0] + "' holds " + t + " values; charts plot numbers (categories belong on a bar chart's x axis)"
                end if
            end if
            pv = _plottable(v)
            if is_unknown(pv) then
                error "chart: category '" + key + "' has an unknown value; a pie needs every share (a missing share misstates the others)"
            end if
            if pv < 0 then
                error "chart: category '" + key + "' is negative; a pie cannot show a negative share"
            end if
            append(vals, pv)
            total = total + pv
            i = i + 1
        end while
        if total <= 0 then
            error "chart: the shares sum to zero; there is nothing to divide"
        end if

        mtop = opts.margin_top
        if is_unknown(mtop) then
            mtop = 12
            if not is_unknown(opts.title) then mtop = mtop + round(opts.font_size * 1.5, 0) + 6
            mtop = mtop + opts.font_size + 8
        end if
        cx = opts.width / 2
        cy = mtop + ((opts.height - mtop - 12) / 2)
        r = (opts.height - mtop - 12) / 2 * 0.85
        half = (opts.width - 24) / 2
        if half < r then r = half

        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(opts.width, 0, false) + chr(34) + " height=" + chr(34) + _fmt(opts.height, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(opts.width, 0, false) + " " + _fmt(opts.height, 0, false) + chr(34) + " font-family=" + chr(34) + "sans-serif" + chr(34) + " font-size=" + chr(34) + _fmt(opts.font_size, 0, false) + chr(34) + ">")

        acc = 0
        i = 0
        while i < nrows
            frac = vals[i] / total
            if frac > 0 then
                if frac >= 1 then
                    append(parts, "<circle cx=" + chr(34) + _coord(cx) + chr(34) + " cy=" + chr(34) + _coord(cy) + chr(34) + " r=" + chr(34) + _coord(r) + chr(34) + " fill=" + chr(34) + opts.palette[i] + chr(34) + "/>")
                else
                    a0 = (0 - (_pi() / 2)) + (acc * 2 * _pi())
                    a1 = (0 - (_pi() / 2)) + ((acc + frac) * 2 * _pi())
                    x1 = cx + (r * _cos(a0))
                    y1 = cy + (r * _sin(a0))
                    x2 = cx + (r * _cos(a1))
                    y2 = cy + (r * _sin(a1))
                    large = 0
                    if frac > 0.5 then large = 1
                    d = "M" + _coord(cx) + " " + _coord(cy) + " L" + _coord(x1) + " " + _coord(y1) + " A" + _coord(r) + " " + _coord(r) + " 0 " + string(large) + " 1 " + _coord(x2) + " " + _coord(y2) + " Z"
                    append(parts, "<path d=" + chr(34) + d + chr(34) + " fill=" + chr(34) + opts.palette[i] + chr(34) + " stroke=" + chr(34) + "#ffffff" + chr(34) + " stroke-width=" + chr(34) + "1" + chr(34) + "/>")
                end if
            end if
            acc = acc + frac
            i = i + 1
        end while

        ' Legend, always: name + share, since slices carry no labels.
        lx = 12
        lyy = mtop - opts.font_size + 2
        i = 0
        while i < nrows
            pct = _fmt(vals[i] / total * 100, 1, false) + "%"
            entry = cats[i] + " " + pct
            append(parts, "<rect x=" + chr(34) + _coord(lx) + chr(34) + " y=" + chr(34) + _coord(lyy - 9) + chr(34) + " width=" + chr(34) + "10" + chr(34) + " height=" + chr(34) + "10" + chr(34) + " fill=" + chr(34) + opts.palette[i] + chr(34) + "/>")
            append(parts, "<text x=" + chr(34) + _coord(lx + 14) + chr(34) + " y=" + chr(34) + _coord(lyy) + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(entry) + "</text>")
            lx = lx + 14 + _estw(entry, opts) + 16
            i = i + 1
        end while

        if not is_unknown(opts.title) then
            append(parts, "<text x=" + chr(34) + _coord(opts.width / 2) + chr(34) + " y=" + chr(34) + _coord(opts.font_size + 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " font-size=" + chr(34) + _fmt(round(opts.font_size * 1.2, 0), 0, false) + chr(34) + " fill=" + chr(34) + "#111111" + chr(34) + ">" + _escape(opts.title) + "</text>")
        end if
        append(parts, "</svg>")
        return join(parts, "")
    end function

    ' ---------------------------------------------------- heatmap (Phase 4)
    '
    ' A matrix of values as colored cells — the correlation-matrix picture.
    ' The frame is { rows: [labels], cols: [labels], matrix: [[...], ...] }.
    ' Color is a diverging three-stop scale (heat_lo/heat_mid/heat_hi) over
    ' heat_min..heat_max (default: the data range). An unknown cell is GRAY
    ' with no number — missing shown as missing, never as a value. A ragged
    ' matrix is refused by row.
    function _render_heatmap(s, opts)
        ok = has(s.df, "rows") and has(s.df, "cols") and has(s.df, "matrix")
        if not ok then
            error "chart: a heatmap frame needs rows, cols, and matrix"
        end if
        rows = s.df["rows"]
        cols = s.df["cols"]
        m = s.df["matrix"]
        nr = count(rows)
        nc = count(cols)
        if count(m) != nr then
            error "chart: the matrix has " + string(count(m)) + " rows; the labels name " + string(nr)
        end if
        ri = 0
        while ri < nr
            if count(m[ri]) != nc then
                error "chart: matrix row " + string(ri) + " has " + string(count(m[ri])) + " cells; the labels name " + string(nc)
            end if
            ri = ri + 1
        end while

        ' Domain.
        lo = opts.heat_min
        hi = opts.heat_max
        if is_unknown(lo) or is_unknown(hi) then
            seen = false
            dlo = 0
            dhi = 1
            ri = 0
            while ri < nr
                ci = 0
                while ci < nc
                    v = m[ri][ci]
                    if not is_unknown(v) then
                        t = type(v)
                        if t != "number" and t != "money" then
                            error "chart: matrix cell (" + string(ri) + "," + string(ci) + ") holds " + t + " values; a heatmap colors numbers"
                        end if
                    end if
                    pv = _plottable(v)
                    if not is_unknown(pv) then
                        if not seen then
                            dlo = pv
                            dhi = pv
                            seen = true
                        else
                            if pv < dlo then dlo = pv
                            if pv > dhi then dhi = pv
                        end if
                    end if
                    ci = ci + 1
                end while
                ri = ri + 1
            end while
            if is_unknown(lo) then lo = dlo
            if is_unknown(hi) then hi = dhi
        end if
        if lo = hi then
            lo = lo - 0.5
            hi = hi + 0.5
        end if

        ' Margins: left fits the longest row label, top fits column labels.
        rwmax = 0
        for each lbl in rows
            w = _estw(string(lbl), opts)
            if w > rwmax then rwmax = w
        end for
        mleft = opts.margin_left
        if is_unknown(mleft) then mleft = rwmax + 10
        mright = opts.margin_right
        if is_unknown(mright) then mright = 14
        mtop = opts.margin_top
        if is_unknown(mtop) then
            mtop = opts.font_size + 10
            if not is_unknown(opts.title) then mtop = mtop + round(opts.font_size * 1.5, 0) + 6
        end if
        mbottom = opts.margin_bottom
        if is_unknown(mbottom) then mbottom = 10

        px0 = mleft
        px1 = opts.width - mright
        py0 = mtop
        py1 = opts.height - mbottom
        cw = (px1 - px0) / nc
        chh = (py1 - py0) / nr

        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(opts.width, 0, false) + chr(34) + " height=" + chr(34) + _fmt(opts.height, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(opts.width, 0, false) + " " + _fmt(opts.height, 0, false) + chr(34) + " font-family=" + chr(34) + "sans-serif" + chr(34) + " font-size=" + chr(34) + _fmt(opts.font_size, 0, false) + chr(34) + ">")

        ' Column labels (middle over each column), row labels (end at left).
        ci = 0
        while ci < nc
            append(parts, "<text x=" + chr(34) + _coord(px0 + (ci * cw) + (cw / 2)) + chr(34) + " y=" + chr(34) + _coord(py0 - 4) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(cols[ci]) + "</text>")
            ci = ci + 1
        end while
        ri = 0
        while ri < nr
            append(parts, "<text x=" + chr(34) + _coord(px0 - 6) + chr(34) + " y=" + chr(34) + _coord(py0 + (ri * chh) + (chh / 2)) + chr(34) + " text-anchor=" + chr(34) + "end" + chr(34) + " dominant-baseline=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + "#333333" + chr(34) + ">" + _escape(rows[ri]) + "</text>")
            ri = ri + 1
        end while

        ' Cells.
        ri = 0
        while ri < nr
            ci = 0
            while ci < nc
                pv = _plottable(m[ri][ci])
                cxp = px0 + (ci * cw)
                cyp = py0 + (ri * chh)
                if is_unknown(pv) then
                    append(parts, "<rect x=" + chr(34) + _coord(cxp) + chr(34) + " y=" + chr(34) + _coord(cyp) + chr(34) + " width=" + chr(34) + _coord(cw) + chr(34) + " height=" + chr(34) + _coord(chh) + chr(34) + " fill=" + chr(34) + "#eeeeee" + chr(34) + " stroke=" + chr(34) + "#ffffff" + chr(34) + "/>")
                else
                    t = (pv - lo) / (hi - lo)
                    if t < 0 then t = 0
                    if t > 1 then t = 1
                    fill = _lerp3(opts.heat_lo, opts.heat_mid, opts.heat_hi, t)
                    append(parts, "<rect x=" + chr(34) + _coord(cxp) + chr(34) + " y=" + chr(34) + _coord(cyp) + chr(34) + " width=" + chr(34) + _coord(cw) + chr(34) + " height=" + chr(34) + _coord(chh) + chr(34) + " fill=" + chr(34) + fill + chr(34) + " stroke=" + chr(34) + "#ffffff" + chr(34) + "/>")
                    if opts.cell_values then
                        tcolor = "#111111"
                        if t < 0.22 or t > 0.78 then tcolor = "#ffffff"
                        append(parts, "<text x=" + chr(34) + _coord(cxp + (cw / 2)) + chr(34) + " y=" + chr(34) + _coord(cyp + (chh / 2)) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " dominant-baseline=" + chr(34) + "middle" + chr(34) + " fill=" + chr(34) + tcolor + chr(34) + ">" + _escape(_fmt(pv, 2, false)) + "</text>")
                    end if
                end if
                ci = ci + 1
            end while
            ri = ri + 1
        end while

        if not is_unknown(opts.title) then
            append(parts, "<text x=" + chr(34) + _coord(opts.width / 2) + chr(34) + " y=" + chr(34) + _coord(opts.font_size + 6) + chr(34) + " text-anchor=" + chr(34) + "middle" + chr(34) + " font-size=" + chr(34) + _fmt(round(opts.font_size * 1.2, 0), 0, false) + chr(34) + " fill=" + chr(34) + "#111111" + chr(34) + ">" + _escape(opts.title) + "</text>")
        end if
        append(parts, "</svg>")
        return join(parts, "")
    end function

    ' -------------------------------------------------- sparkline (Phase 4)
    '
    ' A tiny axis-less line for inline use in tables: no margins, no labels,
    ' no ticks — the shape alone, in a small box. Gaps behave as everywhere.
    function _render_sparkline(s, opts)
        col = s.df[s.x]
        vals = []
        for each v in col
            append(vals, _plottable(v))
        end for
        n = count(vals)
        lo = 0
        hi = 1
        seen = false
        for each v in vals
            if not is_unknown(v) then
                if not seen then
                    lo = v
                    hi = v
                    seen = true
                else
                    if v < lo then lo = v
                    if v > hi then hi = v
                end if
            end if
        end for
        if lo = hi then
            lo = lo - 0.5
            hi = hi + 0.5
        end if

        w = opts.width
        h = opts.height
        pad = 2
        parts = []
        append(parts, "<svg xmlns=" + chr(34) + "http://www.w3.org/2000/svg" + chr(34) + " width=" + chr(34) + _fmt(w, 0, false) + chr(34) + " height=" + chr(34) + _fmt(h, 0, false) + chr(34) + " viewBox=" + chr(34) + "0 0 " + _fmt(w, 0, false) + " " + _fmt(h, 0, false) + chr(34) + ">")
        if seen and n > 1 then
            d = []
            pen = false
            i = 0
            while i < n
                v = vals[i]
                if is_unknown(v) then
                    pen = false
                else
                    sx = pad + (i * (w - (2 * pad)) / (n - 1))
                    sy = (h - pad) - ((v - lo) * (h - (2 * pad)) / (hi - lo))
                    cmd = "L"
                    if not pen then cmd = "M"
                    append(d, cmd + _coord(sx) + " " + _coord(sy))
                    pen = true
                end if
                i = i + 1
            end while
            if count(d) > 0 then
                append(parts, "<path d=" + chr(34) + join(d, " ") + chr(34) + " fill=" + chr(34) + "none" + chr(34) + " stroke=" + chr(34) + opts.palette[0] + chr(34) + " stroke-width=" + chr(34) + "1.5" + chr(34) + "/>")
            end if
        end if
        append(parts, "</svg>")
        return join(parts, "")
    end function

    function line(df, xcol, ycols)
        return render(y(x(spec("line", df), xcol), ycols))
    end function

    function scatter(df, xcol, ycols)
        return render(y(x(spec("scatter", df), xcol), ycols))
    end function

    function line_xy(xs, ys)
        return line({ x: xs, y: ys }, "x", "y")
    end function

    function scatter_xy(xs, ys)
        return scatter({ x: xs, y: ys }, "x", "y")
    end function

    function bar(df, xcol, ycols)
        return render(y(x(spec("bar", df), xcol), ycols))
    end function

    function histogram(df, xcol)
        return render(x(spec("histogram", df), xcol))
    end function

    function bar_xy(categories, values)
        return bar({ category: categories, value: values }, "category", "value")
    end function

    function histogram_xy(values)
        return histogram({ value: values }, "value")
    end function

    function area(df, xcol, ycols)
        return render(y(x(spec("area", df), xcol), ycols))
    end function

    function area_xy(xs, ys)
        return area({ x: xs, y: ys }, "x", "y")
    end function

    function pie(df, labelcol, valuecol)
        return render(y(x(spec("pie", df), labelcol), valuecol))
    end function

    ' A square heatmap (one label list for both axes) — the correlation shape.
    function heatmap(labels, matrix)
        return render(spec("heatmap", { rows: labels, cols: labels, matrix: matrix }))
    end function

    ' The rectangular form.
    function heatmap_grid(row_labels, col_labels, matrix)
        return render(spec("heatmap", { rows: row_labels, cols: col_labels, matrix: matrix }))
    end function

    ' 120x24 by default; the full options record is reachable via the spec path.
    function sparkline(values)
        s = options(x(spec("sparkline", { v: values }), "v"), { width: 120, height: 24 })
        return render(s)
    end function

    ' A minimal standalone HTML document around the fragment, for
    ' save-and-open. The fragment stays the primitive.
    function page(s)
        svg = render(s)
        t = "chart"
        opts = _merge(_defaults(), s.opts)
        if not is_unknown(opts.title) then t = opts.title
        return "<!DOCTYPE html><html><head><meta charset=" + chr(34) + "utf-8" + chr(34) + "><title>" + _escape(t) + "</title></head><body>" + svg + "</body></html>"
    end function

end library
