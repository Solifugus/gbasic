' WP-MDA-3 — mdna.panel / referee / disagreement, SHAPE/SCHEMA tests only, driven
' by fixture models (llm handles with injected transports returning canned verdict
' JSON — no network). Covers: a 3-analyst panel assembled into a verdict frame, a
' MALFORMED panelist that degrades to an ok=false unknown row while the panel
' continues, the candor-variance disagreement measure, and the referee adjudicating
' the verdicts.
program main(args)
    load mdna from "../stdlib/mdna.bas"
    load llm from "../stdlib/llm.bas"

    m_bull = llm.with_transport(llm.anthropic("bull-model", "k"), bull_reply)
    m_bear = llm.with_transport(llm.anthropic("bear-model", "k"), bear_reply)
    m_forensic = llm.with_transport(llm.anthropic("forensic-model", "k"), forensic_reply)
    m_muddled = llm.with_transport(llm.anthropic("muddled-model", "k"), garbage_reply)
    m_referee = llm.with_transport(llm.anthropic("frontier", "k"), referee_reply)

    panelists = [
        { name: "bull", model: m_bull, stance: mdna.stance_bull() },
        { name: "bear", model: m_bear, stance: mdna.stance_bear() },
        { name: "forensic", model: m_forensic, stance: mdna.stance_forensic() },
        { name: "muddled", model: m_muddled, stance: mdna.stance_forensic() }
    ]

    sections = { mdna: "Management discussion of results and outlook.", risk_factors: "ITEM 1A. Risk Factors ..." }
    evidence = { risk_added_count: 137, risk_removed_count: 119, hedge_shift: 0.0021, scorecard: { m_score: -1.78 } }

    v = mdna.panel(panelists, sections, evidence)

    print("== panel verdict frame ==")
    print("rows=" + string(count(v["name"])))
    i = 0
    while i < count(v["name"])
        line = v["name"][i] + ": ok=" + string(v["ok"][i]) + " candor=" + string(v["candor"][i])
        if v["ok"][i] then
            line = line + " evasions=" + string(count(v["evasions"][i])) + " read=" + v["stance_read"][i]
        end if
        print(line)
        i = i + 1
    end while

    print("== disagreement (candor variance, unknowns skipped) ==")
    print("variance=" + string(mdna.disagreement(v)))

    print("== referee ==")
    final = mdna.referee(m_referee, v)
    print("ok=" + string(final.ok) + " verdict=" + final.verdict + " candor=" + string(final.candor))
    print("rationale=" + final.rationale)
    print("dissent=" + final.dissent)
end program

' --- fixture transports: return an anthropic wire envelope carrying the text ---

function _envelope(verdict_text)
    b = {}
    b.model = "fixture"
    b.content = [ { type: "text", text: verdict_text } ]
    b.usage = { input_tokens: 1, output_tokens: 1 }
    b.stop_reason = "end_turn"
    return { status: 200, headers: {}, body: encode(b) }
end function

function bull_reply(m, req)
    return _envelope("{\"candor\": 80, \"stance_read\": \"Brand momentum looks genuine\", \"evasions\": [], \"citations\": [\"risk_added_count\"]}")
end function

function bear_reply(m, req)
    return _envelope("{\"candor\": 35, \"stance_read\": \"Downplays tariff and margin pressure\", \"evasions\": [\"vague on tariffs\", \"no FCF detail\"], \"citations\": [\"hedge_shift\"]}")
end function

function forensic_reply(m, req)
    return _envelope("{\"candor\": 50, \"stance_read\": \"Numbers and narrative mostly agree\", \"evasions\": [\"hedging rose\"], \"citations\": [\"risk_removed_count\"]}")
end function

' Never valid JSON, even after the corrective retry -> ask_json returns unknown.
function garbage_reply(m, req)
    return _envelope("I decline to answer in JSON. Here is some prose instead.")
end function

function referee_reply(m, req)
    return _envelope("{\"verdict\": \"lean negative\", \"candor\": 45, \"rationale\": \"Forensic flags unaddressed; bear case credible\", \"dissent\": \"candor spread 35 to 80\"}")
end function
