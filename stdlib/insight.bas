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
        draws = spec["draws"]
        if is_unknown(draws) then
            draws = 200
        end if
        permute_seed = spec["permute_seed"]
        if is_unknown(permute_seed) then
            permute_seed = 1
        end if

        threshold = 0
        calibration = ""
        if spec["null"] = "siblings_permuted" then
            threshold = _permuted_threshold(cells, alpha, draws, permute_seed)
            calibration = "permuted from this data (" + string(draws) + " draws)"
        else
            threshold = stats.t_quantile(1 - alpha / (2 * n), n - 2)
            calibration = ("assumed: a t threshold, exact only when cell changes"
                           + " are light-tailed. Measured 0.047 against a"
                           + " requested 0.05 for uniform data and 0.110 for"
                           + " lognormal revenue -- declare"
                           + " null: \"siblings_permuted\" to take the"
                           + " threshold from the data instead")
        end if

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

        sums = _sums(changes)

        contributors = []
        for each k in cells.order
            d = cells.current[k] - cells.baseline[k]
            ' Leave-one-out: this cell is judged against the OTHERS, never
            ' against a spread it is itself part of.
            z = _loo_z(d, n, sums.total, sums.total_sq)
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
                               null: spec["null"], alpha: alpha,
                               threshold_calibration: calibration },
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
                    net_t: t, standardized: "leave_one_out", df: n - 2,
                    calibration: calibration,
                    draws: draws, permute_seed: permute_seed },
            strength: { z: top.z, clears: top.clears, leader: top.path },
            contributors: contributors,
            shares_reportable: reportable,
            shares_withheld_because: withheld,
            associations: assoc,
            provenance: prov })
    end function

    ' --- weighing hypotheses --------------------------------------------------
    '
    ' THE VALUE IS IN THE PATTERN, NOT THE MAGNITUDE. A hypothesis predicts
    ' WHICH cells should have moved. Comparing that prediction against the
    ' cells that actually cleared is a CONTINGENCY -- hits, over-predictions,
    ' and cells left unexplained -- and it is auditable in a way a scalar is
    ' not.
    '
    ' SO THERE IS NO PROBABILITY HERE. The charter\'s §11 showed
    ' "inventory availability   confidence .91", and that number is unearned:
    ' nothing in the data supports a probability that a hypothesis is TRUE.
    ' What can honestly be computed is set agreement, which is reported under
    ' its own name with its definition attached.
    function weigh(finding, hypotheses)
        if type(finding) != "record" or is_unknown(finding["contributors"]) then
            error "insight.weigh expects a Finding"
        end if
        if type(hypotheses) != "array" or count(hypotheses) = 0 then
            error "insight.weigh expects a non-empty array of hypotheses"
        end if

        affected = []
        for each c in finding["contributors"]
            if c["clears"] then
                append(affected, _key(c["path"], finding["search"]["dimensions"]))
            end if
        next
        if count(affected) = 0 then
            error ("insight.weigh: this finding has no cell that cleared its"
                   + " threshold, so there is no pattern to explain -- weighing"
                   + " hypotheses against nothing would rank stories by how"
                   + " little they claim")
        end if

        scored = []
        for each h in hypotheses
            if is_unknown(h["predicts"]) or is_unknown(h["discriminator"]) then
                error ("insight.weigh: every hypothesis must be built with"
                       + " reasoning.hypothesis")
            end if
            predicted = []
            for each c in finding["contributors"]
                if _matches(c["path"], finding["search"]["dimensions"], h["predicts"]) then
                    append(predicted, _key(c["path"], finding["search"]["dimensions"]))
                end if
            next
            hit = 0
            for each a in affected
                if contains(predicted, a) then
                    hit = hit + 1
                end if
            next
            over = count(predicted) - hit
            missed = count(affected) - hit
            union = count(predicted) + count(affected) - hit
            agreement = 0
            if union > 0 then
                agreement = hit / union
            end if
            append(scored, { name: h["name"], predicts: h["predicts"],
                             discriminator: h["discriminator"],
                             predicted: count(predicted), hit: hit,
                             over_predicted: over, unexplained: missed,
                             agreement: agreement,
                             explains: false })
        next

        ranked = _by_agreement(scored)

        ' R11. TWO HYPOTHESES THAT PREDICT THE SAME CELLS ARE NOT SEPARATED BY
        ' THIS DATA, and ordering them would be inventing a preference the
        ' evidence does not support. They are reported as tied, and the
        ' discriminators are what the caller should go and observe.
        tied = []
        for i = 0 to count(ranked) - 1
            for j = i + 1 to count(ranked) - 1
                if _same_prediction(finding, ranked[i], ranked[j]) then
                    append(tied, { a: ranked[i].name, b: ranked[j].name,
                                   agreement: ranked[i].agreement,
                                   separate_them_by: [ranked[i].discriminator,
                                                      ranked[j].discriminator] })
                end if
            next
        next

        leader = ranked[0].name
        separable = true
        for each t in tied
            if t.a = leader or t.b = leader then
                separable = false
            end if
        next

        return { affected_cells: count(affected), hypotheses: ranked,
                 leader: leader, leader_is_separable: separable,
                 indistinguishable: tied,
                 agreement_is: ("set agreement between predicted and affected"
                                + " cells (hits / union), NOT a probability that"
                                + " the hypothesis is true"),
                 next_test: _next_test(ranked, tied) }
    end function

    function _next_test(ranked, tied)
        for each t in tied
            if t.a = ranked[0].name or t.b = ranked[0].name then
                return ("the leading hypotheses predict the same cells and this"
                        + " data cannot separate them. Observe: "
                        + join(t.separate_them_by, "  OR  "))
            end if
        next
        return ("to confirm the leader rather than merely rank it, observe: "
                + ranked[0].discriminator)
    end function

    function _same_prediction(finding, a, b)
        if a.predicted != b.predicted then
            return false
        end if
        for each c in finding["contributors"]
            ma = _matches(c["path"], finding["search"]["dimensions"], a.predicts)
            mb = _matches(c["path"], finding["search"]["dimensions"], b.predicts)
            if ma != mb then
                return false
            end if
        next
        return true
    end function

    function _matches(path, dims, predicts)
        for each d in keys(predicts)
            ' `contains`, not `find`: a miss from `find` is `nothing`, and
            ' `is_unknown(nothing)` is FALSE, so an is_unknown guard here reads
            ' as "found at index nothing" and silently lets a hypothesis
            ' predict on an axis that was never searched.
            if not contains(dims, d) then
                error ("insight.weigh: a hypothesis predicts on `" + string(d)
                       + "`, which is not one of the dimensions this finding"
                       + " searched (" + join(dims, ", ") + ")")
            end if
            idx = find(dims, d)
            if path[idx] != predicts[d] then
                return false
            end if
        next
        return true
    end function

    function _key(path, dims)
        return join(path, "/")
    end function

    function _by_agreement(items)
        out = items
        for i = 1 to count(out) - 1
            j = i
            while j > 0 and out[j - 1].agreement < out[j].agreement
                t = out[j - 1]
                out[j - 1] = out[j]
                out[j] = t
                j = j - 1
            end while
        next
        return out
    end function

    ' --- internals -----------------------------------------------------------

    function _accumulate(rows, spec, dims, assoc_names)
        baseline = { }
        current = { }
        path = { }
        order = []
        values = { }
        baseline_n = { }
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
                values[key] = []
                baseline_n[key] = 0
                path[key] = parts
                append(order, key)
                for each a in assoc_names
                    ab[a + "@" + key] = 0
                    ac[a + "@" + key] = 0
                next
            end if
            if r[pcol] = spec["baseline"] then
                baseline[key] = baseline[key] + r[measure]
                lst = values[key]
                append(lst, r[measure])
                values[key] = lst
                baseline_n[key] = baseline_n[key] + 1
                for each a in assoc_names
                    ab[a + "@" + key] = ab[a + "@" + key] + r[a]
                next
            else if r[pcol] = spec["current"] then
                current[key] = current[key] + r[measure]
                lst = values[key]
                append(lst, r[measure])
                values[key] = lst
                for each a in assoc_names
                    ac[a + "@" + key] = ac[a + "@" + key] + r[a]
                next
            end if
        next
        return { baseline: baseline, current: current, path: path,
                 order: order, values: values, baseline_n: baseline_n,
                 assoc_baseline: ab, assoc_current: ac }
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

    ' Leave-one-out standardisation in CLOSED FORM, from running sums. The
    ' first version rebuilt the list without each cell, which is O(n^2) and
    ' fine at twenty cells and not at two hundred -- and the permutation null
    ' below needs this same quantity B times over, which the copying version
    ' could not afford.
    '
    ' BOTH PATHS CALL THIS. If the parametric threshold and the permuted one
    ' standardised differently, the permutation would be calibrating a
    ' statistic nobody computes.
    function _loo_z(x, n, total, total_sq)
        if n < 3 then
            return 0
        end if
        others_mean = (total - x) / (n - 1)
        others_sq = total_sq - x * x
        ss = others_sq - (total - x) * (total - x) / (n - 1)
        if ss <= 0 then
            return 0
        end if
        sd = sqrt(ss / (n - 2))
        if sd <= 0 then
            return 0
        end if
        return (x - others_mean) / sd
    end function

    function _sums(xs)
        t = 0
        q = 0
        for each v in xs
            t = t + v
            q = q + v * v
        next
        return { total: t, total_sq: q }
    end function

    ' The largest |z| in one vector -- the statistic the whole search rests on.
    function _max_abs_z(xs)
        n = count(xs)
        s = _sums(xs)
        best = 0
        for each v in xs
            z = abs(_loo_z(v, n, s.total, s.total_sq))
            if z > best then
                best = z
            end if
        next
        return best
    end function

    ' A PERMUTATION THRESHOLD, for the null that does not assume a shape.
    '
    ' MEASURED, WHICH IS WHY THIS EXISTS: the parametric threshold is exactly
    ' right when cell changes are light-tailed (0.047 observed against a
    ' requested 0.05 for uniform data) and 2-3x wrong when they are not (0.110
    ' at 20 cells and 0.143 at 40 for lognormal revenue), getting WORSE as the
    ' search widens -- the opposite of what a family-wise correction is for.
    ' A change is a difference of two independent sums, so its SKEW cancels;
    ' what does not cancel is TAIL WEIGHT, and revenue-like data has it.
    '
    ' THE OBVIOUS PERMUTATION IS WRONG AND WAS BUILT FIRST. Sign-flipping each
    ' cell\'s deviation leaves every |deviation| intact, so the biggest cell is
    ' still the biggest in every draw and the permutation distribution centres
    ' on the very statistic it is judging: measured, the permuted threshold
    ' (3.724) came out ABOVE the observed maximum (3.309) and the test fired
    ' 0 times in 200 null trials. A null that never fires is worse than one
    ' that fires too often, because nothing reveals it.
    '
    ' What IS exchangeable under the null is the PERIOD LABEL WITHIN A CELL: if
    ' a cell did not move, which of its observations were "before" and which
    ' "after" is arbitrary. Reassigning those labels at random, keeping each
    ' cell\'s own counts, gives the distribution of "the largest |z| when
    ' nothing moved" from this data\'s own variability -- tails included.
    '
    ' Deterministic in a declared seed: a rehearsal must reach the same
    ' threshold as the live run, or R5\'s argument fails one level down.
    function _permuted_threshold(cells, alpha, draws, seed)
        n = count(cells.order)
        maxes = []
        for b = 1 to draws
            changes = []
            idx = 0
            for each k in cells.order
                vals = cells.values[k]
                nb = cells.baseline_n[k]
                ' A deterministic shuffle, then split at the cell's own
                ' baseline count -- so the periods keep their sizes and only
                ' the labelling moves.
                order = _shuffled(count(vals), seed, b, idx)
                lo = 0
                hi = 0
                for j = 0 to count(order) - 1
                    if j < nb then
                        lo = lo + vals[order[j]]
                    else
                        hi = hi + vals[order[j]]
                    end if
                next
                append(changes, hi - lo)
                idx = idx + 1
            next
            append(maxes, _max_abs_z(changes))
        next
        ranked = sort(maxes)
        i = floor((1 - alpha) * draws)
        if i > draws - 1 then
            i = draws - 1
        end if
        if i < 0 then
            i = 0
        end if
        return ranked[i]
    end function

    ' Fisher-Yates from the deterministic bit source.
    function _shuffled(n, seed, b, cell)
        out = []
        for i = 0 to n - 1
            append(out, i)
        next
        for i = n - 1 to 1 step -1
            j = mod(_bit(seed + cell, b, i), i + 1)
            t = out[i]
            out[i] = out[j]
            out[j] = t
        next
        return out
    end function

    ' Small deterministic bit source. Same requirement as automation.assign:
    ' reproducible across runs and machines, because a replay must land on the
    ' same threshold.
    function _bit(seed, b, i)
        h = mod(seed * 2654435 + b * 40503 + i * 12289, 1000003)
        h = bxor(h, shl(h, 7))
        h = bxor(h, shr(h, 11))
        return mod(h * 2654435 + 12345, 1000003)
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
