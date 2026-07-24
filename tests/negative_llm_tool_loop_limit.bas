program main(args)
    load llm from "../stdlib/llm.bas"
    ' NAP-13: a provider that ALWAYS requests a tool must be stopped by the
    ' max-tool-rounds cap, raising rather than looping forever.
    schema = { type: "object", properties: { id: { type: "integer" } }, required: ["id"] }
    m = llm.anthropic("claude-sonnet-4-6", "sk-test")
    m = llm.with_tools(m, [ llm.tool("ping", "always called", schema, ping) ])
    m = llm.with_max_tool_rounds(m, 2)
    m = llm.with_transport(m, always_tool)
    print(llm.run_tools(m, "sys", [ { role: "user", content: "go" } ]).text)
end program

function ping(a)
    return { ok: true }
end function

function always_tool(m, req)
    b = "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"t1\",\"name\":\"ping\",\"input\":{\"id\":1}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
    return { status: 200, headers: {}, body: b }
end function
