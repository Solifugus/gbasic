' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' decision.bas — evaluates and chooses (docs/automation_reasoning_design.md §5).
' It never executes its recommendations.
'
' SECOND INCREMENT, from Recipe 5 (docs/automation_recipe_05_what_to_do.md),
' which is the first recipe to cross this boundary at all. It produced two
' refusals, and both are the reason this is a library rather than an `if`:
'
'   R9  A decision may not be sized off a quantity the Finding DECLINED TO
'       ESTABLISH. Measured: the same intervention over the same data gives
'       expected value +7,497 sized off the aggregate decline and -4,899 sized
'       off the cell that actually cleared -- ACT against DO NOT ACT -- and the
'       Finding had already said the aggregate was not established.
'
'   R6  Authority is stated, never enforced here. The recommendation is the
'       best alternative, not the best AFFORDABLE one; quietly returning the
'       affordable one hides the only choice a human needs to make.
library decision

    load reasoning from "reasoning.bas"
    load stats from "stats.bas"

    ' --- what a decision may be sized off ------------------------------------

    ' Both are quantities the Finding carries, and each has its own test for
    ' whether the Finding established it.
    function sizings()
        return ["aggregate", "leading_cell"]
    end function

    ' --- calibration ---------------------------------------------------------
    '
    ' TURNING THE LOOP. Recipe 5 ASSUMED a recovery of 0.6 and flagged its
    ' recommendation as sensitive to exactly that figure. Recipe 7 showed where
    ' such a figure comes from, and that measuring it without a control gives
    ' the wrong one. This is the third step: take controlled evidence and
    ' produce the assumption.
    '
    ' IT TAKES `as_evidence` RESULTS, NOT NUMBERS. That is the whole point --
    ' R10 has already refused anything uncontrolled, so a calibration cannot be
    ' built from observations of what merely happened next.
    '
    ' AND WHAT IT RETURNS IS AN INTERVAL, NOT A NUMBER. "How much evidence is
    ' enough" has no answer in the abstract; it has an answer relative to A
    ' DECISION, and §14 below is where the two meet.
    function calibrate(evidences)
        if type(evidences) != "array" then
            error "decision.calibrate expects an array of evidence"
        end if
        n = count(evidences)
        if n < 2 then
            error ("decision.calibrate: " + string(n) + " observation is not a"
                   + " calibration -- an interval needs at least two, and a"
                   + " single measurement offers no way to tell a real effect"
                   + " from the one time it happened to work")
        end if
        effects = []
        for i = 0 to n - 1
            e = evidences[i]
            if type(e) != "record" or e["kind"] != "prior_action" then
                error ("decision.calibrate: item " + string(i + 1) + " is not"
                       + " evidence from reasoning.as_evidence")
            end if
            if e["controlled"] != true then
                error ("decision.calibrate: item " + string(i + 1) + " is not"
                       + " controlled -- calibrating from what merely happened"
                       + " next reproduces the regression it measured (R10)")
            end if
            if is_unknown(e["effect"]) then
                error ("decision.calibrate: item " + string(i + 1)
                       + " has no measured effect")
            end if
            append(effects, e["effect"])
        next
        m = mean(effects)
        sd = stdev(effects)
        se = sd / sqrt(n)
        ' A t interval, because the spread is estimated from the same handful
        ' of observations. With n small this is WIDE, and it is supposed to be.
        crit = stats.t_quantile(0.975, n - 1)
        return { n: n, estimate: m, sd: sd, standard_error: se,
                 low: m - crit * se, high: m + crit * se, level: 0.95,
                 from: "controlled outcomes" }
    end function

    ' --- evaluate ------------------------------------------------------------
    '
    ' finding       a reasoning Finding
    ' context       { objectives, thresholds, authority }
    ' alternatives  [{ name, cost, benefit }] or [{ name, cost, recovers }]
    ' spec          { sizing, recovery_range: [lo, hi] }
    function evaluate(finding, context, alternatives, spec)
        if type(finding) != "record" or is_unknown(finding["observation"]) then
            error "decision.evaluate expects a Finding"
        end if
        if type(context) != "record" then
            error "decision.evaluate expects a context record"
        end if
        if type(alternatives) != "array" or count(alternatives) = 0 then
            error "decision.evaluate expects a non-empty array of alternatives"
        end if
        if type(spec) != "record" then
            error "decision.evaluate expects a spec record"
        end if
        auth = context["authority"]
        if is_unknown(auth) or is_unknown(auth["spend_limit"]) then
            error ("decision.evaluate: the context needs an authority with a"
                   + " spend_limit -- an unset authority means NOTHING may"
                   + " execute, and leaving it out would silently mean the"
                   + " opposite (design §7)")
        end if

        needs_sizing = false
        for each a in alternatives
            if not is_unknown(a["recovers"]) then
                needs_sizing = true
            end if
        next

        loss = 0
        sized = { quantity: "none", established: true, value: 0 }
        if needs_sizing then
            sizing = spec["sizing"]
            if is_unknown(sizing) then
                error ("decision.evaluate: an alternative sized by `recovers`"
                       + " needs a declared `sizing` -- one of "
                       + join(sizings(), ", ") + ". They are different numbers"
                       + " and pick opposite actions (design R9)")
            end if
            if not contains(sizings(), sizing) then
                error ("decision.evaluate: sizing must be one of "
                       + join(sizings(), ", ") + " (got " + string(sizing) + ")")
            end if
            sized = _size(finding, sizing)
            ' R9, at the boundary rather than after the fact.
            if not sized.established then
                error ("decision.evaluate: sized off the " + sizing + ", which"
                       + " this finding did not establish -- " + sized.why
                       + ". Size off something the evidence supports, or gather"
                       + " more evidence (design R9)")
            end if
            loss = sized.value
        end if

        ' --- score every alternative. NOT just the affordable ones (R6).
        scored = []
        for i = 0 to count(alternatives) - 1
            a = alternatives[i]
            if is_unknown(a["name"]) or is_unknown(a["cost"]) then
                error ("decision.evaluate: alternative " + string(i + 1)
                       + " needs a name and a cost")
            end if
            benefit = a["benefit"]
            if is_unknown(benefit) then
                benefit = 0
                if not is_unknown(a["recovers"]) then
                    benefit = 0 - loss * a["recovers"]
                end if
            end if
            append(scored, { name: a["name"], cost: a["cost"], benefit: benefit,
                             expected_value: benefit - a["cost"],
                             within_authority: a["cost"] <= auth["spend_limit"] })
        next

        best = scored[0]
        for each o in scored
            if o.expected_value > best.expected_value then
                best = o
            end if
        next

        ' --- assurance is a SENSITIVITY, not a feeling.
        ' The charter's own example of a useful decision was "the recommendation
        ' is highly sensitive to the assumed churn response". So assurance here
        ' is the share of a DECLARED range of the recovery assumption over which
        ' the recommendation does not change, plus the point at which it does.
        assurance = unknown
        sensitivities = []
        if needs_sizing then
            rng = spec["sensitivity_range"]
            cal = spec["calibration"]
            if not is_unknown(cal) then
                ' THE RANGE STOPS BEING INVENTED. Recipe 5 swept an arbitrary
                ' [0, 2] because there was nothing better; a calibration
                ' supplies the range the EVIDENCE actually supports, so
                ' `assurance` becomes the share of the plausible interval over
                ' which the recommendation survives rather than the share of a
                ' span somebody chose.
                if is_unknown(cal["low"]) or is_unknown(cal["high"]) then
                    error "decision.evaluate: a calibration needs low and high"
                end if
                base = cal["estimate"]
                if base = 0 then
                    error ("decision.evaluate: the calibrated estimate is zero,"
                           + " so it cannot scale an assumed recovery")
                end if
                rng = [cal["low"] / base, cal["high"] / base]
                if rng[0] > 1 then
                    rng[0] = 1
                end if
                if rng[1] < 1 then
                    rng[1] = 1
                end if
            end if
            if is_unknown(rng) then
                error ("decision.evaluate: an alternative sized by `recovers`"
                       + " needs a `sensitivity_range` to be assured over."
                       + " A recommendation with no sensitivity is the"
                       + " \"Option A is best\" output this layer exists to"
                       + " improve on")
            end if
            ' The range is a MULTIPLIER on every alternative's own assumed
            ' recovery, so relative effectiveness is preserved -- sweeping a
            ' single shared fraction instead would give a cheap intervention
            ' and an expensive one identical benefit, and the cheap one would
            ' always win. It must BRACKET 1, or the sweep never visits the case
            ' the recommendation was actually made under and `assurance` would
            ' be a statement about a scenario nobody proposed.
            if count(rng) != 2 or rng[0] > 1 or rng[1] < 1 then
                error ("decision.evaluate: sensitivity_range must be [lo, hi]"
                       + " with lo <= 1 <= hi -- it scales the assumed"
                       + " recoveries, so a range excluding 1 never visits the"
                       + " assumption the recommendation was made under")
            end if
            swept = _sweep(alternatives, auth, loss, rng, best.name)
            assurance = swept.held
            sensitivities = swept.sensitivities
            ' THE INVARIANT THAT CATCHES A BROKEN SWEEP. At a scale of 1 the
            ' sweep is computing exactly what the point estimate computed, so
            ' it must reach the same recommendation. The first version of
            ' `_sweep` did not -- it cancelled each alternative's own recovery,
            ' gave a cheap intervention and an expensive one identical benefit,
            ' and reported assurance 0 for a recommendation it never once
            ' picked. Every assertion in the fixture still passed.
            if swept.at_nominal != best.name then
                error ("decision.evaluate: the sensitivity sweep disagrees with"
                       + " the point estimate at the nominal assumption ("
                       + swept.at_nominal + " against " + best.name
                       + ") -- one of them is wrong and the recommendation"
                       + " cannot be trusted either way")
            end if
        end if

        material = _materiality(finding, context)

        return reasoning.decision({
            objective: _objective(context, finding),
            finding_subject: finding["subject"],
            materiality: material,
            alternatives: scored,
            recommendation: best.name,
            expected_value: best.expected_value,
            ' R6: STATED, not enforced. The recommendation above is the best
            ' alternative, not the best affordable one.
            authority_required: not best.within_authority,
            authority_reason: _authority_reason(best, auth),
            assurance: assurance,
            sensitivities: sensitivities,
            sized_off: { quantity: sized.quantity, established: sized.established,
                         value: sized.value },
            provenance: reasoning.provenance({
                method: "evaluate/expected_value",
                rows: count(alternatives),
                parameters: { sizing: spec["sizing"],
                              sensitivity_range: spec["sensitivity_range"],
                              calibrated_from: _cal_n(spec["calibration"]),
                              spend_limit: auth["spend_limit"] },
                assumptions: ["alternatives are mutually exclusive",
                              "the recovery fraction is the only uncertainty swept"] }) })
    end function

    ' --- internals -----------------------------------------------------------

    ' §4.6: materiality is computed HERE, from the context, and never on the
    ' Finding -- it needs objectives and the insight layer does not have them.
    function _materiality(finding, context)
        thresholds = context["thresholds"]
        m = finding["measure"]
        t = unknown
        if not is_unknown(thresholds) then
            t = thresholds[m]
        end if
        change = finding["observation"]["change"]
        if is_unknown(t) then
            return { threshold: unknown, change: change, is_material: unknown,
                     why: "no threshold declared for " + string(m) }
        end if
        return { threshold: t, change: change, is_material: abs(change) >= t,
                 why: "" }
    end function

    function _objective(context, finding)
        objs = context["objectives"]
        if is_unknown(objs) or count(objs) = 0 then
            return { measure: finding["measure"], direction: "unstated" }
        end if
        for each o in objs
            if o["measure"] = finding["measure"] then
                return o
            end if
        next
        return objs[0]
    end function

    function _size(finding, sizing)
        if sizing = "aggregate" then
            ok = finding["shares_reportable"]
            if is_unknown(ok) then
                ok = false
            end if
            why = ""
            if not ok then
                why = finding["shares_withheld_because"]
                if is_unknown(why) then
                    why = "the finding did not establish the aggregate change"
                end if
            end if
            return { quantity: "aggregate", established: ok, why: why,
                     value: finding["observation"]["change"] }
        end if
        top = finding["contributors"][0]
        ok = finding["strength"]["clears"]
        why = ""
        if not ok then
            why = ("the leading cell does not clear the threshold for a search"
                   + " of " + string(finding["search"]["cells"]) + " cells")
        end if
        return { quantity: "leading_cell", established: ok, why: why,
                 value: top["change"] }
    end function

    ' Sweep the recovery assumption across its declared range and ask how often
    ' the recommendation survives. Twenty-one steps is enough to locate a
    ' crossing to a twentieth of the range and cheap at this scale.
    function _sweep(alternatives, auth, loss, rng, chosen)
        lo = rng[0]
        hi = rng[1]
        steps = 20
        held = 0
        tried = 0
        flips = []
        previous = ""
        for k = 0 to steps
            r = lo
            if steps > 0 then
                r = lo + (hi - lo) * k / steps
            end if
            best = ""
            best_ev = 0 - 999999999999
            for each a in alternatives
                benefit = a["benefit"]
                if is_unknown(benefit) then
                    benefit = 0
                    if not is_unknown(a["recovers"]) then
                        benefit = 0 - loss * a["recovers"] * r
                    end if
                end if
                ev = benefit - a["cost"]
                if ev > best_ev then
                    best_ev = ev
                    best = a["name"]
                end if
            next
            tried = tried + 1
            if best = chosen then
                held = held + 1
            end if
            if previous != "" and best != previous then
                append(flips, { at: round(r, 4), from: previous, to: best })
            end if
            previous = best
        next
        return { held: held / tried, sensitivities: flips,
                 at_nominal: _winner(alternatives, loss, 1) }
    end function

    ' The winner at a given scale, shared by the sweep and by the nominal check.
    function _winner(alternatives, loss, r)
        best = ""
        best_ev = 0 - 999999999999
        for each a in alternatives
            benefit = a["benefit"]
            if is_unknown(benefit) then
                benefit = 0
                if not is_unknown(a["recovers"]) then
                    benefit = 0 - loss * a["recovers"] * r
                end if
            end if
            ev = benefit - a["cost"]
            if ev > best_ev then
                best_ev = ev
                best = a["name"]
            end if
        next
        return best
    end function

    function _cal_n(cal)
        if is_unknown(cal) then
            return unknown
        end if
        return cal["n"]
    end function

    function _authority_reason(best, auth)
        if best.within_authority then
            return ("cost " + string(best.cost) + " is within the spend limit of "
                    + string(auth["spend_limit"]))
        end if
        return ("cost " + string(best.cost) + " exceeds the spend limit of "
                + string(auth["spend_limit"]) + " -- this decision is a"
                + " recommendation and needs approval before it may execute")
    end function

end library
