program main(args)
    load llm from "../stdlib/llm.bas"
    ' NAP-13: a duplicate tool name is a program bug — caught at configuration
    ' time, not silently shadowed mid-conversation.
    schema = { type: "object", properties: {}, required: [] }
    m = llm.anthropic("claude-sonnet-4-6", "sk-test")
    print(llm.with_tools(m, [ llm.tool("dup", "one", schema, f), llm.tool("dup", "two", schema, f) ]).format)
end program

function f(a)
    return 1
end function
