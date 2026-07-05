program main(args)
    load llm from "../stdlib/llm.bas"
    ' WP-LLM-1 §4: 429/5xx that never recovers must RAISE after the retry budget
    ' (default 3). Injected always-500 transport + a silent injected sleep (no
    ' network, no real waiting): 3 backoffs, then raise.
    m = llm.anthropic("claude-sonnet-4-6", "sk-test")
    m = llm.with_transport(m, dead)
    m = llm.with_sleep(m, nowait)
    print(llm.ask(m, "sys", "go"))
end program

function dead(m, req)
    return { status: 500, headers: {}, body: "" }
end function

function nowait(seconds)
end function
