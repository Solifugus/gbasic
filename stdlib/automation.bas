' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' automation.bas — orchestrates and acts (docs/automation_reasoning_design.md
' §5). THE ONLY LAYER AUTHORISED TO CHANGE EXTERNAL STATE.
'
' Third increment, from Recipe 6. It exists to enforce the two refusals nothing
' had ever tested, because until something could act there was nothing to
' enforce them against:
'
'   R5  a process that has never been replayed against history may not act;
'   R6  authority is ENFORCED HERE, never in the decision. `decision` states
'       what a recommendation would need; this is where it is spent.
'
' THE DRY RUN AND THE LIVE PATH SHARE ONE GATE. `would` and `execute` both call
' `_gate` and neither has a copy of the rules. If they did not, a rehearsal
' would be a statement about a DIFFERENT PROGRAM than the one that runs -- and
' the whole argument for R5 is that the rehearsal tells you what will happen.
library automation

    load reasoning from "reasoning.bas"

    ' --- the gate ------------------------------------------------------------

    ' Returns { allowed, reason, needs }. Never acts, never raises for a
    ' policy refusal: whether a thing may happen is an ANSWER, and the caller
    ' handles it. Malformed input still raises.
    function _gate(dec, context, rehearsal)
        if type(dec) != "record" or is_unknown(dec["recommendation"]) then
            error "automation: expected a Decision"
        end if
        auth = context["authority"]
        if is_unknown(auth) then
            error ("automation: the context needs an authority -- unset means"
                   + " NOTHING may execute, and leaving it out must not quietly"
                   + " mean the opposite")
        end if

        ' R5, and it is deliberately checked BEFORE authority: an unrehearsed
        ' process should not be able to act even with a human's approval,
        ' because the human has nothing to approve ON. Nobody knows how often
        ' it fires or how often it is wrong.
        need = auth["min_rehearsal_periods"]
        if is_unknown(need) then
            error ("automation: authority must declare min_rehearsal_periods"
                   + " -- how much history this process must be replayed"
                   + " against before it may act (design R5)")
        end if
        if is_unknown(rehearsal) then
            return { allowed: false, needs: "rehearsal",
                     reason: ("this process has never been replayed against"
                              + " history, so nothing is known about how often"
                              + " it fires or how often it is wrong (design R5)") }
        end if
        if rehearsal["periods"] < need then
            return { allowed: false, needs: "rehearsal",
                     reason: ("rehearsed over " + string(rehearsal["periods"])
                              + " periods, which is short of the "
                              + string(need) + " this authority requires") }
        end if

        ' R6, the enforcement half. `decision` STATED this; here it is spent.
        if dec["authority_required"] = true then
            ap = context["approval"]
            if is_unknown(ap) or is_unknown(ap["by"]) then
                return { allowed: false, needs: "approval",
                         reason: ("this recommendation exceeds the delegated"
                                  + " authority and no approval is on file: "
                                  + string(dec["authority_reason"])) }
            end if
            return { allowed: true, needs: "approval",
                     reason: "approved by " + string(ap["by"]) }
        end if
        return { allowed: true, needs: "none",
                 reason: "within delegated authority" }
    end function

    ' --- dry run -------------------------------------------------------------

    ' What WOULD happen. Takes no executor, so it cannot act by construction --
    ' the safety property is structural rather than a flag somebody must
    ' remember to pass. This is how a rehearsal is built honestly.
    function would(dec, context, rehearsal)
        g = _gate(dec, context, rehearsal)
        return { would_act: g.allowed, needs: g.needs, reason: g.reason,
                 recommendation: dec["recommendation"] }
    end function

    ' --- rehearsal -----------------------------------------------------------

    ' The record of a replay. The caller runs the replay, because only the
    ' caller knows its own history; this validates and carries it.
    function rehearsal(spec)
        if type(spec) != "record" then
            error "automation.rehearsal expects a record"
        end if
        for each field in ["periods", "fired"]
            if is_unknown(spec[field]) then
                error "automation.rehearsal needs a " + field
            end if
        next
        periods = spec["periods"]
        fired = spec["fired"]
        if periods < 1 then
            error "automation.rehearsal: periods must be at least 1"
        end if
        if fired > periods then
            error ("automation.rehearsal: fired on " + string(fired)
                   + " of " + string(periods) + " periods -- a process cannot"
                   + " fire more often than it ran")
        end if
        false_alarms = spec["false_alarms"]
        rate = unknown
        if not is_unknown(false_alarms) then
            if false_alarms > fired then
                error ("automation.rehearsal: " + string(false_alarms)
                       + " false alarms out of " + string(fired) + " firings")
            end if
            rate = false_alarms / periods
        end if
        return { periods: periods, fired: fired,
                 false_alarms: false_alarms, missed: spec["missed"],
                 needed_approval: spec["needed_approval"],
                 false_alarm_rate: rate }
    end function

    ' --- execute -------------------------------------------------------------

    ' The only function here that can change anything, and it changes nothing
    ' itself: `executor` is a function value the caller supplies, called with
    ' the decision, and it is called ONLY past the gate.
    function execute(dec, context, rehearsal_v, executor)
        g = _gate(dec, context, rehearsal_v)
        if not g.allowed then
            error ("automation.execute refused: " + g.reason)
        end if
        result = executor(dec)
        ap = context["approval"]
        if is_unknown(ap) then
            ap = { by: "delegated authority", at: unknown }
        end if
        return reasoning.action({
            decision: dec,
            ' §9's "policy permitting it". The regime in force when this ran --
            ' objectives, thresholds, authority -- without which an action
            ' cannot be re-judged later against the rules that have replaced
            ' it, and a stored action cannot say why it was allowed.
            context: context,
            rehearsal: rehearsal_v,
            authority: { needed: dec["authority_required"], granted_by: ap["by"],
                         reason: g.reason },
            result: result,
            provenance: reasoning.provenance({
                method: "execute/gated",
                rows: 1,
                parameters: { recommendation: dec["recommendation"],
                              expected_value: dec["expected_value"],
                              rehearsed_periods: rehearsal_v["periods"] },
                assumptions: ["the executor reports its own success honestly"] }) })
    end function

    ' --- holding out ---------------------------------------------------------

    ' THE CONCEPT THE ARCHITECTURE LACKED (recipe 7). A system that always acts
    ' when it should can never learn whether acting helps, because it has
    ' nothing to compare against -- and what happened next, uncontrolled, is
    ' mostly the extreme reverting on its own.
    '
    ' DETERMINISTIC IN THE KEY, not random, for two reasons that are both about
    ' R5: a replay must make the same assignments as the live run or the
    ' rehearsal describes a different program, and an operator asking "why was
    ' this one held back" must get an answer.
    function assign(key, holdout_rate)
        if type(holdout_rate) != "number" or holdout_rate < 0 or holdout_rate >= 1 then
            error ("automation.assign: holdout_rate must be at least 0 and below"
                   + " 1 -- holding everything back means never acting")
        end if
        if holdout_rate = 0 then
            return { arm: "treat", key: key,
                     why: "no holdout declared; nothing can be learned about"
                          + " whether acting helps" }
        end if
        h = _hash(string(key))
        if mod(h, 1000) / 1000 < holdout_rate then
            return { arm: "holdout", key: key,
                     why: "held back deliberately, so the effect of acting is"
                          + " measurable" }
        end if
        return { arm: "treat", key: key, why: "assigned to act" }
    end function

    ' Stable string hash, deterministic across runs and machines -- this decides
    ' an experiment's arms, not a security property.
    '
    ' IT NEEDS A MIXING STEP AND THE FIRST VERSION DID NOT HAVE ONE. A plain
    ' `h = h * 31 + byte` mod a large prime leaves consecutive short keys
    ' consecutive, so `mod(h, 1000)` walked in lockstep and a run of keys landed
    ' entirely on one side of the threshold: measured 0 holdouts out of 400 for
    ' keys "c1".."c400", while longer varied keys looked perfectly fine. That is
    ' the shape of hash bug that hides -- the first distribution check used
    ' "cell-N/x" and reported a clean 0.200.
    '
    ' Values are kept small enough that every intermediate stays well inside
    ' exact double integers; gBASIC numbers are doubles and a 32-bit-style
    ' multiply would silently lose the low bits that carry the entropy.
    function _hash(s)
        h = 5381
        for i = 0 to len(s) - 1
            h = mod(h * 33 + byte_at(s, i), 1000003)
        next
        h = bxor(h, shl(h, 7))
        h = bxor(h, shr(h, 11))
        return mod(h * 2654435 + 12345, 1000003)
    end function

    ' --- outcome -------------------------------------------------------------

    ' §11 and R7. Returns the action WITH its outcome, because a gBASIC record
    ' is a value and mutating the caller's copy would silently do nothing.
    function observe(act, measured)
        if type(act) != "record" or is_unknown(act["decision"]) then
            error "automation.observe expects an Action"
        end if
        if type(measured) != "record" then
            error "automation.observe expects a measurement record"
        end if
        expected = act["decision"]["expected_value"]
        observed = measured["value"]
        if is_unknown(observed) then
            error ("automation.observe: the measurement needs a `value` -- an"
                   + " outcome without a measurement is a memory (design R7)")
        end if
        out = { }
        for each field in keys(act)
            out[field] = act[field]
        next
        out["outcome"] = reasoning.outcome({
            expected: expected, observed: observed,
            measured_at: measured["at"],
            holdout: measured["holdout"],
            met: observed >= expected })
        return out
    end function

end library
