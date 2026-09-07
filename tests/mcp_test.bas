' `mcp` -- publishing a toolset over the Model Context Protocol.
' Self-checking; run by tests/run_mcp.sh.
'
' docs/gbasic_ai_reference_and_primitives.md §1.11, step 6.
'
' TWO TRANSPORTS, ONE DISPATCHER. `mcp.handle` turns a JSON-RPC request into a
' reply and does no I/O, so the stdio loop and an HTTP `server` block are thin
' wrappers over the same function -- and the PROTOCOL is testable with neither
' transport present, which is what this file does. The transports get their own
' tiers because what they can get wrong is different: buffering and framing,
' not semantics.
'
' THE DISTINCTION THIS FILE EXISTS FOR is between a TOOL that failed and a
' PROTOCOL error. A tool that raises is a RESULT with isError -- the model
' asked for something reasonable and got an answer it can react to. A method
' that does not exist is a JSON-RPC error -- the CLIENT is malformed, and it
' cannot fix that by trying again differently. Collapsing the two either way
' produces a perfectly well-formed reply that means the wrong thing, which is
' why both are asserted here rather than one.

load mcp
load tools

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function get_balance(args)
    return "42.00 for " + args.id
end function
function whoami(args)
    p = principal()
    if p = nothing then return "nobody"
    return p.user
end function
function boom(args)
    error "the tool raised"
    return "unreached"
end function

ts = tools.define("bank", [
    { name: "get_balance", describe: "Read an account balance.",
      params: [ { name: "id", type: "string", describe: "the account id", required: true } ],
      reads: ["accounts"], mutates: [], fn: get_balance },
    { name: "whoami", describe: "Report the principal.",
      params: [], reads: [], mutates: [], fn: whoami },
    { name: "boom", describe: "Always fails.",
      params: [], reads: [], mutates: [], fn: boom }
])

plain = { name: "bank-mcp", version: "1.0.0" }
mapped = { name: "bank-mcp", version: "1.0.0",
           principal: { user: "alice", groups: ["tellers"] }, max_groups: 2 }

function reply(ts, opts, obj)
    return decode(mcp.handle(ts, opts, json_encode(obj)))
end function

' ---- initialize ------------------------------------------------------------
i = reply(ts, plain, { jsonrpc: "2.0", id: 1, method: "initialize" })
check("initialize answers with the protocol version", i.result.protocolVersion, "2024-11-05")
check("and declares a tools capability", has(i.result.capabilities, "tools"), true)
check("and names the server", i.result.serverInfo.name, "bank-mcp")
check("echoing the request id", i.id, 1)

' ---- tools/list ------------------------------------------------------------
l = reply(ts, plain, { jsonrpc: "2.0", id: 2, method: "tools/list" })
check("tools/list publishes every tool", count(l.result.tools), 3)
check("in declared order", l.result.tools[0].name, "get_balance")
check("with its description, which is what the model reads",
      l.result.tools[0].description, "Read an account balance.")
' MCP calls it inputSchema; tools.schema calls it parameters. The rename lives
' in one place, and this is the check that says so.
check("under MCP's own name for the schema",
      has(l.result.tools[0], "inputSchema"), true)
check("and NOT under this stdlib's name for it",
      has(l.result.tools[0], "parameters"), false)
check("carrying what the tool requires",
      l.result.tools[0].inputSchema.required[0], "id")

' ---- tools/call ------------------------------------------------------------
c = reply(ts, plain, { jsonrpc: "2.0", id: 3, method: "tools/call",
                       params: { name: "get_balance", arguments: { id: "a1" } } })
check("a call returns content", c.result.content[0].text, "42.00 for a1")
check("as a text block", c.result.content[0].type, "text")
check("and is not an error", c.result.isError, false)

' ---- THE DISTINCTION -------------------------------------------------------
' A tool that failed is a RESULT; a method that does not exist is an ERROR.
b = reply(ts, plain, { jsonrpc: "2.0", id: 4, method: "tools/call",
                       params: { name: "boom", arguments: {} } })
check("A TOOL THAT RAISES IS A RESULT, not a protocol error",
      has(b, "result"), true)
check("marked isError, so the model knows it failed", b.result.isError, true)
check("carrying what went wrong", contains(b.result.content[0].text, "the tool raised"), true)

m = reply(ts, plain, { jsonrpc: "2.0", id: 5, method: "nosuch" })
check("A METHOD THAT DOES NOT EXIST IS A PROTOCOL ERROR", has(m, "error"), true)
check("with JSON-RPC's own code", m["error"].code, -32601)
check("and no result", has(m, "result"), false)

' An unknown TOOL is still a result -- the client is well-formed and the model
' can pick another tool.
u = reply(ts, plain, { jsonrpc: "2.0", id: 6, method: "tools/call",
                       params: { name: "nosuch_tool", arguments: {} } })
check("an unknown TOOL is a result, not a protocol error", has(u, "result"), true)
check("marked as an error the model can read", u.result.isError, true)

' ---- malformed input -------------------------------------------------------
p1 = decode(mcp.handle(ts, plain, "{not json"))
check("unparseable input is a parse error", p1["error"].code, -32700)
p2 = decode(mcp.handle(ts, plain, "[1,2,3]"))
check("a non-object request is invalid", p2["error"].code, -32600)
p3 = decode(mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", id: 9 })))
check("a request with no method is invalid", p3["error"].code, -32600)
p4 = decode(mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", id: 10,
      method: "tools/call", params: { arguments: {} } })))
check("tools/call with no tool name is invalid params", p4["error"].code, -32602)

' ---- notifications get NO reply --------------------------------------------
' Answering a notification is the commonest way a JSON-RPC server confuses a
' client: it is not waiting for that reply, so the reply arrives as the answer
' to whatever it asks next and every subsequent exchange is off by one.
n = mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", method: "notifications/initialized" }))
check("a notification gets no reply at all", n, "")
n2 = mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", method: "tools/list" }))
check("even for a method that would otherwise answer", n2, "")
' The control: the same method WITH an id does answer, so it is the missing id
' doing the work and not the method being ignored.
n3 = mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", id: 11, method: "tools/list" }))
check("and the same method with an id does answer", len(n3) > 0, true)

' ---- the mapped principal reaches the tool ---------------------------------
w1 = reply(ts, plain, { jsonrpc: "2.0", id: 12, method: "tools/call",
                        params: { name: "whoami", arguments: {} } })
check("with no mapping a tool sees no principal", w1.result.content[0].text, "nobody")
w2 = reply(ts, mapped, { jsonrpc: "2.0", id: 13, method: "tools/call",
                         params: { name: "whoami", arguments: {} } })
check("with one it sees the configured identity", w2.result.content[0].text, "alice")

' ---- THE CEILING: the mapping is the leak surface --------------------------
on error goto next

x = mcp.check_options({ name: "s", version: "1",
                        principal: { user: "svc", groups: ["a","b","c"] }, max_groups: 2 })
if error then
    check("a mapping wider than its ceiling REFUSES TO START",
          contains(error.message, "but the ceiling is 2"), true)
    error.clear()
end if

x = mcp.check_options({ name: "s", version: "1", principal: { user: "svc", groups: [] } })
if error then
    check("a mapped principal with no ceiling is refused",
          contains(error.message, "needs a max_groups ceiling"), true)
    error.clear()
end if

x = mcp.check_options({ name: "s", version: "1", max_groups: 5 })
if error then
    check("and a ceiling with nothing to bound is refused",
          contains(error.message, "no principal to bound"), true)
    error.clear()
end if

x = mcp.check_options({ name: "s" })
if error then
    check("a server needs a version", contains(error.message, "needs a version"), true)
    error.clear()
end if

x = mcp.check_options({ version: "1" })
if error then
    check("and a name", contains(error.message, "needs a name"), true)
    error.clear()
end if

x = mcp.check_options({ name: "s", version: "1", princpal: {} })
if error then
    check("an unknown option is refused by name",
          contains(error.message, "no option 'princpal'"), true)
    error.clear()
end if

' The controls. Without these the ceiling is indistinguishable from refusing
' every mapping, which would make the feature unusable rather than safe.
ok1 = mcp.check_options({ name: "s", version: "1" })
check("the control: no principal at all is legal -- and it is the NARROWEST state",
      is_nothing(ok1), true)
ok2 = mcp.check_options({ name: "s", version: "1",
                          principal: { user: "alice", groups: ["tellers"] }, max_groups: 2 })
check("and a mapping within its ceiling is accepted", is_nothing(ok2), true)
ok3 = mcp.check_options({ name: "s", version: "1",
                          principal: { user: "alice", groups: ["a","b"] }, max_groups: 2 })
check("exactly at the ceiling is accepted", is_nothing(ok3), true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
