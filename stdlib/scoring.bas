' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' scoring.bas — credit scorecards (docs/scoring_design.md).
'
' `credit` measures what a book has already DONE. This turns a population into
' a model that RANKS risk, and then into the artefact a credit committee
' approves: a table of attributes and points.
'
' DELIBERATELY THE CREDIT-SPECIFIC HALF ONLY. The modelling engine already
' exists -- `stats.logistic_regression` -- and this does not reimplement it.
' What `stats` has no reason to carry is binning measured by Weight of
' Evidence, discrimination measured the way lenders measure it, calibration
' onto a point scale, and stability monitoring.
'
' ONE CODING IS FIXED AND EVERYTHING ELSE IS DECLARED: an outcome of 1 means
' BAD (the event being predicted). It is fixed rather than declared because it
' appears in every call, and a per-call choice would be a sign error waiting in
' each one. It is validated, so a column of 1s meaning "good" fails loudly
' rather than inverting the model.
library scoring

    load stats from "stats.bas"

    ' --- what is declared ----------------------------------------------------

    ' TWO WOE CONVENTIONS, DIFFERING BY SIGN AND NOTHING ELSE (design §2). Every
    ' coefficient flips, every point allocation flips, and the scorecard ranks
    ' PERFECTLY BACKWARDS while looking entirely normal -- the same
    ' discrimination, pointed at the people who will not pay. Declared, never
    ' inferred, the rule `lending` sets for accrual basis.
    function orientations()
        return ["good_bad", "bad_good"]
    end function

    function _check_orientation(o)
        if not contains(orientations(), o) then
            error ("scorecard: orientation must be \"good_bad\" or \"bad_good\""
                   + " -- they differ by SIGN, so the wrong one ranks backwards"
                   + " while looking normal (got " + string(o) + ")")
        end if
        return true
    end function

    ' An outcome column must be 0/1 with both present. Both halves matter: a
    ' column of one value has no information and every rate below is 0 or 1,
    ' which is a plausible-looking table rather than an error.
    function _check_outcomes(outcomes)
        if type(outcomes) != "array" or count(outcomes) = 0 then
            error "scorecard: outcomes must be a non-empty array"
        end if
        goods = 0
        bads = 0
        for i = 0 to count(outcomes) - 1
            v = outcomes[i]
            if v = 1 or v = true then
                bads = bads + 1
            else if v = 0 or v = false then
                goods = goods + 1
            else
                error ("scorecard: outcome " + string(i + 1) + " is "
                       + string(v) + " -- outcomes are 1 for BAD and 0 for good")
            end if
        next
        if goods = 0 or bads = 0 then
            error ("scorecard: the outcome column has " + string(goods)
                   + " goods and " + string(bads) + " bads -- one class is"
                   + " absent, so every rate is 0 or 1 and nothing can be learned")
        end if
        return { goods: goods, bads: bads }
    end function

    function _is_bad(v)
        return v = 1 or v = true
    end function

    ' --- binning -------------------------------------------------------------

    ' Cut a numeric predictor at declared boundaries. `cuts` are upper edges,
    ' ascending and exclusive; everything above the last lands in a final bin.
    ' A value that is unknown gets its own bin rather than being dropped --
    ' MISSING IS PREDICTIVE, and dropping it both loses that and silently
    ' changes the denominator of every other bin.
    function bin_numeric(values, outcomes, cuts)
        if count(values) != count(outcomes) then
            error ("scoring.bin_numeric: " + string(count(values)) + " values"
                   + " against " + string(count(outcomes)) + " outcomes")
        end if
        checked = _check_outcomes(outcomes)
        if type(cuts) != "array" or count(cuts) = 0 then
            error "scoring.bin_numeric expects a non-empty array of cut points"
        end if
        for i = 1 to count(cuts) - 1
            if cuts[i] <= cuts[i - 1] then
                error ("scoring.bin_numeric: cut points must ascend, but "
                       + string(cuts[i]) + " follows " + string(cuts[i - 1]))
            end if
        next
        labels = []
        for i = 0 to count(cuts) - 1
            if i = 0 then
                append(labels, "< " + string(cuts[0]))
            else
                append(labels, string(cuts[i - 1]) + " to < " + string(cuts[i]))
            end if
        next
        append(labels, ">= " + string(cuts[count(cuts) - 1]))
        append(labels, "(missing)")
        assigned = []
        for i = 0 to count(values) - 1
            append(assigned, labels[_bucket_of(values[i], cuts)])
        next
        return _tally(labels, assigned, outcomes)
    end function

    function _bucket_of(v, cuts)
        if is_unknown(v) or is_nothing(v) then
            return count(cuts) + 1
        end if
        for i = 0 to count(cuts) - 1
            if v < cuts[i] then
                return i
            end if
        next
        return count(cuts)
    end function

    ' Group a categorical predictor. `groups` is [{ label:, values: [...] }];
    ' a value in no group is REFUSED rather than swept into an "other" bin,
    ' since an unlisted category silently absorbed is a category nobody
    ' decided about.
    function bin_categorical(values, outcomes, groups)
        if count(values) != count(outcomes) then
            error ("scoring.bin_categorical: " + string(count(values))
                   + " values against " + string(count(outcomes)) + " outcomes")
        end if
        checked = _check_outcomes(outcomes)
        if type(groups) != "array" or count(groups) = 0 then
            error "scoring.bin_categorical expects a non-empty array of groups"
        end if
        labels = []
        for each g in groups
            if is_unknown(g["label"]) or is_unknown(g["values"]) then
                error "scoring.bin_categorical: each group needs a label and values"
            end if
            if contains(labels, g["label"]) then
                error ("scoring.bin_categorical: duplicate group label "
                       + string(g["label"]))
            end if
            append(labels, g["label"])
        next
        assigned = []
        for i = 0 to count(values) - 1
            found = unknown
            for each g in groups
                if contains(g["values"], values[i]) then
                    found = g["label"]
                    break
                end if
            next
            if is_unknown(found) then
                error ("scoring.bin_categorical: value " + string(values[i])
                       + " (row " + string(i + 1) + ") is in no group -- an"
                       + " unlisted category swept into an \"other\" bin is a"
                       + " category nobody decided about")
            end if
            append(assigned, found)
        next
        return _tally(labels, assigned, outcomes)
    end function

    ' Count goods and bads per bin, dropping bins nothing landed in.
    function _tally(labels, assigned, outcomes)
        goods = { }
        bads = { }
        for each l in labels
            goods[l] = 0
            bads[l] = 0
        next
        for i = 0 to count(assigned) - 1
            l = assigned[i]
            if _is_bad(outcomes[i]) then
                bads[l] = bads[l] + 1
            else
                goods[l] = goods[l] + 1
            end if
        next
        out = []
        for each l in labels
            if goods[l] + bads[l] > 0 then
                append(out, { label: l, goods: goods[l], bads: bads[l],
                              n: goods[l] + bads[l] })
            end if
        next
        return out
    end function

    ' --- weight of evidence --------------------------------------------------

    ' spec: { orientation: "good_bad"|"bad_good", smoothing: <n> }
    '
    ' AN EMPTY CELL IS REFUSED, NOT SMOOTHED (design §3). A bin with no bads
    ' gives ln(x/0); both infinities are ORDINARY in real data. Smoothing is a
    ' legitimate choice and is supported -- but it INVENTS EVIDENCE, and how
    ' much depends on the bin size in a way the output does not show, so it is
    ' the caller's declaration and its absence is a refusal.
    function woe_table(bins, spec)
        if type(bins) != "array" or count(bins) = 0 then
            error "scoring.woe_table expects a non-empty array of bins"
        end if
        if type(spec) != "record" then
            error "scoring.woe_table expects a spec record"
        end if
        orientation = spec["orientation"]
        if is_unknown(orientation) then
            error ("scoring.woe_table needs an orientation: \"good_bad\" gives"
                   + " higher WOE to better risk, \"bad_good\" is its negative,"
                   + " and neither is the default")
        end if
        checked = _check_orientation(orientation)
        smoothing = spec["smoothing"]
        if is_unknown(smoothing) then
            smoothing = 0
        end if
        if type(smoothing) != "number" or smoothing < 0 then
            error "scoring.woe_table: smoothing must be a number, 0 or more"
        end if

        total_good = 0
        total_bad = 0
        for each b in bins
            total_good = total_good + b.goods + smoothing
            total_bad = total_bad + b.bads + smoothing
        next
        if total_good <= 0 or total_bad <= 0 then
            error ("scoring.woe_table: the population has " + string(total_good)
                   + " goods and " + string(total_bad) + " bads")
        end if

        rows = []
        iv = 0
        for each b in bins
            g = b.goods + smoothing
            d = b.bads + smoothing
            if g <= 0 or d <= 0 then
                which = "bads"
                if g <= 0 then
                    which = "goods"
                end if
                error ("scoring.woe_table: bin " + string(b.label) + " has no "
                       + which + ", so its weight of evidence is infinite."
                       + " Re-bin, or declare a smoothing -- but note smoothing"
                       + " INVENTS evidence in proportion to how small the bin is")
            end if
            dist_good = g / total_good
            dist_bad = d / total_bad
            w = log(dist_good / dist_bad)
            if orientation = "bad_good" then
                w = 0 - w
            end if
            ' IV is orientation-INDEPENDENT: the sign of the log ratio and the
            ' sign of the difference flip together, so the product does not.
            part = (dist_good - dist_bad) * log(dist_good / dist_bad)
            iv = iv + part
            append(rows, { label: b.label, goods: b.goods, bads: b.bads, n: b.n,
                           dist_good: dist_good, dist_bad: dist_bad,
                           bad_rate: b.bads / b.n, woe: w, iv_part: part })
        next
        return { orientation: orientation, smoothing: smoothing,
                 rows: rows, iv: iv, goods: total_good, bads: total_bad }
    end function

    ' Look a value's WOE up in a table produced above. Used to transform a
    ' predictor into the column a logistic regression is fitted on.
    function woe_of(table, label)
        for each r in table.rows
            if r.label = label then
                return r.woe
            end if
        next
        error ("scoring.woe_of: no bin labelled " + string(label)
               + " -- a value the table never saw has no evidence to offer")
    end function

    ' --- discrimination ------------------------------------------------------

    ' AUC by the Mann-Whitney identity over ranks, ties taking half credit, so
    ' it is EXACT rather than a trapezoid over a sampled curve.
    '
    ' SCORES RUN THE SCORECARD WAY: higher means LOWER RISK. So this is
    ' P(a good outranks a bad).
    '
    ' AN AUC BELOW 0.5 MEANS THE MODEL IS BACKWARDS, NOT WEAK, and it is
    ' reported as it stands with `reversed: true` rather than flipped. Flipping
    ' silently turns the most consequential error in this work into a
    ' mediocre-looking result that gets deployed.
    function auc(scores, outcomes)
        if count(scores) != count(outcomes) then
            error ("scoring.auc: " + string(count(scores)) + " scores against "
                   + string(count(outcomes)) + " outcomes")
        end if
        tally = _check_outcomes(outcomes)
        ranks = _mid_ranks(scores)
        rank_sum_good = 0
        for i = 0 to count(scores) - 1
            if not _is_bad(outcomes[i]) then
                rank_sum_good = rank_sum_good + ranks[i]
            end if
        next
        ng = tally.goods
        nb = tally.bads
        a = (rank_sum_good - ng * (ng + 1) / 2) / (ng * nb)
        return { auc: a, gini: 2 * a - 1, reversed: a < 0.5,
                 goods: ng, bads: nb }
    end function

    ' Ranks 1..n ascending, tied values sharing the average of their positions.
    function _mid_ranks(xs)
        n = count(xs)
        order = []
        for i = 0 to n - 1
            append(order, i)
        next
        ' Insertion sort of indices by value. n here is a validation sample,
        ' not a book; the cost is recorded rather than optimised.
        for i = 1 to n - 1
            j = i
            while j > 0 and xs[order[j - 1]] > xs[order[j]]
                t = order[j - 1]
                order[j - 1] = order[j]
                order[j] = t
                j = j - 1
            end while
        next
        ranks = []
        for i = 0 to n - 1
            append(ranks, 0)
        next
        i = 0
        while i < n
            j = i
            while j + 1 < n and xs[order[j + 1]] = xs[order[i]]
                j = j + 1
            end while
            avg = (i + j + 2) / 2
            for k = i to j
                ranks[order[k]] = avg
            next
            i = j + 1
        end while
        return ranks
    end function

    ' The largest gap between the cumulative good and bad distributions.
    ' A DIFFERENT QUANTITY FROM AUC, not a restatement: AUC integrates over the
    ' whole range and KS reports one point, so two models can rank one way on
    ' AUC and the other way on KS.
    function ks(scores, outcomes)
        if count(scores) != count(outcomes) then
            error ("scoring.ks: " + string(count(scores)) + " scores against "
                   + string(count(outcomes)) + " outcomes")
        end if
        tally = _check_outcomes(outcomes)
        order = []
        for i = 0 to count(scores) - 1
            append(order, i)
        next
        for i = 1 to count(order) - 1
            j = i
            while j > 0 and scores[order[j - 1]] > scores[order[j]]
                t = order[j - 1]
                order[j - 1] = order[j]
                order[j] = t
                j = j - 1
            end while
        next
        cg = 0
        cb = 0
        best = 0
        at = unknown
        for each idx in order
            if _is_bad(outcomes[idx]) then
                cb = cb + 1
            else
                cg = cg + 1
            end if
            gap = abs(cb / tally.bads - cg / tally.goods)
            if gap > best then
                best = gap
                at = scores[idx]
            end if
        next
        return { ks: best, at: at }
    end function

    ' --- points --------------------------------------------------------------

    ' The standard linear-in-log-odds scaling. `pdo` is POINTS TO DOUBLE THE
    ' ODDS -- the number the business agrees to. All three are required: a
    ' default here silently redefines every cut-off downstream.
    function scaling(spec)
        if type(spec) != "record" then
            error "scoring.scaling expects a record"
        end if
        for each field in ["base_score", "base_odds", "pdo"]
            if is_unknown(spec[field]) then
                error ("scoring.scaling needs a " + field
                       + " -- a default would silently redefine every cut-off")
            end if
        next
        if spec["base_odds"] <= 0 then
            error "scoring.scaling: base_odds must be above zero"
        end if
        if spec["pdo"] = 0 then
            error "scoring.scaling: pdo of zero gives every case the same score"
        end if
        factor = spec["pdo"] / log(2)
        offset = spec["base_score"] - factor * log(spec["base_odds"])
        return { base_score: spec["base_score"], base_odds: spec["base_odds"],
                 pdo: spec["pdo"], factor: factor, offset: offset }
    end function

    ' Points from log odds, and back. HIGHER POINTS ALWAYS MEAN LOWER RISK:
    ' `log_odds` here is ln(good/bad), so it rises as risk falls.
    function points_of(sc, log_odds)
        return sc.offset + sc.factor * log_odds
    end function

    function log_odds_of(sc, pts)
        return (pts - sc.offset) / sc.factor
    end function

    ' --- stability -----------------------------------------------------------

    ' Population Stability Index over matching bands. Says the model has not
    ' changed but the applicants have. The customary thresholds are RULES OF
    ' THUMB and are returned as a label, never as a verdict.
    function psi(expected, actual)
        if count(expected) != count(actual) then
            error ("scoring.psi: " + string(count(expected)) + " expected bands"
                   + " against " + string(count(actual)) + " -- the bands must match")
        end if
        te = 0
        ta = 0
        for i = 0 to count(expected) - 1
            te = te + expected[i]
            ta = ta + actual[i]
        next
        if te <= 0 or ta <= 0 then
            error "scoring.psi: a population is empty"
        end if
        total = 0
        parts = []
        for i = 0 to count(expected) - 1
            e = expected[i] / te
            a = actual[i] / ta
            if e <= 0 or a <= 0 then
                error ("scoring.psi: band " + string(i + 1) + " is empty in one"
                       + " population, so its contribution is infinite -- widen"
                       + " the bands rather than reporting an infinite index")
            end if
            part = (a - e) * log(a / e)
            total = total + part
            append(parts, part)
        next
        note = "stable"
        if total >= 0.25 then
            note = "large shift"
        else if total >= 0.10 then
            note = "some shift"
        end if
        return { psi: total, parts: parts, note: note,
                 thresholds: "rule of thumb: 0.10 look, 0.25 act" }
    end function

end library
