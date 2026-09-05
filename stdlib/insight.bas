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
    '   max_causes    optional, default 1 -- how many cells may be wrong at
    '                 once. Raising it is what lets a second cause be seen and
    '                 costs every cause a higher bar (R14).
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

        ' R14. How many cells may be wrong at once. The default is 1 because
        ' that is what a leave-one-out reference has always silently assumed --
        ' naming it does not change any existing answer, it makes the
        ' assumption arguable.
        ' R18. HOW MANY TIMES THIS SEARCH WILL BE RUN. The correction has
        ' always been family-wise over the CELLS of ONE search, and nothing
        ' anywhere said so -- so a monitoring process re-asking the same
        ' question every month is running twelve families and paying for one.
        ' Measured over a population with NOTHING wrong in it: 0.10 per run,
        ' and 0.725 that some month raises a finding within the year. Even a
        ' perfectly calibrated 0.05 per run is 0.46 over twelve.
        '
        ' Default 1, which is what the library always silently assumed, so
        ' naming it changes no existing answer. AND IT IS NOT FREE, which is
        ' why it is declared rather than guessed: at 20 cells the bar goes
        ' 3.51 -> 4.63 for a year of monthly runs, 5.31 weekly, 6.25 daily,
        ' and by R15 the bar IS the smallest change the search can find. What
        ' it buys was measured over a population with nothing wrong in it:
        ' the chance that some month raises a finding falls from 0.725 to
        ' 0.175. It does not reach the requested 0.05, for the tail-weight
        ' reason `null.calibration` already records, deeper into the tail.
        repetitions = spec["repetitions"]
        if is_unknown(repetitions) then
            repetitions = 1
        end if
        if type(repetitions) != "number" or repetitions < 1 or floor(repetitions) != repetitions then
            error ("insight.explain_change: repetitions must be a whole number"
                   + " of runs, at least 1 -- it is how many times this same"
                   + " search will be asked, and the correction is family-wise"
                   + " over cells TIMES runs (design R18)")
        end if

        max_causes = spec["max_causes"]
        if is_unknown(max_causes) then
            max_causes = 1
        end if
        if type(max_causes) != "number" or max_causes < 1 or floor(max_causes) != max_causes then
            error ("insight.explain_change: max_causes must be a whole number"
                   + " of cells, at least 1")
        end if
        trim = max_causes - 1
        if n - 1 - trim < 3 then
            error ("insight.explain_change: max_causes " + string(max_causes)
                   + " would leave " + string(n - 1 - trim) + " cells to define"
                   + " ordinary out of " + string(n) + " -- allowing for that"
                   + " many simultaneous causes in a search this narrow means"
                   + " there is no population left to judge them against")
        end if
        if trim > 0 and spec["null"] != "siblings_permuted" then
            ' NOT a matter of precision. The t quantile is a formula for the
            ' UNTRIMMED statistic, and trimming the reference deflates its
            ' spread under the null too -- measured at 24 cells, the null 95th
            ' percentile of max|z| rises 3.74 -> 4.30 -> 4.76 -> 5.74 as the
            ' trim goes 0 -> 1 -> 2 -> 4, while the t formula answers 3.49
            ' whatever the trim. Using it here would not be approximately
            ' right; it would be a threshold for a different statistic, and it
            ' errs towards reporting causes that are not there.
            error ("insight.explain_change: max_causes above 1 needs"
                   + " null: \"siblings_permuted\" -- trimming the reference"
                   + " changes the statistic, and the t threshold is a formula"
                   + " for the untrimmed one. The permuted null takes its"
                   + " threshold from the statistic actually used, so it"
                   + " follows the trim; the t formula does not and would"
                   + " report causes that are not there")
        end if

        threshold = 0
        calibration = { }
        if spec["null"] = "siblings_permuted" then
            ' A PERMUTED THRESHOLD IS AN ESTIMATED QUANTILE, and an estimate of
            ' a quantile needs draws OUT IN THE TAIL IT IS ESTIMATING. The
            ' index is floor((1 - alpha) * draws), so once alpha falls below
            ' about 1/draws the answer is simply the LARGEST of the draws --
            ' a random variable with no stated coverage, returned silently and
            ' looking exactly like a threshold.
            '
            ' It was reachable before only through an unusual `alpha`. R18 made
            ' it reachable by an ORDINARY declaration: `repetitions: 12` divides
            ' alpha by twelve, and at the default 200 draws that asks for rank
            ' 199 of 200. Requiring at least ten draws beyond the threshold is
            ' the rule, and the default pair (alpha 0.05, 200 draws) sits
            ' exactly on it, which is not a coincidence -- 200 is the smallest
            ' draw count that supports the default alpha.
            tail = alpha / repetitions * draws
            if tail < 10 then
                error ("insight.explain_change: a permuted threshold at alpha "
                       + string(alpha) + " over " + string(repetitions)
                       + " run(s) needs at least "
                       + string(ceil(10 * repetitions / alpha)) + " draws and"
                       + " has " + string(draws) + " -- with fewer, the"
                       + " threshold is just the largest draw and its coverage"
                       + " is not the alpha you asked for. Raise `draws`, or"
                       + " use null: \"siblings\" and accept its tail-weight"
                       + " assumption instead")
            end if
            ' Dividing alpha by the repetitions is the Bonferroni step ACROSS
            ' runs, applied on top of a within-run null that is already exact
            ' family-wise over the cells.
            threshold = _permuted_threshold(cells, alpha / repetitions, draws,
                                            permute_seed, trim)
            calibration = { method: "period-label permutation",
                            assumed: false,
                            draws: draws, seed: permute_seed,
                            measured_null_rate: ("0.085 at 20 cells and 0.050 at"
                                                 + " 40, at 200 draws; 0.070 at 20"
                                                 + " cells at 800 draws"
                                                 + " (lognormal revenue, 200 trials"
                                                 + " each). More draws estimate the"
                                                 + " quantile better and cost"
                                                 + " proportionally more"),
                            note: ("the threshold comes from this data's own"
                                   + " variability, so it follows the tails"
                                   + " rather than assuming them") }
        else
            ' The family is cells TIMES runs. df stays n - 2: the degrees of
            ' freedom are a fact about the data of THIS run, not about how often
            ' the question is asked.
            threshold = stats.t_quantile(1 - alpha / (2 * n * repetitions), n - 2)
            calibration = { method: "t quantile", assumed: true,
                            measured_null_rate: ("0.047 light-tailed; 0.100 at 20"
                                                 + " cells and 0.130 at 40 for"
                                                 + " lognormal revenue"),
                            note: ("EXACT ONLY FOR LIGHT-TAILED cell changes."
                                   + " A change is a difference of two"
                                   + " independent sums so its skew cancels;"
                                   + " tail weight does not, and revenue-like"
                                   + " data has it. Declare"
                                   + " null: \"siblings_permuted\" to take the"
                                   + " threshold from the data instead") }
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
        ' --- R13. THE SECOND WAY A SHARE LIES, and it is not R2's way.
        ' Recipe 3 compared December with January -- the most ordinary seasonal
        ' comparison in commerce -- over a population with NOTHING wrong in it.
        ' Revenue "fell" 50.1%, the net test established it at t = -4.49, and
        ' shares were duly reported: a confident attribution of an entirely
        ' ordinary January to whichever cell happened to sell the most toys.
        '
        ' The signature is visible in the data and needs nothing extra: 22 of
        ' 24 cells moved the SAME WAY. Under any null where a cell is as likely
        ' to rise as to fall, that is a one-in-thirty-thousand event -- the
        ' population moved TOGETHER, and a movement common to every cell is not
        ' explained by any of them. A sign test says so, and it assumes less
        ' than the t test already assumes: only that a cell is as likely to
        ' rise as to fall, never anything about the shape of how far.
        '
        ' THIS IS NOT A SEASONALITY DETECTOR AND MUST NOT BE READ AS ONE. A
        ' real company-wide collapse moves 22 of 24 cells down too, and so does
        ' a broken feed. What the test establishes is the same thing R2
        ' establishes one level down -- that the DECOMPOSITION HAS NOT LOCATED
        ' THIS -- and the honest report of a common movement is that it
        ' happened everywhere, not that Northeast caused 82.6% of it.
        down = 0
        up = 0
        for each d in changes
            if d < 0 then
                down = down + 1
            end if
            if d > 0 then
                up = up + 1
            end if
        next
        ' The sign test uses the SAME declared alpha, uncorrected, and that is
        ' deliberate: the search width is a family-wise rate over n cells
        ' because a drill-down IS a search, while this is ONE test asked once,
        ' so there is no family to correct for. Both are the caller's declared
        ' tolerance for being wrong; only one of them is spent n times.
        moved = down + up
        sign_p = 1
        if moved > 0 then
            fewer = down
            if up < down then
                fewer = up
            end if
            sign_p = 2 * stats.binom_cdf(fewer, moved, 0.5)
            if sign_p > 1 then
                sign_p = 1
            end if
        end if
        common = sign_p < alpha

        reportable = abs(t) >= 2
        withheld = ""
        if not reportable then
            withheld = ("the net change is not distinguishable from zero"
                        + " (t = " + string(round(t, 2)) + " over " + string(n)
                        + " cells), so a share of it would be a share of"
                        + " something not established")
        end if
        if reportable and common then
            reportable = false
            bigger = down
            way = "down"
            if up > down then
                bigger = up
                way = "up"
            end if
            withheld = ("this movement is common to the population -- "
                        + string(bigger) + " of " + string(moved)
                        + " cells moved " + way + " (sign test p = "
                        + string(round(sign_p, 5)) + "), so it happened"
                        + " everywhere and the decomposition has not located"
                        + " it. A share would attribute to one cell what every"
                        + " cell did. Seasonality, a common shock and a broken"
                        + " feed all look like this and the data cannot tell"
                        + " them apart; compare like with like"
                        + " (comparison: \"versus_last_year\") if the measure"
                        + " has a season (design R13)")
        end if

        ' --- R15. WHAT THIS SEARCH COULD HAVE FOUND -----------------------
        '
        ' `within ordinary variation` is returned in identical words by a
        ' search that examined a healthy business and by one that could not
        ' have found a cell going to zero. Recipe 4 measured the difference and
        ' it is not small: cutting one business of fixed size into 24, 240 and
        ' 1200 cells moves the threshold only 3.49 -> 3.77 -> 4.11, because
        ' sqrt(2 ln n) grows about as slowly as anything in statistics -- but
        ' the smallest change that could clear it goes from 18% of a typical
        ' cell to 53% to 147%. At the finest cut NO DECLINE, however complete,
        ' could be reported: a cell's whole revenue is smaller than the bar.
        '
        ' So a Finding states the smallest change it was capable of finding,
        ' in the units of the business and as a share of a typical cell, and it
        ' states it WHETHER OR NOT anything cleared -- because "nothing
        ' cleared" is exactly when a reader needs to know whether the search
        ' was able to clear anything at all.
        '
        ' The spread used is the population's, not a particular cell's
        ' leave-one-out reference; those differ by O(1/n) and a headline figure
        ' that varied by cell would not be a headline.
        baselines = []
        for each k in cells.order
            append(baselines, cells.baseline[k])
        next
        typical_cell = median(baselines)
        detectable_change = threshold * sd
        detectable_share = unknown
        if typical_cell != 0 then
            detectable_share = detectable_change / typical_cell
        end if

        sums = _sums(changes)

        contributors = []
        ci = 0
        for each k in cells.order
            d = cells.current[k] - cells.baseline[k]
            ' Leave-one-out: this cell is judged against the OTHERS, never
            ' against a spread it is itself part of -- and, when the caller has
            ' allowed for more than one cause, never against the other
            ' candidates either (R14).
            if trim = 0 then
                z = _loo_z(d, n, sums.total, sums.total_sq)
            else
                z = _trimmed_z(changes, ci, trim)
            end if
            ci = ci + 1
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
        clearing = []
        for each c in contributors
            if c.clears then
                append(clearing, c.path)
            end if
        next

        top = contributors[0]
        assoc = _associations(cells, assoc_names, changes)

        prov = reasoning.provenance({
                 method: "explain_change/siblings",
                 rows: count(rows),
                 parameters: { measure: spec["measure"], period: spec["period"],
                               baseline: spec["baseline"], current: spec["current"],
                               dimensions: dims, comparison: spec["comparison"],
                               null: spec["null"], alpha_requested: alpha,
                               repetitions: repetitions,
                               threshold_method: calibration["method"],
                               threshold_assumed: calibration["assumed"] },
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
            ' `alpha_requested`, NOT `alpha`. What was asked for is not what
            ' was achieved: measured, the t threshold delivers 0.10-0.13 on
            ' revenue-like data against a requested 0.05. The field name is the
            ' retraction -- a Finding may state what error rate was REQUESTED
            ' and must not imply it was DELIVERED. `null.calibration` carries
            ' what is actually known.
            search: { dimensions: dims, cells: n, width: threshold,
                      alpha_requested: alpha, correction: _correction(repetitions),
                      repetitions: repetitions,
                      max_causes: max_causes,
                      detectable: { change: detectable_change,
                                    typical_cell: typical_cell,
                                    share: detectable_share } },
            null: { kind: spec["null"], mean: m, sd: sd, threshold: threshold,
                    net_t: t, standardized: "leave_one_out", df: n - 2,
                    common_movement: { down: down, up: up, p: sign_p,
                                       common: common },
                    calibration: calibration,
                    draws: draws, permute_seed: permute_seed },
            ' `leader` is ONE cell, and R14 is the case where that is not
            ' enough: two cells can clear and a reader of `strength` alone
            ' would see one. `clearing` names all of them.
            strength: { z: top.z, clears: top.clears, leader: top.path,
                        clearing: clearing },
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

    ' --- R14. THE REFERENCE MUST EXCLUDE THE OTHER CANDIDATES ----------------
    '
    ' Leave-one-out removes a cell from the spread it is judged against, which
    ' is what Recipe 6 fixed. It removes NOTHING ELSE, and Recipe 2 measured
    ' what that costs when more than one thing has gone wrong at once -- the
    ' ordinary condition of a business rather than an exotic one.
    '
    ' The experiment holds ONE cell literally constant: the same collapse, a
    ' change of -25,728, in every run. Only the number of OTHER, unrelated
    ' cells collapsing beside it varies
    ' (examples/automation_lab/10_two_causes_at_once.bas):
    '
    '   others broken   population mean   population sd       z   verdict
    '               0              -666           6,974   -5.70   found
    '               1            -1,643           8,472   -3.65   found
    '               2            -2,663           9,668   -2.83   nothing
    '               4            -4,942          12,213   -1.86   nothing
    '
    ' Both terms of the standardisation move against detection at once: the
    ' reference mean slides towards the anomaly and the reference spread
    ' inflates. Nothing in the watched cell changed. The test finds a problem
    ' only while it is nearly the only problem, and deepening the collapse does
    ' not help, because the cells contaminating the reference deepen with it.
    '
    ' TWO OBVIOUS REPAIRS WERE MEASURED AND BOTH FAILED, which is why this one
    ' is a DECLARED CHOICE rather than a better default. Sequential peeling --
    ' test the most extreme, remove it, test the next -- does not help, because
    ' the FIRST test is the most contaminated and it is the one that decides
    ' whether anything is reported at all. A robust median/MAD scale does not
    ' help either: at 24 cells the MAD itself rose 28% between one cause and
    ' two, and once its own null threshold is measured honestly (4.18 against
    ' the t formula's 3.49) the robust statistic is FURTHER from clearing.
    '
    ' What works is excluding the other candidates from the REFERENCE -- not
    ' blessing them as findings, merely declining to let them define what
    ' ordinary looks like. It restores the statistic to a property of the cell:
    ' the trimmed z is -5.59, -5.46, -5.48 across the same three runs whose
    ' untrimmed z fell from -3.65 to -1.86. AND IT IS NOT FREE. The bar rises
    ' with what is allowed for -- 4.35, 4.84, 6.46 -- so by five causes in
    ' twenty-four cells it has outrun the evidence. A trade with a limit, not a
    ' repair, and `max_causes` therefore belongs to the caller like the null
    ' and the comparison. Its default is 1 because that is what this library
    ' has always silently assumed; naming it changes no existing answer.
    function _trimmed_z(xs, at, trim)
        n = count(xs)
        if n - 1 - trim < 3 then
            return 0
        end if
        others = []
        for j = 0 to n - 1
            if j != at then
                append(others, xs[j])
            end if
        next
        for cut = 1 to trim
            m = mean(others)
            worst = 0
            wd = 0 - 1
            for j = 0 to count(others) - 1
                d = abs(others[j] - m)
                if d > wd then
                    wd = d
                    worst = j
                end if
            next
            remove(others, worst)
        next
        m = mean(others)
        sd = stdev(others)
        if sd <= 0 then
            return 0
        end if
        return (xs[at] - m) / sd
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
    ' The statistic is defined HERE and nowhere else, which is what lets the
    ' permuted threshold follow `trim` without knowing anything about it: a
    ' threshold computed from a different statistic than the one it judges is
    ' the defect this shape prevents.
    function _max_abs_z(xs, trim)
        n = count(xs)
        best = 0
        if trim = 0 then
            s = _sums(xs)
            for each v in xs
                z = abs(_loo_z(v, n, s.total, s.total_sq))
                if z > best then
                    best = z
                end if
            next
            return best
        end if
        for j = 0 to n - 1
            z = abs(_trimmed_z(xs, j, trim))
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
    ' R18. The correction NAMES ITS FAMILY. "bonferroni" alone left a reader
    ' to assume the family was whatever they had in mind, and what it actually
    ' was is the cells of one search.
    function _correction(repetitions)
        if repetitions = 1 then
            return "bonferroni over the cells of this one search"
        end if
        return ("bonferroni over the cells of this search times "
                + string(repetitions) + " runs")
    end function

    function _permuted_threshold(cells, alpha, draws, seed, trim)
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
            append(maxes, _max_abs_z(changes, trim))
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
