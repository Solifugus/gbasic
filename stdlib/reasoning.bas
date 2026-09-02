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
    ' answer downstream, so it is declared and never inferred. `siblings` --
    ' the other cells of the same decomposition -- is right for a stationary
    ' measure and WRONG under seasonality, which is why the list is short and
    ' adding to it is deliberate.
    function nulls()
        return ["siblings"]
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
