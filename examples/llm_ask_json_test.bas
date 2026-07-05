' WP-LLM-2 — llm.ask_json structured output (§3, §7.1). Four cases, all offline
' via injected transports (no network). The corrective retry is detected
' statelessly: ask_json's second call carries the corrective instruction in its
' body, so a transport can branch on whether req.body contains "not valid JSON".
program main(args)
    load llm from "../stdlib/llm.bas"

    base = llm.anthropic("claude-sonnet-4-6", "sk-test")

    ' 1) clean JSON -> parsed record
    m1 = llm.with_transport(base, clean)
    r1 = llm.ask_json(m1, "sys", "give verdict")
    print("clean:  verdict=" + r1.verdict + " score=" + string(r1.score))

    ' 2) fenced JSON (```json ... ```) -> fences stripped, parsed
    m2 = llm.with_transport(base, fenced)
    r2 = llm.ask_json(m2, "sys", "give verdict")
    print("fenced: verdict=" + r2.verdict + " score=" + string(r2.score))

    ' 3) garbage first, clean on the corrective retry -> parsed
    m3 = llm.with_transport(base, retry_ok)
    r3 = llm.ask_json(m3, "sys", "give verdict")
    print("retry:  ok=" + string(r3.ok) + " (parsed on the corrective retry)")

    ' 4) garbage both times -> unknown
    m4 = llm.with_transport(base, always_bad)
    r4 = llm.ask_json(m4, "sys", "give verdict")
    print("garbage: is_unknown=" + string(is_unknown(r4)))

    ' 5) a list is a valid ask_json result too
    m5 = llm.with_transport(base, listy)
    r5 = llm.ask_json(m5, "sys", "give list")
    print("list:   count=" + string(count(r5)) + " first=" + string(r5[0]))
end program

' --- injected transports (return the anthropic wire envelope with our body) ---

function _reply(text)
    b = {}
    b.model = "claude-sonnet-4-6"
    b.content = [ { type: "text", text: text } ]
    b.usage = { input_tokens: 1, output_tokens: 1 }
    b.stop_reason = "end_turn"
    return { status: 200, headers: {}, body: encode(b) }
end function

function clean(m, req)
    return _reply("{\"verdict\":\"buy\",\"score\":9}")
end function

function fenced(m, req)
    return _reply("```json" + chr(10) + "{\"verdict\":\"sell\",\"score\":2}" + chr(10) + "```")
end function

function listy(m, req)
    return _reply("[10, 20, 30]")
end function

' Garbage unless the request carries the corrective instruction, then clean.
function retry_ok(m, req)
    if find(req.body, "not valid JSON") != nothing then
        return _reply("{\"ok\":true}")
    end if
    return _reply("Sure! here you go: {oops not json")
end function

function always_bad(m, req)
    return _reply("I cannot comply, here is prose instead.")
end function
