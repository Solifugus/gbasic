' mdna.bas — MD&A / risk-factor extraction from 10-K/10-Q HTML
' (edgar_design.md §5.2, WP-MDA-1). Extraction rides the xml module's lenient
' HTML parser (xml.parse_html) + xml.text (xml_design.md §6): strip the filing to
' plain text, then locate the Item sections by their headings.
'
' CONTRACT: best effort, `unknown` per section when sectioning fails. The panel
' (WP-MDA-3) can be handed the whole stripped document via mdna.text() when a
' section can't be isolated, since LLMs tolerate noise that parsers don't.
'
' HEADING HEURISTIC. A 10-K names the sections we want by Item number:
'   MD&A          = Item 7   (ends at Item 7A or Item 8)
'   Risk Factors  = Item 1A  (ends at Item 1B or Item 2)
' The catch is that "Item 7." also appears in the table of contents and in
' in-body cross-references ("...as described in Part II - Item 7. ..."), so a
' naive first/last/largest-span match is wrong. The reliable signal is CASE: the
' actual section headers are printed in UPPERCASE ("ITEM 7.", "ITEM 1A."), while
' the TOC and cross-references use title case ("Item 7."). We therefore match the
' uppercase form case-sensitively — which is unique per section in real filings —
' and take from that header to the nearest following end-header. A filer that
' does not uppercase its headers yields `unknown` (the honest best-effort result);
' the whole-document fallback covers the panel in that case.
library mdna
    load xml
    load llm from "llm.bas"

    ' Whole-document fallback accessor: the filing stripped to plain text (with
    ' non-breaking spaces normalized to ordinary spaces). This is what the panel
    ' receives when a section can't be isolated.
    function text(html_text)
        tree = xml.parse_html(html_text)
        return replace(xml.text(tree), chr(160), " ")
    end function

    ' find `needle` in `s` at or after `from`, or `nothing`.
    function _find_from(s, needle, from)
        if from >= len(s) then
            return nothing
        end if
        sub = mid(s, from, len(s) - from)
        r = find(sub, needle)
        if r = nothing then
            return nothing
        end if
        return from + r
    end function

    ' Extract the block from the (uppercase) `start_marker` header to the nearest
    ' following end marker, or `unknown` if the start header is absent. If no end
    ' marker follows, the block runs to end of document (still best-effort useful).
    function _section(full, start_marker, end_markers)
        s = find(full, start_marker)
        if s = nothing then
            return unknown
        end if
        after = s + len(start_marker)
        best = len(full)
        i = 0
        while i < count(end_markers)
            e = _find_from(full, end_markers[i], after)
            if e != nothing and e < best then
                best = e
            end if
            i = i + 1
        end while
        return trim(mid(full, s, best - s))
    end function

    ' Public: HTML -> { mdna, risk_factors }. Each field is the section text, or
    ' `unknown` when that section's header can't be located.
    function sections(html_text)
        full = text(html_text)
        out = {}
        out["mdna"] = _section(full, "ITEM 7.", ["ITEM 7A.", "ITEM 8."])
        out["risk_factors"] = _section(full, "ITEM 1A.", ["ITEM 1B.", "ITEM 2."])
        return out
    end function

    ' ======================================================================
    ' Deterministic pre-pass (edgar_design.md §5.2) — cheap, before any LLM.
    ' ======================================================================

    ' --- risk-factor year-over-year diff -------------------------------------
    '
    ' GRANULARITY CHOICE: SENTENCE-level. Item-level (one risk-factor caption per
    ' unit) would be ideal but 10-K risk-factor captions are a formatting
    ' convention that does not survive HTML-to-text stripping reliably, so there is
    ' no robust delimiter to split on. Sentences split deterministically on ". "
    ' and are the honest, reproducible unit here. The signal the design cares about
    ' — a previously disclosed risk sentence disappearing (removed) or a new one
    ' appearing (added) — is captured at this granularity.

    ' Collapse all whitespace runs to single spaces and trim. (for-each iteration:
    ' indexing arr[i] in a while-loop deep-copies the whole array each step in
    ' gBASIC, which is O(n^2) — for-each iterates in O(n). Same reason throughout.)
    function _norm_ws(s)
        out = ""
        for each p in split(s, " ")
            if p != "" then
                if out != "" then
                    out = out + " "
                end if
                out = out + p
            end if
        end for
        return out
    end function

    ' Split section text into normalized sentences (drop sub-25-char fragments and
    ' de-duplicate). Newlines/tabs flatten to spaces first; a trailing period is
    ' trimmed so the last sentence matches its mid-text twin.
    function _sentences(text)
        flat = replace(replace(text, chr(10), " "), chr(9), " ")
        out = []
        for each rawpart in split(flat, ". ")
            s = _norm_ws(rawpart)
            if len(s) > 0 and mid(s, len(s) - 1, 1) = "." then
                s = trim(mid(s, 0, len(s) - 1))
            end if
            if len(s) >= 25 then
                append(out, s)
            end if
        end for
        return unique(out)
    end function

    ' Diff two risk-factor section texts. Returns { added, removed, added_count,
    ' removed_count, common_count, prior_count, current_count }; the *_count fields
    ' and the lists are `unknown` when either section is missing (NA policy).
    function risk_diff(prior_risk, current_risk)
        if not is_string(prior_risk) or not is_string(current_risk) then
            return { added: unknown, removed: unknown, added_count: unknown, removed_count: unknown, common_count: unknown, prior_count: unknown, current_count: unknown }
        end if
        ps = _sentences(prior_risk)
        cs = _sentences(current_risk)
        added = []
        removed = []
        common = 0
        for each c in cs
            if contains(ps, c) then
                common = common + 1
            else
                append(added, c)
            end if
        end for
        for each p in ps
            if not contains(cs, p) then
                append(removed, p)
            end if
        end for
        return { added: added, removed: removed, added_count: count(added), removed_count: count(removed), common_count: common, prior_count: count(ps), current_count: count(cs) }
    end function

    ' --- hedge-language density ----------------------------------------------

    ' Editable hedge/uncertainty lexicon (lowercase, whole-word). Adjust freely;
    ' hedge_stats matches tokens against this list.
    function hedge_lexicon()
        return [
            "may", "might", "could", "would", "should", "can", "cannot",
            "possibly", "potentially", "approximately", "generally", "substantially",
            "believe", "believes", "believed", "expect", "expects", "expected",
            "anticipate", "anticipates", "anticipated", "estimate", "estimates", "estimated",
            "intend", "intends", "intended", "project", "projects", "projected",
            "assume", "assumes", "assumed", "likely", "unlikely", "uncertain", "uncertainty",
            "tend", "tends", "seek", "seeks", "plan", "plans", "perhaps", "presumably",
            "roughly", "somewhat", "appear", "appears", "seem", "seems", "predict", "predicts"
        ]
    end function

    ' Punctuation/separator characters mapped to spaces before tokenizing.
    function _separators()
        return [
            chr(10), chr(9), chr(13), chr(160), ".", ",", ";", ":", "(", ")",
            "\"", "'", "!", "?", "/", "%", "$", "&", "-", "_", "[", "]", "{", "}",
            "*", "|", "<", ">", "=", "+", "@", "#", chr(226)
        ]
    end function

    ' Tokenize `text` into lowercase words. Cleaning is done with a fixed set of
    ' whole-string `replace` passes (each O(len)) rather than per-token character
    ' work — per-token user-function calls are the expensive path in a tree-walking
    ' interpreter (a 10k-word MD&A took seconds that way). chr(226) is the lead byte
    ' of the UTF-8 curly punctuation the SEC HTML uses (’ “ ” —), so splitting on it
    ' drops those too. Callers filter out the empty tokens.
    function _tokenize(text)
        s = lower(text)
        for each sep in _separators()
            s = replace(s, sep, " ")
        end for
        return split(s, " ")
    end function

    ' Hedge density of a text: { hedge, total, rate }. rate is hedge/total, or
    ' `unknown` for empty / non-string input.
    function hedge_stats(text)
        if not is_string(text) then
            return { hedge: unknown, total: unknown, rate: unknown }
        end if
        toks = _tokenize(text)
        lex = hedge_lexicon()
        total = 0
        h = 0
        for each w in toks
            if w != "" then
                total = total + 1
                if contains(lex, w) then
                    h = h + 1
                end if
            end if
        end for
        r = unknown
        if total > 0 then
            r = h / total
        end if
        return { hedge: h, total: total, rate: r }
    end function

    ' Convenience: just the rate (design's "reported as a rate").
    function hedge_rate(text)
        return hedge_stats(text).rate
    end function

    ' --- evidence-record assembly --------------------------------------------

    ' Bundle the deterministic pre-pass into one evidence record for the panel
    ' (WP-MDA-3): the risk-factor diff, the MD&A hedge density for both years and
    ' its shift, and the SUPPLIED §4.5 forensics scorecard (passed in, embedded
    ' opaquely — mdna does not depend on forensics.bas). `prior`/`current` are
    ' sections() records; `scorecard` is whatever forensics.flags produced.
    function evidence(prior, current, scorecard)
        rd = risk_diff(prior["risk_factors"], current["risk_factors"])
        hp = hedge_stats(prior["mdna"])
        hc = hedge_stats(current["mdna"])
        shift = unknown
        if not is_unknown(hp.rate) and not is_unknown(hc.rate) then
            shift = hc.rate - hp.rate
        end if
        out = {}
        out["risk_added"] = rd.added
        out["risk_removed"] = rd.removed
        out["risk_added_count"] = rd.added_count
        out["risk_removed_count"] = rd.removed_count
        out["hedge_prior"] = hp
        out["hedge_current"] = hc
        out["hedge_shift"] = shift
        out["scorecard"] = scorecard
        return out
    end function

    ' ======================================================================
    ' The panel — "make them fight," formalized (edgar_design.md §5.2, §9.6).
    ' N adversarial analysts + a referee, decoded via llm.ask_json so
    ' disagreement is measurable. Economics (llm_design.md §5): local models in
    ' the analyst seats, a frontier model in the referee chair — a configuration
    ' choice (each panelist carries its own model handle).
    ' ======================================================================

    ' --- default stance system-prompts (overridable per panelist) ------------
    ' A panelist is { name, model, stance }; `stance` is the system prompt and can
    ' be any string — these are just the library defaults. The verdict schema is
    ' requested by the panel in the USER prompt, so overriding a stance only
    ' changes the persona, not the output contract.

    function stance_bull()
        return "You are a bullish equity analyst. Read the filing for genuine strengths and give management a fair hearing, but do not excuse clear evasions. Judge how candidly management discusses its business."
    end function

    function stance_bear()
        return "You are a skeptical short-seller. Hunt for what management is downplaying, omitting, or spinning. Judge how candidly management discusses the risks and weaknesses in its business."
    end function

    function stance_forensic()
        return "You are a forensic accountant. Focus on whether the narrative and the numbers agree, on hedging language, and on risks that appeared or vanished. Judge how candidly management discusses its business."
    end function

    function stance_referee()
        return "You are the chief investment officer refereeing a panel of analysts. You see only their verdicts and the deterministic evidence. Adjudicate the disagreement, weigh candor against the forensic scorecard, and issue a single decision. Do not invent facts the analysts did not raise."
    end function

    ' --- prompt assembly (evidence rendered as text the model can cite) -------

    function _ev_line(label, v)
        return label + ": " + string(v) + chr(10)
    end function

    function _analyst_prompt(sections, evidence)
        md = sections["mdna"]
        if not is_string(md) then
            md = ""
        end if
        snippet = md
        if len(md) > 6000 then
            snippet = left(md, 6000)
        end if
        p = "Analyze this 10-K MD&A excerpt together with the deterministic evidence, then return your verdict." + chr(10) + chr(10)
        p = p + "=== MD&A EXCERPT ===" + chr(10) + snippet + chr(10) + chr(10)
        p = p + "=== DETERMINISTIC EVIDENCE ===" + chr(10)
        p = p + _ev_line("risk_added_count", evidence["risk_added_count"])
        p = p + _ev_line("risk_removed_count", evidence["risk_removed_count"])
        p = p + _ev_line("hedge_shift", evidence["hedge_shift"])
        p = p + _ev_line("scorecard", evidence["scorecard"])
        p = p + chr(10) + "Respond with ONLY a JSON object of the form: {\"candor\": <integer 0-100>, \"stance_read\": <one-sentence string>, \"evasions\": [<strings>], \"citations\": [<evidence keys or MD&A quotes>]}"
        return p
    end function

    ' --- the panel: one llm.ask_json per analyst -> verdict frame -------------
    '
    ' Returns a frame (one row per analyst) with columns: name, candor,
    ' stance_read, evasions, citations, ok. A panelist whose model returns
    ' unmarshalable output (llm.ask_json -> unknown) becomes an `ok=false` row of
    ' `unknown`s and the panel CONTINUES (edgar_design.md §9.6: malformed output is
    ' ask_json's problem, absent fields are unknown). Verdict fields are read
    ' dynamically (bracket) so a valid-but-incomplete verdict degrades per field.
    function panel(panelists, sections, evidence)
        prompt = _analyst_prompt(sections, evidence)
        names = []
        candor = []
        reads = []
        evasions = []
        citations = []
        ok = []
        for each pan in panelists
            v = llm.ask_json(pan.model, pan.stance, prompt)
            append(names, pan.name)
            if is_unknown(v) then
                append(candor, unknown)
                append(reads, unknown)
                append(evasions, unknown)
                append(citations, unknown)
                append(ok, false)
            else
                append(candor, v["candor"])
                append(reads, v["stance_read"])
                append(evasions, v["evasions"])
                append(citations, v["citations"])
                append(ok, true)
            end if
        end for
        out = {}
        out["name"] = names
        out["candor"] = candor
        out["stance_read"] = reads
        out["evasions"] = evasions
        out["citations"] = citations
        out["ok"] = ok
        return out
    end function

    ' --- disagreement measure: population variance of the candor column ------
    ' Uncontroversial filings converge (low variance); evasive ones split the
    ' panel. Only numeric candor values count; `unknown` when fewer than two.
    function disagreement(verdicts)
        vals = []
        for each c in verdicts["candor"]
            if is_number(c) then
                append(vals, c)
            end if
        end for
        n = count(vals)
        if n < 2 then
            return unknown
        end if
        s = 0
        for each x in vals
            s = s + x
        end for
        m = s / n
        ss = 0
        for each x in vals
            ss = ss + (x - m) * (x - m)
        end for
        return ss / n
    end function

    ' --- the referee: adjudicate the verdicts ---------------------------------
    ' Sees only the verdicts (rendered as text) + must issue one decision. Returns
    ' { verdict, candor, rationale, dissent, ok }; `ok=false` with `unknown`s when
    ' the frontier model's reply won't parse.
    function _referee_prompt(verdicts)
        p = "The analyst panel returned these verdicts. Adjudicate them into one decision." + chr(10) + chr(10)
        i = 0
        n = count(verdicts["name"])
        while i < n
            p = p + "- " + verdicts["name"][i] + ": candor=" + string(verdicts["candor"][i])
            p = p + ", read=" + string(verdicts["stance_read"][i])
            p = p + ", evasions=" + string(verdicts["evasions"][i]) + chr(10)
            i = i + 1
        end while
        p = p + chr(10) + "Respond with ONLY a JSON object: {\"verdict\": <string>, \"candor\": <integer 0-100>, \"rationale\": <string>, \"dissent\": <string>}"
        return p
    end function

    function referee(frontier_model, verdicts)
        v = llm.ask_json(frontier_model, stance_referee(), _referee_prompt(verdicts))
        if is_unknown(v) then
            return { verdict: unknown, candor: unknown, rationale: unknown, dissent: unknown, ok: false }
        end if
        return { verdict: v["verdict"], candor: v["candor"], rationale: v["rationale"], dissent: v["dissent"], ok: true }
    end function
end library
