' NAP-13 — llm.bas tool/function-calling, entirely offline.
'
' A SCRIPTED transport returns canned provider payloads in order, so multi-round
' tool conversations are exercised deterministically with no network and no keys.
' Covers both wire formats in both directions:
'   normalized tool definition -> provider request payload
'   provider tool-call response -> normalized tool calls
'   normalized tool result     -> provider continuation payload
' plus dispatch, argument validation, and every controlled failure class.
program main(args)
    load llm from "../stdlib/llm.bas"

    ' Program-global script state, read/written by the injected transport.
    SCRIPT = { i: 0, bodies: [] }

    ' ---- the tool registry ------------------------------------------------
    schema = {
        type: "object",
        properties: { id: { type: "integer" } },
        required: ["id"]
    }
    tools = [
        llm.tool("get_customer", "Retrieve a customer record", schema, get_customer),
        llm.tool("get_balance", "Retrieve a balance", schema, get_balance),
        llm.tool("explode", "Always fails", schema, explode),
        llm.tool("unserializable", "Returns a live value", schema, unserializable)
    ]

    a = llm.with_tools(llm.anthropic("claude-sonnet-4-6", "sk-ant-test"), tools)
    o = llm.with_tools(llm.openai("gpt-4o", "sk-oai-test"), tools)
    msgs = [ { role: "user", content: "Who is customer 42?" } ]

    print("== tool definitions -> request payload (anthropic) ==")
    print(llm._build_body(a, "sys", msgs))
    print("== tool definitions -> request payload (openai) ==")
    print(llm._build_body(o, "sys", msgs))

    ' ---- response -> normalized tool calls ---------------------------------
    print("== provider response -> normalized calls (anthropic) ==")
    ra = chat_scripted(a, anth_tool_call(), msgs)
    show_calls(llm.tool_calls(ra))
    print("== provider response -> normalized calls (openai) ==")
    ro = chat_scripted(o, oai_tool_call(), msgs)
    show_calls(llm.tool_calls(ro))

    ' ---- normalized results -> continuation payload ------------------------
    print("== tool results -> continuation payload (anthropic) ==")
    resa = llm.execute_tools(a, llm.tool_calls(ra))
    nexta = llm.append_tool_results(a, msgs, ra, resa)
    print(llm._build_body(a, "sys", nexta))
    print("== tool results -> continuation payload (openai) ==")
    reso = llm.execute_tools(o, llm.tool_calls(ro))
    nexto = llm.append_tool_results(o, msgs, ro, reso)
    print(llm._build_body(o, "sys", nexto))

    ' ---- the automatic loop, both formats ----------------------------------
    print("== run_tools: single call then final answer (anthropic) ==")
    fa = run_scripted(a, [ anth_tool_call(), anth_final() ], msgs)
    print("text=" + fa.text + " rounds=" + string(fa.rounds))
    print("== run_tools: single call then final answer (openai) ==")
    fo = run_scripted(o, [ oai_tool_call(), oai_final() ], msgs)
    print("text=" + fo.text + " rounds=" + string(fo.rounds))

    ' ---- multiple calls in one turn: ids and order preserved ---------------
    print("== multiple tool calls in one assistant turn (anthropic) ==")
    mres = llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_two_calls(), msgs)))
    show_results(mres)

    print("== multiple tool calls in one assistant turn (openai) ==")
    mres2 = llm.execute_tools(o, llm.tool_calls(chat_scripted(o, oai_two_calls(), msgs)))
    show_results(mres2)

    ' ---- multi-round: tool -> tool -> final --------------------------------
    print("== run_tools: two rounds then final (anthropic) ==")
    r2 = run_scripted(a, [ anth_tool_call(), anth_second_call(), anth_final() ], msgs)
    print("text=" + r2.text + " rounds=" + string(r2.rounds))
    print("transcript turns=" + string(count(r2.messages)))

    ' ---- no-tool response: ordinary chat is unchanged ----------------------
    print("== no tool calls: plain answer, zero rounds ==")
    r3 = run_scripted(a, [ anth_final() ], msgs)
    print("text=" + r3.text + " rounds=" + string(r3.rounds))
    n3 = count(llm.tool_calls(r3))
    print("tool_calls=" + string(n3))

    ' ---- controlled failure classes ---------------------------------------
    print("== unknown tool ==")
    show_results(llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_unknown_tool(), msgs))))

    print("== malformed tool arguments (openai: bad JSON string) ==")
    show_results(llm.execute_tools(o, llm.tool_calls(chat_scripted(o, oai_bad_json(), msgs))))

    print("== missing required field ==")
    show_results(llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_missing_field(), msgs))))

    print("== wrong argument type ==")
    show_results(llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_wrong_type(), msgs))))

    print("== tool reports failure by returning { error: ... } ==")
    show_results(llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_explode(), msgs))))

    print("== unserializable tool result ==")
    show_results(llm.execute_tools(a, llm.tool_calls(chat_scripted(a, anth_unserializable(), msgs))))

    print("== registry is the only authority: text naming a tool is not dispatched ==")
    r4 = run_scripted(a, [ anth_text_naming_tool() ], msgs)
    n4 = count(llm.tool_calls(r4))
    print("tool_calls=" + string(n4) + " text=" + r4.text)
end program

' ---- registered tools (must be TOTAL: return, never raise) ----------------

function get_customer(a)
    return { id: a.id, name: "Ada Lovelace" }
end function

function get_balance(a)
    return { id: a.id, balance: 1200 }
end function

function explode(a)
    return llm.tool_error("no such customer")
end function

function unserializable(a)
    return { fn: get_customer }
end function

' ---- scripted transport ---------------------------------------------------

function scripted(m, req)
    i = SCRIPT.i
    SCRIPT.i = i + 1
    b = SCRIPT.bodies[i]
    return { status: 200, headers: {}, body: b }
end function

' One-shot: a single chat() against one canned payload (no tool loop).
function chat_scripted(m, body, msgs)
    SCRIPT.i = 0
    SCRIPT.bodies = [ body ]
    mm = llm.with_transport(m, scripted)
    return llm.chat(mm, "sys", msgs)
end function

' Drive run_tools over a fixed list of provider payloads.
function run_scripted(m, bodies, msgs)
    SCRIPT.i = 0
    SCRIPT.bodies = bodies
    mm = llm.with_transport(m, scripted)
    return llm.run_tools(mm, "sys", msgs)
end function

' ---- reporting helpers ----------------------------------------------------

function show_calls(calls)
    for each c in calls
        print("  id=" + string(c.id) + " name=" + string(c.name) + " args=" + encode(c.arguments))
    end for
end function

function show_results(results)
    for each r in results
        print("  id=" + string(r.id) + " name=" + string(r.name) + " is_error=" + string(r.is_error) + " content=" + r.content)
    end for
end function

' ---- provider payload fixtures --------------------------------------------

function anth_tool_call()
    return "{\"model\":\"claude-sonnet-4-6\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"get_customer\",\"input\":{\"id\":42}}],\"usage\":{\"input_tokens\":10,\"output_tokens\":5},\"stop_reason\":\"tool_use\"}"
end function

function anth_second_call()
    return "{\"model\":\"claude-sonnet-4-6\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_2\",\"name\":\"get_balance\",\"input\":{\"id\":42}}],\"usage\":{\"input_tokens\":12,\"output_tokens\":5},\"stop_reason\":\"tool_use\"}"
end function

function anth_two_calls()
    return "{\"model\":\"claude-sonnet-4-6\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_a\",\"name\":\"get_customer\",\"input\":{\"id\":1}},{\"type\":\"tool_use\",\"id\":\"toolu_b\",\"name\":\"get_balance\",\"input\":{\"id\":2}}],\"usage\":{\"input_tokens\":10,\"output_tokens\":9},\"stop_reason\":\"tool_use\"}"
end function

function anth_final()
    return "{\"model\":\"claude-sonnet-4-6\",\"content\":[{\"type\":\"text\",\"text\":\"Customer 42 is Ada Lovelace.\"}],\"usage\":{\"input_tokens\":20,\"output_tokens\":6},\"stop_reason\":\"end_turn\"}"
end function

function anth_unknown_tool()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_x\",\"name\":\"drop_tables\",\"input\":{\"id\":1}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
end function

function anth_missing_field()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_m\",\"name\":\"get_customer\",\"input\":{}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
end function

function anth_wrong_type()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_w\",\"name\":\"get_customer\",\"input\":{\"id\":\"forty-two\"}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
end function

function anth_explode()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_e\",\"name\":\"explode\",\"input\":{\"id\":7}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
end function

function anth_unserializable()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_u\",\"name\":\"unserializable\",\"input\":{\"id\":7}}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"tool_use\"}"
end function

' Prose that merely NAMES a tool must never be dispatched.
function anth_text_naming_tool()
    return "{\"model\":\"c\",\"content\":[{\"type\":\"text\",\"text\":\"I would call get_customer({id: 42}) now.\"}],\"usage\":{\"input_tokens\":1,\"output_tokens\":1},\"stop_reason\":\"end_turn\"}"
end function

function oai_tool_call()
    return "{\"model\":\"gpt-4o\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"get_customer\",\"arguments\":\"{\\\"id\\\":42}\"}}]},\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}"
end function

function oai_two_calls()
    return "{\"model\":\"gpt-4o\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_a\",\"type\":\"function\",\"function\":{\"name\":\"get_customer\",\"arguments\":\"{\\\"id\\\":1}\"}},{\"id\":\"call_b\",\"type\":\"function\",\"function\":{\"name\":\"get_balance\",\"arguments\":\"{\\\"id\\\":2}\"}}]},\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":9}}"
end function

function oai_bad_json()
    return "{\"model\":\"gpt-4o\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_bad\",\"type\":\"function\",\"function\":{\"name\":\"get_customer\",\"arguments\":\"{id: 42\"}}]},\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1}}"
end function

function oai_final()
    return "{\"model\":\"gpt-4o\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Customer 42 is Ada Lovelace.\"},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":20,\"completion_tokens\":6}}"
end function
