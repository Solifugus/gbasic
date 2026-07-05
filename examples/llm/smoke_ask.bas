' MANUAL live smoke — NOT part of `make test` (real network + a real API key).
' Run one of:
'   ANTHROPIC_API_KEY=sk-ant-...  ./gbasic examples/llm/smoke_ask.bas anthropic
'   OPENAI_API_KEY=sk-...         ./gbasic examples/llm/smoke_ask.bas openai
'   ./gbasic examples/llm/smoke_ask.bas local   # Ollama at :11434
'
' Demonstrates the whole path against a live provider: constructor -> ask ->
' string, and chat -> {text, usage, stop_reason, model}. Keys come from the
' conventional env var (never hard-code one here).
program main(args)
    load llm from "../../stdlib/llm.bas"

    which = "anthropic"
    if count(args) > 0 then
        which = args[0]
    end if

    if which = "openai" then
        m = llm.openai("gpt-4o", unknown)          ' key from OPENAI_API_KEY
    else
        if which = "local" then
            m = llm.local("http://localhost:11434/v1", "llama3.3:70b")
        else
            m = llm.anthropic("claude-sonnet-4-6", unknown)   ' key from ANTHROPIC_API_KEY
        end if
    end if

    system = "You are a terse financial analyst. Answer in one sentence."
    prompt = "In plain terms, why is a sudden jump in a company's receivables relative to sales a red flag?"

    print("== ask (" + which + ") ==")
    print(llm.ask(m, system, prompt))

    print("== chat (full response record) ==")
    r = llm.chat(m, system, [ { role: "user", content: prompt } ])
    print("text : " + r.text)
    print("usage: in=" + string(r.usage.input) + " out=" + string(r.usage.output))
    print("stop : " + string(r.stop_reason) + "  model: " + string(r.model))
end program
