' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' reasoning.bas — the shared value model for Business Automation Reasoning
' (docs/automation_reasoning_design.md §5, §6).
'
' NOT A FOURTH LAYER. `insight`, `decision` and `automation` all construct and
' inspect these values, and none of them should have to depend on another to do
' it. This library has no behaviour beyond construction, validation and
' provenance.
'
' THE VALIDATION IS WHERE THE DESIGN'S CORRECTIONS LIVE. Recipe 1 found three
' things wrong with the original charter's Finding, and all three are refusals
' here rather than advice in a document:
'
'   * a Finding with no SEARCH WIDTH cannot be judged (§4.3, R1);
'   * MATERIALITY is not a property of a Finding -- it needs objectives, which
'     belong to the decision layer, and putting it here forces `insight` to
'     know the business's goals (§4.6);
'   * a Finding never carries a CAUSE. It carries associations, and a
'     hypothesis becomes an explanation only through a recorded test (R3).
library reasoning

    ' --- what is declared ----------------------------------------------------

    ' R8: what counts as ORDINARY is a modelling choice that changes every
    ' answer downstream, so it is declared and never inferred.
    '
    '   siblings           the other cells, with a threshold from the t
    '                      distribution. Exactly calibrated when cell changes
    '                      are light-tailed and ANTI-CONSERVATIVE when they are
    '                      not -- measured at 0.047 against a requested 0.05 for
    '                      uniform data and 0.110 for lognormal revenue.
    '   siblings_permuted  the same cells, with the threshold taken from THIS
    '                      DATA'S OWN TAILS by sign-flipping. Costs B passes and
    '                      assumes only that a deviation is as likely up as down.
    '
    ' Both are wrong under seasonality, which is a temporal structure neither
    ' looks at. That is why the list is short and adding to it is deliberate.
    function nulls()
        return ["siblings", "siblings_permuted"]
    end function

    ' The comparison is likewise declared: period over period, versus the same
    ' period last year, and versus forecast disagree routinely and each is
    ' right for a different question.
    function comparisons()
        return ["period_over_period"]
    end function

    ' §4.5. Three quantities that shared one word and one threshold in the
    ' charter. They share no scale, so they may not be compared (R4).
    function confidence_kinds()
        return ["confidence", "support", "assurance"]
    end function

    ' --- R4 ------------------------------------------------------------------

    ' Comparing a statistical confidence with a decision's assurance is the
    ' mistake the charter's single `confidence` invited. Refused by name rather
    ' than coerced, since both are numbers in 0..1 and nothing would complain.
    function compare_confidence(kind_a, a, kind_b, b)
        for each k in [kind_a, kind_b]
            if not contains(confidence_kinds(), k) then
                error ("reasoning: " + string(k) + " is not a confidence kind -- "
                       + join(confidence_kinds(), ", "))
            end if
        next
        if kind_a != kind_b then
            error ("reasoning: cannot compare " + kind_a + " with " + kind_b
                   + " -- they answer different questions and share no scale."
                   + " `confidence` is how well a quantity is estimated,"
                   + " `support` how well a hypothesis accounts for the evidence,"
                   + " `assurance` how sure we are an action is right")
        end if
        if a > b then
            return 1
        end if
        if a < b then
            return 0 - 1
        end if
        return 0
    end function

    ' --- Finding -------------------------------------------------------------

    ' Fields a Finding must never carry, each with the reason, because the
    ' reason is the whole design decision and an author who reaches for one is
    ' making exactly the mistake it records.
    function _forbidden(field)
        if field = "materiality" then
            return ("materiality is not a property of a Finding: it needs"
                    + " objectives, objectives belong to the decision layer, and"
                    + " putting it here forces `insight` to know the business's"
                    + " goals (design §4.6). Compute it in `decision`, from the"
                    + " Context")
        end if
        if field = "cause" or field = "causes" then
            return ("a Finding carries `associations`, never a cause. A"
                    + " hypothesis becomes an explanation only by passing a"
                    + " declared test whose result is recorded (design R3)")
        end if
        if field = "assurance" then
            return ("`assurance` is the decision layer's -- how sure we are an"
                    + " action is right. A Finding carries `confidence` (design"
                    + " §4.5)")
        end if
        return ""
    end function

    function finding(spec)
        if type(spec) != "record" then
            error "reasoning.finding expects a record"
        end if
        for each field in keys(spec)
            why = _forbidden(field)
            if why != "" then
                error "reasoning.finding: " + why
            end if
        next
        for each field in ["subject", "measure", "observation", "search",
                           "null", "strength", "contributors", "provenance"]
            if is_unknown(spec[field]) then
                error "reasoning.finding needs a " + field
            end if
        next

        ' R1. The one that is easiest to omit and most consequential: the
        ' significance cut is a function of how many cells were searched, so a
        ' Finding without a width cannot be judged, and two Findings from
        ' searches of different width cannot be compared at all.
        s = spec["search"]
        if is_unknown(s["cells"]) or s["cells"] <= 0 then
            error ("reasoning.finding: `search.cells` is required and must be"
                   + " above zero -- the significance threshold is a function of"
                   + " how wide the search was, so a finding that cannot state"
                   + " it cannot be judged (design R1)")
        end if
        if is_unknown(s["width"]) then
            error "reasoning.finding: `search.width` is required (design R1)"
        end if

        ' R8.
        n = spec["null"]
        if is_unknown(n["kind"]) or not contains(nulls(), n["kind"]) then
            error ("reasoning.finding: the null must be declared and be one of "
                   + join(nulls(), ", ") + " -- what counts as ordinary changes"
                   + " every answer downstream (design R8)")
        end if

        out = { subject: spec["subject"], measure: spec["measure"],
                observation: spec["observation"], search: s, null: n,
                strength: spec["strength"], contributors: spec["contributors"],
                provenance: spec["provenance"] }
        for each field in ["period", "comparison", "associations", "hypotheses",
                           "confidence", "shares_reportable", "shares_withheld_because"]
            if not is_unknown(spec[field]) then
                out[field] = spec[field]
            end if
        next
        if is_unknown(out["associations"]) then
            out["associations"] = []
        end if
        if is_unknown(out["hypotheses"]) then
            out["hypotheses"] = []
        end if
        return out
    end function

    ' --- Decision ------------------------------------------------------------

    ' The mirror of `finding`'s refusals, one layer along. A Decision carries
    ' `assurance`, never `confidence` -- how sure we are an ACTION is right is
    ' not how well a quantity is estimated (§4.5) -- and it must state what
    ' authority it needs, because R6 puts the enforcement at the action and an
    ' unstated requirement cannot be enforced anywhere.
    function _forbidden_decision(field)
        if field = "confidence" then
            return ("`confidence` is the insight layer's -- how well a quantity"
                    + " is estimated. A Decision carries `assurance`: how sure"
                    + " we are the recommended action is right (design §4.5)")
        end if
        if field = "authorized" or field = "permitted" then
            return ("a Decision does not decide whether it is allowed. It states"
                    + " the authority it REQUIRES, and enforcement happens at"
                    + " the action -- a decision may freely recommend what it"
                    + " may not execute, and suppressing that hides the case a"
                    + " human most needs to see (design R6)")
        end if
        return ""
    end function

    function decision(spec)
        if type(spec) != "record" then
            error "reasoning.decision expects a record"
        end if
        for each field in keys(spec)
            why = _forbidden_decision(field)
            if why != "" then
                error "reasoning.decision: " + why
            end if
        next
        for each field in ["objective", "alternatives", "recommendation",
                           "expected_value", "authority_required", "sized_off",
                           "provenance"]
            if is_unknown(spec[field]) then
                error "reasoning.decision needs a " + field
            end if
        next
        ' R9. The quantity a decision was sized off must be one the Finding
        ' ESTABLISHED. Recorded here so the chain is auditable even when the
        ' decision layer got it right by accident.
        so = spec["sized_off"]
        if is_unknown(so["quantity"]) or is_unknown(so["established"]) then
            error ("reasoning.decision: `sized_off` must name the quantity the"
                   + " decision was sized off and whether the finding"
                   + " established it (design R9)")
        end if
        if so["established"] != true then
            error ("reasoning.decision: sized off " + string(so["quantity"])
                   + ", which the finding did NOT establish -- a decision built"
                   + " on a quantity its own evidence declined to establish is"
                   + " the failure recipe 5 exists to prevent (design R9)")
        end if
        out = { }
        for each field in keys(spec)
            out[field] = spec[field]
        next
        return out
    end function

    ' --- Hypothesis ----------------------------------------------------------

    ' §4's ladder: observation -> association -> CAUSAL HYPOTHESIS -> test ->
    ' supported or rejected explanation. A Hypothesis is the third rung and it
    ' is never the fifth. It carries `explains: false` for its whole life here,
    ' because nothing in this library can promote it -- only a recorded test
    ' can, and that is R3.
    '
    ' `predicts` is a record of dimension -> value constraints, ALL of which
    ' must hold for a cell to be predicted. It is declarative rather than a
    ' function value on purpose: a prediction that cannot be written into
    ' provenance cannot be audited later, and "why did we believe that" is the
    ' question this whole layer exists to answer.
    '
    ' `discriminator` is REQUIRED, and that is the opinionated part. A
    ' hypothesis you cannot imagine an observation for is not a hypothesis, it
    ' is a story -- and a story that scores well against a pattern is exactly
    ' the failure mode this design keeps finding.
    function hypothesis(spec)
        if type(spec) != "record" then
            error "reasoning.hypothesis expects a record"
        end if
        for each field in ["name", "predicts", "discriminator"]
            if is_unknown(spec[field]) then
                if field = "discriminator" then
                    error ("reasoning.hypothesis needs a discriminator: the"
                           + " observation that would tell this apart from its"
                           + " rivals. A hypothesis nobody can imagine a test"
                           + " for is a story, and a story scores against a"
                           + " pattern just as well as an explanation does")
                end if
                error "reasoning.hypothesis needs a " + field
            end if
        next
        if type(spec["predicts"]) != "record" then
            error ("reasoning.hypothesis: `predicts` must be a record of"
                   + " dimension -> value constraints")
        end if
        return { name: spec["name"], predicts: spec["predicts"],
                 discriminator: spec["discriminator"],
                 rationale: spec["rationale"], explains: false }
    end function

    ' --- Action and Outcome --------------------------------------------------

    function action(spec)
        if type(spec) != "record" then
            error "reasoning.action expects a record"
        end if
        for each field in ["decision", "rehearsal", "authority", "result",
                           "provenance"]
            if is_unknown(spec[field]) then
                error "reasoning.action needs a " + field
            end if
        next
        out = { }
        for each field in keys(spec)
            out[field] = spec[field]
        next
        return out
    end function

    function outcome(spec)
        if type(spec) != "record" then
            error "reasoning.outcome expects a record"
        end if
        for each field in ["expected", "observed", "measured_at"]
            if is_unknown(spec[field]) then
                error ("reasoning.outcome needs a " + field
                       + " -- an outcome without a measurement is a memory")
            end if
        next
        out = { expected: spec["expected"], observed: spec["observed"],
                measured_at: spec["measured_at"],
                met: spec["met"], controlled: false, effect: unknown }
        ' R10. A comparison turns an observation into a measurement OF AN
        ' EFFECT. Without one, `observed` is what happened next, which is not
        ' the same thing and in the one case anybody bothers to measure is
        ' systematically not the same thing (§10 of recipe 7).
        if not is_unknown(spec["holdout"]) then
            out["holdout"] = spec["holdout"]
            out["controlled"] = true
            out["effect"] = spec["observed"] - spec["holdout"]
        end if
        return out
    end function

    ' R7. "We did this before and it worked" enters the system as a FACT when
    ' it is a memory. An action may be cited only once its outcome has actually
    ' been measured -- which is also the only thing that makes §11's learning
    ' loop worth anything.
    function as_evidence(act)
        if type(act) != "record" or is_unknown(act["decision"]) then
            error "reasoning.as_evidence expects an Action"
        end if
        o = act["outcome"]
        if is_unknown(o) then
            error ("reasoning.as_evidence: this action has no measured outcome,"
                   + " so it is not evidence -- it is a memory. Measure it with"
                   + " automation.observe first (design R7)")
        end if
        ' R10. THE ONE THAT COSTS SOMETHING TO OBEY. An outcome with no
        ' comparison is evidence that something happened next, and an action is
        ' taken precisely when a measure is EXTREME, so what happened next is
        ' mostly the extreme reverting. Measured in recipe 7: an intervention
        ' with a true effect of exactly zero showed a 52% "recovery", which is
        ' the number a learning loop would have stored.
        if o["controlled"] != true then
            error ("reasoning.as_evidence: this outcome has no comparison, so it"
                   + " is not evidence that the action WORKED -- only that"
                   + " something happened next. An action is taken when a"
                   + " measure is extreme, and extremes revert on their own:"
                   + " measured at 52% apparent recovery for an intervention"
                   + " that did nothing at all. Supply a holdout, or read it"
                   + " with reasoning.as_observation and do not call it"
                   + " evidence (design R10)")
        end if
        return { kind: "prior_action", decision: act["decision"]["recommendation"],
                 expected: o["expected"], observed: o["observed"],
                 holdout: o["holdout"], effect: o["effect"], met: o["met"],
                 controlled: true }
    end function

    ' What happened next, honestly labelled. Not evidence of an effect, and it
    ' says so in the value rather than in a comment somebody may not read.
    function as_observation(act)
        if type(act) != "record" or is_unknown(act["decision"]) then
            error "reasoning.as_observation expects an Action"
        end if
        o = act["outcome"]
        if is_unknown(o) then
            error ("reasoning.as_observation: this action has no measured"
                   + " outcome (design R7)")
        end if
        return { kind: "prior_observation", uncontrolled: true,
                 decision: act["decision"]["recommendation"],
                 expected: o["expected"], observed: o["observed"], met: o["met"],
                 caveat: ("no comparison, so this is what happened next and not"
                          + " the effect of acting") }
    end function

    ' --- provenance ----------------------------------------------------------

    ' §9. Deliberately CLOCK-FREE: a caller who wants the run stamped passes
    ' `as_of`. A library that read the clock here would make every finding
    ' irreproducible and every golden a moving target, and provenance whose
    ' point is answering "how did we get this" must itself be replayable.
    function provenance(spec)
        if type(spec) != "record" then
            error "reasoning.provenance expects a record"
        end if
        for each field in ["method", "rows", "parameters"]
            if is_unknown(spec[field]) then
                error "reasoning.provenance needs a " + field
            end if
        next
        out = { method: spec["method"], rows: spec["rows"],
                parameters: spec["parameters"],
                assumptions: spec["assumptions"] }
        if is_unknown(out["assumptions"]) then
            out["assumptions"] = []
        end if
        if not is_unknown(spec["as_of"]) then
            out["as_of"] = spec["as_of"]
        end if
        return out
    end function

    ' Does this finding answer the questions §9 requires of it? Structural, so
    ' a caller can assert completeness without reading prose.
    function provenance_complete(f)
        missing = []
        p = f["provenance"]
        if is_unknown(p) then
            return ["provenance"]
        end if
        for each field in ["method", "rows", "parameters"]
            if is_unknown(p[field]) then
                append(missing, "provenance." + field)
            end if
        next
        for each field in ["search", "null", "observation", "measure"]
            if is_unknown(f[field]) then
                append(missing, field)
            end if
        next
        return missing
    end function

end library
