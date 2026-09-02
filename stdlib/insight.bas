' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' insight.bas — observes and reasons (docs/automation_reasoning_design.md §5).
' It never alters the business.
'
' FIRST INCREMENT (§13): one function. `explain_change` answers "revenue moved;
' where did that happen, and does any of it mean anything?" -- and the second
' half of that question is why the increment is one function rather than
' twelve. Recipe 1 measured that a drill-down implemented WITHOUT it produces
' the same confident three-level causal chain from a real planted collapse and
' from pure noise, with the same headline to a tenth of a percent. A decision
' layer built on a Finding like that would be built on sand.
library insight

    load frame from "frame.bas"
    load reasoning from "reasoning.bas"
    load stats from "stats.bas"

    ' --- explain_change ------------------------------------------------------
    '
    ' spec:
    '   measure       column holding the quantity
    '   period        column separating the two periods
    '   baseline      the period value that is "before"
    '   current       the period value that is "after"
    '   dimensions    ordered list of columns to decompose by
    '   comparison    declared (reasoning.comparisons())
    '   null          declared (reasoning.nulls())
    '   associations  optional: other measure columns that may have moved with it
    '   subject       optional label
    '   as_of         optional, recorded in provenance
    function explain_change(df, spec)
        if type(spec) != "record" then
            error "insight.explain_change expects a spec record"
        end if
        for each field in ["measure", "period", "baseline", "current",
                           "dimensions", "comparison", "null"]
            if is_unknown(spec[field]) then
                ' The two DECLARED choices say why they are required. A bare
                ' "needs a null" invites the reader to supply anything that
                ' silences it, which is the failure the declaration exists to
                ' prevent.
                if field = "null" then
                    error ("insight.explain_change: the null must be declared"
                           + " -- what counts as ordinary changes every answer"
                           + " downstream, and `siblings` is right for a"
                           + " stationary measure and wrong under seasonality"
                           + " (design R8). One of "
                           + join(reasoning.nulls(), ", "))
                end if
                if field = "comparison" then
                    error ("insight.explain_change: the comparison must be"
                           + " declared -- period over period, versus last year"
                           + " and versus forecast disagree routinely and each"
                           + " answers a different question. One of "
                           + join(reasoning.comparisons(), ", "))
                end if
                error ("insight.explain_change needs a " + field)
            end if
        next
        if not contains(reasoning.comparisons(), spec["comparison"]) then
            error ("insight.explain_change: comparison must be one of "
                   + join(reasoning.comparisons(), ", ")
                   + " -- period over period, versus last year and versus"
                   + " forecast disagree routinely and each answers a different"
                   + " question (got " + string(spec["comparison"]) + ")")
        end if
        if not contains(reasoning.nulls(), spec["null"]) then
            error ("insight.explain_change: the null must be declared and be one"
                   + " of " + join(reasoning.nulls(), ", ")
                   + " (got " + string(spec["null"]) + ")")
        end if
        dims = spec["dimensions"]
        if type(dims) != "array" or count(dims) = 0 then
            error "insight.explain_change needs a non-empty list of dimensions"
        end if

        rows = frame.to_rows(df)
        if count(rows) = 0 then
            error "insight.explain_change: the frame has no rows"
        end if
        have = keys(rows[0])
        for each c in dims
            if not contains(have, c) then
                error ("insight.explain_change: no column " + string(c)
                       + " to decompose by")
            end if
        next
        for each c in [spec["measure"], spec["period"]]
            if not contains(have, c) then
                error "insight.explain_change: no column " + string(c)
            end if
        next
        assoc_names = spec["associations"]
        if is_unknown(assoc_names) then
            assoc_names = []
        end if
        for each c in assoc_names
            if not contains(have, c) then
                error ("insight.explain_change: no association column "
                       + string(c))
            end if
        next

        ' --- one pass: totals per cell, per period, for the measure and for
        ' every association named.
        cells = _accumulate(rows, spec, dims, assoc_names)
        n = count(cells.order)
        if n < 2 then
            error ("insight.explain_change: the decomposition produced "
                   + string(n) + " cell -- a null needs siblings to be drawn"
                   + " from, so there is nothing to compare this against")
        end if

        changes = []
        for each k in cells.order
            append(changes, cells.current[k] - cells.baseline[k])
        next
        m = mean(changes)
        sd = stdev(changes)

        base_total = 0
        now_total = 0
        for each k in cells.order
            base_total = base_total + cells.baseline[k]
            now_total = now_total + cells.current[k]
        next
        net = now_total - base_total

        ' --- R1 / §4.3. The cut is NOT a constant: it is decided by how wide
        ' the search was, so a z that is remarkable across four regions is
        ' unremarkable across two hundred product families.
        '
        ' IT IS A FAMILY-WISE QUANTILE, NOT THE EXPECTED MAXIMUM. Recipe 1 used
        ' sqrt(2 ln n), which is where the largest of n draws lands ON AVERAGE
        ' -- so roughly half of all pure-noise populations produce a leader
        ' that clears it. Measured while building this: of thirteen seeds with
        ' NOTHING planted, six cleared. That is not a correction, it is a coin
        ' flip with a formula in front of it.
        '
        ' The threshold is therefore the two-sided Bonferroni quantile for a
        ' declared family-wise error rate: reject only beyond the point that a
        ' search of n cells would exceed with probability `alpha` when nothing
        ' is happening. `alpha` is recorded in the finding, because a threshold
        ' nobody can see is a threshold nobody can argue with.
        alpha = spec["alpha"]
        if is_unknown(alpha) then
            alpha = 0.05
        end if
        if type(alpha) != "number" or alpha <= 0 or alpha >= 1 then
            error "insight.explain_change: alpha must be a number between 0 and 1"
        end if

        ' THE NULL IS COMPUTED LEAVE-ONE-OUT, and that is not a refinement.
        ' Standardising a cell against a spread that INCLUDES it means the
        ' outlier inflates the very sd it is measured against, which puts a
        ' hard ceiling on how extreme anything can look: max|z| = (n-1)/sqrt(n),
        ' regardless of the data. At 12 cells that ceiling is 3.17; at 8 it is
        ' 2.47 -- BELOW the threshold, so the test could never fire however
        ' completely a cell had collapsed, and would report "within ordinary
        ' variation" for a cell that had gone to zero.
        '
        ' Found by replaying a year through the process (recipe 6): it fired 0
        ' of 12 months and MISSED a planted collapse it had caught easily at 60
        ' cells. Leaving the cell out removes the contamination and the ceiling
        ' with it.
        '
        ' A leave-one-out standardised residual is t-distributed with n-2
        ' degrees of freedom, not normal, so the threshold follows -- using a
        ' normal quantile here would be anti-conservative at exactly the small
        ' n where this matters most.
        if n < 4 then
            error ("insight.explain_change: " + string(n) + " cells is too few"
                   + " to judge one against the others -- a leave-one-out"
                   + " spread needs at least two degrees of freedom, and a"
                   + " search this narrow cannot establish anything. Decompose"
                   + " by fewer dimensions, or accept that this question is not"
                   + " answerable from this data")
        end if
        threshold = stats.t_quantile(1 - alpha / (2 * n), n - 2)

        ' --- R2 / §4.4. A share is contributor_change / net_change, and that
        ' denominator is unstable: as offsetting movements grow, shares inflate
        ' without bound while the net approaches zero. So ask first whether the
        ' NET is distinguishable from zero at all -- a one-sample test on the
        ' mean cell change. If it is not, "82.6% of the decline" is a share of
        ' something that has not been established, and no share is reported.
        se = 0
        if n > 0 and sd > 0 then
            se = sd / sqrt(n)
        end if
        t = 0
        if se > 0 then
            t = m / se
        end if
        reportable = abs(t) >= 2
        withheld = ""
        if not reportable then
            withheld = ("the net change is not distinguishable from zero"
                        + " (t = " + string(round(t, 2)) + " over " + string(n)
                        + " cells), so a share of it would be a share of"
                        + " something not established")
        end if

        total = 0
        for each c in changes
            total = total + c
        next

        contributors = []
        idx = 0
        for each k in cells.order
            d = cells.current[k] - cells.baseline[k]
            ' Leave-one-out: this cell is judged against the OTHERS, never
            ' against a spread it is itself part of.
            others = _without(changes, idx)
            om = mean(others)
            osd = stdev(others)
            z = 0
            if osd > 0 then
                z = (d - om) / osd
            end if
            idx = idx + 1
            share = unknown
            if reportable and net != 0 then
                share = d / net
            end if
            append(contributors, { path: cells.path[k], change: d,
                                   baseline: cells.baseline[k],
                                   current: cells.current[k],
                                   share: share, z: z,
                                   clears: abs(z) > threshold })
        next
        contributors = _by_absolute_change(contributors)

        top = contributors[0]
        assoc = _associations(cells, assoc_names, changes)

        prov = reasoning.provenance({
                 method: "explain_change/siblings",
                 rows: count(rows),
                 parameters: { measure: spec["measure"], period: spec["period"],
                               baseline: spec["baseline"], current: spec["current"],
                               dimensions: dims, comparison: spec["comparison"],
                               null: spec["null"] },
                 assumptions: ["ordinary movement is stationary across cells",
                               "cells vary independently"],
                 as_of: spec["as_of"] })

        subject = spec["subject"]
        if is_unknown(subject) then
            subject = spec["measure"]
        end if
        pct = unknown
        if base_total != 0 then
            pct = net / base_total
        end if

        return reasoning.finding({
            subject: subject,
            measure: spec["measure"],
            period: { baseline: spec["baseline"], current: spec["current"] },
            comparison: spec["comparison"],
            observation: { baseline: base_total, current: now_total,
                           change: net, change_pct: pct },
            search: { dimensions: dims, cells: n, width: threshold,
                      alpha: alpha, correction: "bonferroni" },
            null: { kind: spec["null"], mean: m, sd: sd, threshold: threshold,
                    net_t: t, standardized: "leave_one_out", df: n - 2 },
            strength: { z: top.z, clears: top.clears, leader: top.path },
            contributors: contributors,
            shares_reportable: reportable,
            shares_withheld_because: withheld,
            associations: assoc,
            provenance: prov })
    end function

    ' --- internals -----------------------------------------------------------

    function _accumulate(rows, spec, dims, assoc_names)
        baseline = { }
        current = { }
        path = { }
        order = []
        ab = { }
        ac = { }
        measure = spec["measure"]
        pcol = spec["period"]
        for each r in rows
            key = ""
            parts = []
            for each d in dims
                key = key + string(r[d]) + "|"
                append(parts, string(r[d]))
            next
            if is_unknown(baseline[key]) then
                baseline[key] = 0
                current[key] = 0
                path[key] = parts
                append(order, key)
                for each a in assoc_names
                    ab[a + "@" + key] = 0
                    ac[a + "@" + key] = 0
                next
            end if
            if r[pcol] = spec["baseline"] then
                baseline[key] = baseline[key] + r[measure]
                for each a in assoc_names
                    ab[a + "@" + key] = ab[a + "@" + key] + r[a]
                next
            else if r[pcol] = spec["current"] then
                current[key] = current[key] + r[measure]
                for each a in assoc_names
                    ac[a + "@" + key] = ac[a + "@" + key] + r[a]
                next
            end if
        next
        return { baseline: baseline, current: current, path: path,
                 order: order, assoc_baseline: ab, assoc_current: ac }
    end function

    ' R3. These are ASSOCIATIONS. The word `cause` does not appear, and
    ' `reasoning.finding` refuses a field by that name, because a measure that
    ' moved with this one across cells is evidence for a hypothesis and is not
    ' itself an explanation.
    function _associations(cells, assoc_names, changes)
        out = []
        for each a in assoc_names
            other = []
            for each k in cells.order
                append(other, cells.assoc_current[a + "@" + k]
                              - cells.assoc_baseline[a + "@" + k])
            next
            append(out, { measure: a, correlation: _corr(changes, other),
                          relationship: "moved with", explains: false })
        next
        return out
    end function

    ' The list with one entry removed. Small n by construction -- this is a
    ' decomposition, not a dataset -- so the copy is the honest implementation.
    function _without(xs, skip)
        out = []
        for i = 0 to count(xs) - 1
            if i != skip then
                append(out, xs[i])
            end if
        next
        return out
    end function

    function _corr(a, b)
        n = count(a)
        if n < 2 then
            return unknown
        end if
        ma = mean(a)
        mb = mean(b)
        sa = 0
        sb = 0
        sab = 0
        for i = 0 to n - 1
            da = a[i] - ma
            db = b[i] - mb
            sa = sa + da * da
            sb = sb + db * db
            sab = sab + da * db
        next
        if sa <= 0 or sb <= 0 then
            return unknown
        end if
        return sab / sqrt(sa * sb)
    end function

    function _by_absolute_change(items)
        out = items
        for i = 1 to count(out) - 1
            j = i
            while j > 0 and abs(out[j - 1].change) < abs(out[j].change)
                t = out[j - 1]
                out[j - 1] = out[j]
                out[j] = t
                j = j - 1
            end while
        next
        return out
    end function

end library
