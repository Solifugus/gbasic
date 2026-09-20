' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' ari_discover §11 / Phase 4 -- THE OPTIONAL LLM ADVISOR.
'
' A SEPARATE LIBRARY FROM `ari_discover`, and that is a decision rather than
' filing. `llm` needs libcurl, so folding this in would make every program that
' discovers a report specification carry an HTTP client it never calls --
' the argument `tools` already made for refusing to load `mcp`. `ari_discover`
' stays dependency-free and a caller who wants advice loads this.
'
' ===========================================================================
' THE SPLIT THIS LIBRARY IS BUILT ON, and it corrects §11.
' ===========================================================================
'
' §11 says: "Every LLM suggestion must be translated into a deterministic
' candidate, executed by ARI, and scored."
'
' THAT IS TRUE OF A RULE AND FALSE OF A NAME. A rule can be run: coverage,
' collisions and unclaimed text judge it, and a bad one loses. A NAME cannot.
' `field_3` and `teller_id` parse the same corpus to the same rows with the same
' coverage and the same collisions -- running ARI cannot prefer either, because
' nothing about the parse depends on what the field is called.
'
' So a uniform "every suggestion is validated" promise has a hole exactly where
' the advisor is most useful, since naming is the main thing Phases 0-3 cannot
' do (they derive a name from a heading when there is one, and have nothing to
' say when there is not). This library therefore handles the two classes
' DIFFERENTLY and says so:
'
'   RULE ADVICE  is executed and scored. A candidate that does not beat the
'                deterministic proposal is DISCARDED, not reported as a
'                maybe -- §11's rule, kept.
'
'   NAME ADVICE  is never adopted. It arrives as `advice` beside the field,
'                carrying who proposed it and on what evidence, and a human
'                accepts it. The deterministic name stays until someone does.
'
' The alternative -- adopting a name because a model was confident about it --
' is the failure this whole project keeps finding: a plausible answer that no
' check can refuse. A name is the one part of a specification a person reads,
' so a wrong one is also the part most likely to be believed.
'
' ===========================================================================
' PRIVACY (§11.2)
' ===========================================================================
'
' The advisor never sees the report. It sees an EVIDENCE PACKAGE built here:
'
'   off       this library is not called at all
'   local     a caller-supplied adapter; the package is unredacted but does not
'             leave the machine, which is the caller's assertion and not ours
'   redacted  structural signatures and MASKED examples (digits -> 9, letters
'             -> X), the default for anything external
'   full      real example values may travel, and the caller must say so
'
' `redacted` is the default for a reason a bank will recognise: a teller journal
' names people and accounts, and the difference between "9,999.99" and a real
' balance is the difference between a structural hint and a disclosure.
library ari_advisor

load llm
load ari
load ari_discover

' The modes, in one place, so a typo in a caller's options is refused against a
' list rather than silently read as `off`.
function modes()
    return [ "off", "local", "redacted", "full" ]
end function

function default_options()
    return { mode: "redacted",
             prompt_version: "ari-advisor-1",
             max_fields: 24,
             max_examples: 3 }
end function

function _options(given)
    o = default_options()
    if is_nothing(given) then
        return o
    end if
    if not (type(given) = "record") then
        error "ari_advisor: options must be a record"
    end if
    known = keys(o)
    for each k in keys(given)
        if not contains(known, k) then
            error ("ari_advisor: unknown option '" + k + "' (known: "
                   + join(known, ", ") + ")")
        end if
        o[k] = given[k]
    end for
    if not contains(modes(), o.mode) then
        error ("ari_advisor: unknown mode '" + string(o.mode) + "' (known: "
               + join(modes(), ", ") + ")")
    end if
    return o
end function

' MASKING IS STRUCTURAL, NOT REMOVAL. A field's shape is the whole evidence --
' "9,999.99" says fixed decimal with a thousands separator where "[redacted]"
' says nothing and would make the advisor's job impossible rather than private.
function mask(text)
    out = ""
    i = 0
    while i < len(text)
        c = mid(text, i, 1)
        if contains("0123456789", c) then
            out = out + "9"
        else
            if contains("abcdefghijklmnopqrstuvwxyz", c) then
                out = out + "x"
            else
                if contains("ABCDEFGHIJKLMNOPQRSTUVWXYZ", c) then
                    out = out + "X"
                else
                    out = out + c
                end if
            end if
        end if
        i = i + 1
    end while
    return out
end function

' The bounded package §11.1 asks for. Built from the PROPOSAL, never from the
' report: what travels is what the deterministic engine already concluded.
function evidence(proposal, options = nothing)
    o = _options(options)
    flds = []
    n = 0
    for each f in proposal.fields
        if n >= o.max_fields then
            break
        end if
        ex = ""
        if has(f, "example") and not is_unknown(f.example) then
            ex = string(f.example)
        end if
        if o.mode = "redacted" then
            ex = mask(ex)
        end if
        append(flds, { derived_name: f.name,
                       type: string(f.type),
                       anchored: not f.positional,
                       example: ex })
        n = n + 1
    end for

    heads = []
    tots = []
    if has(proposal, "labels") then
        if has(proposal.labels, "headings") then
            heads = proposal.labels.headings
        end if
        if has(proposal.labels, "totals") then
            tots = proposal.labels.totals
        end if
    end if
    if o.mode = "redacted" then
        mh = []
        for each h in heads
            append(mh, h)
        end for
        heads = mh
    end if

    ' THE FAMILY IS SENT AS ITS SIGNATURE AND SIZE, not whole. The family record
    ' carries the LINE NUMBERS every member was found on and indices into the
    ' source; none of that helps name a column, and shipping it would make the
    ' package grow with the report rather than with its structure -- the
    ' opposite of the minimization §11.1 asks for. Measured on this corpus: the
    ' whole record is a 45-line list, the signature is one string.
    sig = ""
    cnt = 0
    if has(proposal.family, "signature") then
        sig = string(proposal.family.signature)
    end if
    if has(proposal.family, "count") then
        cnt = proposal.family.count
    end if

    return { mode: o.mode,
             prompt_version: o.prompt_version,
             row_signature: sig,
             row_count: cnt,
             headings: heads,
             totals: tots,
             fields: flds }
end function

' ---------------------------------------------------------------------------
' NAME ADVICE -- proposed, never adopted.
' ---------------------------------------------------------------------------

function _name_system()
    return ("You are naming columns in a banking report whose structure has "
            + "already been determined by a deterministic parser. You are given "
            + "each column's derived name, its type, whether it was located by "
            + "an anchor, and a MASKED example where digits are 9 and letters "
            + "are X or x. Propose a better snake_case name for a column ONLY "
            + "where you are confident the derived name is wrong or "
            + "uninformative. It is correct and expected to propose nothing. "
            + "Never invent a meaning the evidence does not support. Reply with "
            + "JSON only: {\"names\":[{\"derived_name\":\"...\",\"proposed\":"
            + "\"...\",\"why\":\"...\"}]}")
end function

' `advise_names(proposal, model, options)` -> the proposal with `advice` added
' to any field the advisor spoke about, plus `advisor` provenance.
'
' THE PROPOSAL'S OWN NAMES ARE NOT TOUCHED. `f.name` is what every downstream
' consumer reads and what `ari.parse` will emit; `f.advice` is a suggestion
' sitting beside it. A caller that wants to adopt one does so explicitly, which
' is a decision with a person behind it.
function advise_names(proposal, model, options = nothing)
    o = _options(options)
    if o.mode = "off" then
        error "ari_advisor.advise_names: mode is off; do not call the advisor"
    end if

    ' A REFUSED PROPOSAL GETS NO ADVICE, and this is structural rather than
    ' incidental. Over the null corpus the deterministic engine answers
    ' ok=false with no fields, so today an advisor would be handed nothing and
    ' would say nothing -- but that is an accident of there being no fields to
    ' name, not a rule, and it would stop being true the moment the engine
    ' reported a low-confidence proposal instead of refusing.
    '
    ' The rule is the one `decision.evaluate` already enforces one library over:
    ' you may not build on a quantity the analysis DECLINED TO ESTABLISH. A
    ' model asked to name the columns of a specification that was refused will
    ' name them -- fluently, because that is what it does -- and the names would
    ' be the most convincing part of a result the engine had already rejected.
    if has(proposal, "ok") and not proposal.ok then
        error ("ari_advisor.advise_names: this proposal was refused "
               + "(ok=false); there is nothing established to name")
    end if

    pkg = evidence(proposal, o)
    reply = llm.ask_json(model, _name_system(), encode(pkg))

    prov = { prompt_version: o.prompt_version,
             mode: o.mode,
             fields_sent: count(pkg.fields),
             answered: false,
             suggestions: 0 }

    out = proposal
    if is_unknown(reply) then
        ' §13: an unusable reply is an OUTCOME, not a failure. The deterministic
        ' proposal is already complete and is returned unchanged.
        prov.why = "the advisor did not return usable JSON"
        out.advisor = prov
        return out
    end if
    if not has(reply, "names") then
        prov.why = "the advisor's reply carried no `names`"
        out.advisor = prov
        return out
    end if

    prov.answered = true
    fields = out.fields
    for each s in reply.names
        if not has(s, "derived_name") or not has(s, "proposed") then
            continue
        end if
        i = 0
        while i < count(fields)
            if fields[i].name = s.derived_name then
                why = ""
                if has(s, "why") then
                    why = string(s.why)
                end if
                f = fields[i]
                f.advice = { proposed: string(s.proposed),
                             why: why,
                             adopted: false,
                             source: "llm" }
                fields[i] = f
                prov.suggestions = prov.suggestions + 1
            end if
            i = i + 1
        end while
    end for
    out.fields = fields
    out.advisor = prov
    return out
end function

' Adopt one piece of name advice, BY NAME and one at a time. There is no
' `adopt_all`, deliberately: accepting every suggestion in one call is the
' silent adoption this library exists to prevent, wearing a person's clothes.
function adopt(proposal, derived_name)
    out = proposal
    fields = out.fields
    found = false
    i = 0
    while i < count(fields)
        if fields[i].name = derived_name then
            if not has(fields[i], "advice") then
                error ("ari_advisor.adopt: '" + derived_name
                       + "' carries no advice to adopt")
            end if
            f = fields[i]
            a = f.advice
            a.adopted = true
            f.advice = a
            f.name_was = f.name
            f.name = a.proposed
            fields[i] = f
            found = true
        end if
        i = i + 1
    end while
    if not found then
        error ("ari_advisor.adopt: no field named '" + derived_name + "'")
    end if
    out.fields = fields
    return out
end function

end library
