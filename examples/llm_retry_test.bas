' WP-LLM-1 — llm.bas retry/backoff (§4), driven entirely offline with an INJECTED
' transport and an INJECTED sleep (no network, no real waiting). The flaky
' transport branches on req.attempt: a 500 (exponential backoff 1s), then a 503
' carrying Retry-After: 5 (honored -> 5s), then a 200 with a valid body. The
' injected sleep just prints each delay, so the backoff sequence is the golden.
program main(args)
    load llm from "../stdlib/llm.bas"

    m = llm.anthropic("claude-sonnet-4-6", "sk-test")
    m = llm.with_transport(m, flaky)
    m = llm.with_sleep(m, tick)

    r = llm.chat(m, "sys", [ {role: "user", content: "go"} ])
    print("result text: " + r.text)
    print("usage in=" + string(r.usage.input) + " out=" + string(r.usage.output))
end program

' Attempt-indexed transport (stateless: the retry loop passes req.attempt).
function flaky(m, req)
    if req.attempt = 0 then
        return { status: 500, headers: {}, body: "" }
    end if
    if req.attempt = 1 then
        h = {}
        h["Retry-After"] = "5"
        return { status: 503, headers: h, body: "" }
    end if
    ok = "{\"model\":\"claude-sonnet-4-6\",\"content\":[{\"type\":\"text\",\"text\":\"done\"}],\"usage\":{\"input_tokens\":3,\"output_tokens\":2},\"stop_reason\":\"end_turn\"}"
    return { status: 200, headers: {}, body: ok }
end function

' Injected backoff sleep: print instead of waiting.
function tick(seconds)
    print("backoff " + string(seconds) + "s")
end function
