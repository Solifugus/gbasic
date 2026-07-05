' WP-LLM-1 — llm.bas adapters (§1, §3), both wire formats, entirely offline.
' Request-built golden: _build_body renders the exact wire body for each format
' (anthropic carries `system` as a top-level field; openai as a leading system
' message). Response-parsed golden: chat() runs through the offline fixture
' transport (examples/fixtures/llm/{format}_response.json) and the common
' response shape { text, usage:{input,output}, stop_reason, model } is extracted,
' proving usage extraction for both formats.
program main(args)
    load llm from "../stdlib/llm.bas"

    a = llm.anthropic("claude-sonnet-4-6", "sk-ant-test")
    o = llm.openai("gpt-4o", "sk-oai-test")
    sys = "You are a forensic analyst."
    msgs = [ {role: "user", content: "Summarize the accruals."} ]

    print("== request built (anthropic) ==")
    print(llm._build_body(a, sys, msgs))
    print("== request built (openai) ==")
    print(llm._build_body(o, sys, msgs))

    a = llm.offline(a, "examples/fixtures/llm")
    o = llm.offline(o, "examples/fixtures/llm")
    ra = llm.chat(a, sys, msgs)
    ro = llm.chat(o, sys, msgs)

    print("== response parsed (anthropic) ==")
    print("text=" + ra.text)
    print("usage in=" + string(ra.usage.input) + " out=" + string(ra.usage.output))
    print("stop=" + ra.stop_reason + " model=" + ra.model)
    print("== response parsed (openai) ==")
    print("text=" + ro.text)
    print("usage in=" + string(ro.usage.input) + " out=" + string(ro.usage.output))
    print("stop=" + ro.stop_reason + " model=" + ro.model)

    print("== ask (90% case -> string) ==")
    print(llm.ask(a, sys, "Give me one line."))
end program
