program main(args)
    load llm from "../stdlib/llm.bas"
    ' NAP-13: a tool definition must carry a real callable, not a value.
    schema = { type: "object", properties: {}, required: [] }
    print(llm.tool("bad", "not callable", schema, 42).name)
end program
