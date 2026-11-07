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
function caller_echo(args)
    return principal()
end function
' ONE FUNCTION, MANY ENTRIES (Transward §7.2): a tool is called as a method
' of its entry, so `this` says which entry it is serving
function shared(args)
    return "served " + this.name
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
      params: [], reads: [], mutates: [], fn: boom },
    { name: "caller_echo", describe: "Report the whole principal.",
      params: [], reads: [], mutates: [], fn: caller_echo },
    { name: "first_of_two", describe: "One of two entries sharing a function.",
      params: [], reads: [], mutates: [], fn: shared },
    { name: "second_of_two", describe: "The other.",
      params: [], reads: [], mutates: [], fn: shared }
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
check("tools/list publishes every tool", count(l.result.tools), 6)
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

' ---- one function serving many entries (Transward §7.2) ---------------------
s1 = reply(ts, plain, { jsonrpc: "2.0", id: 30, method: "tools/call",
                        params: { name: "first_of_two", arguments: {} } })
s2 = reply(ts, plain, { jsonrpc: "2.0", id: 31, method: "tools/call",
                        params: { name: "second_of_two", arguments: {} } })
check("a shared function knows the entry it serves", s1.result.content[0].text, "served first_of_two")
check("and so does the other entry", s2.result.content[0].text, "served second_of_two")

' ---- a principal per request (Transward §7.3) -------------------------------
' A record the caller verified for THIS request, an application's own fields
' included, passes through untouched and wins over a configured mapping.
mine = { kind: "user", name: "ana", actor: "u:17", role: "operator", via: "mcp",
         client: "claude-desktop", source_ip: "10.0.0.5", groups: ["a", "b", "c", "d"] }
function reply_as(ts, opts, obj, who)
    return decode(mcp.handle(ts, opts, json_encode(obj), who))
end function
c1 = reply_as(ts, plain, { jsonrpc: "2.0", id: 32, method: "tools/call",
                           params: { name: "caller_echo", arguments: {} } }, mine)
check("the caller's record reaches the tool untouched", c1.result.content[0].text, string(mine))
c2 = reply_as(ts, mapped, { jsonrpc: "2.0", id: 33, method: "tools/call",
                            params: { name: "whoami", arguments: {} } }, { user: "bo" })
check("and takes the place of the configured mapping", c2.result.content[0].text, "bo")
c4 = reply_as(ts, mapped, { jsonrpc: "2.0", id: 36, method: "tools/call",
                            params: { name: "caller_echo", arguments: {} } }, mine)
check("four groups, past the mapping's ceiling of 2, still pass: a ceiling bounds a mapping",
      c4.result.content[0].text, string(mine))
c3 = reply(ts, mapped, { jsonrpc: "2.0", id: 34, method: "tools/call",
                         params: { name: "whoami", arguments: {} } })
check("without one, the mapping is used as before", c3.result.content[0].text, "alice")
on error goto next
x = mcp.handle(ts, plain, json_encode({ jsonrpc: "2.0", id: 35, method: "tools/list" }), "ana")
check("a caller that is not a record is refused", error.message,
      "mcp.handle: the caller is a record of who this request is for, not string")
error.clear()
on error stop

' ---- resources, templates and prompts (Transward §7.1) ---------------------
function health(vars)
    return { ok: true, who: principal() }
end function
function transcript(vars)
    return "transcript of " + vars.run_id + " by " + this.name
end function
function step_of(vars)
    return vars.run_id + "/" + vars.step
end function
function failing(vars)
    error "the store is down"
    return ""
end function
function diagnose(args)
    return "Why did run " + args.run_id + " fail?"
end function
function chat(args)
    return [ { role: "user", text: "hello" }, { role: "assistant", text: "hi " + args.who } ]
end function
srv = mcp.server(ts, {
    resources: [ { uri: "app://health", name: "health", describe: "Health.", mime: "application/json", fn: health },
                 { uri: "app://broken", name: "broken", fn: failing } ],
    templates: [ { uri: "app://runs/{run_id}/transcript", name: "transcript", describe: "A run's transcript.",
                   mime: "text/plain", fn: transcript },
                 { uri: "app://runs/{run_id}/steps/{step}", name: "step", fn: step_of } ],
    prompts: [ { name: "diagnose", describe: "Diagnose a failed run.",
                 arguments: [ { name: "run_id", describe: "the run", required: true } ], fn: diagnose },
               { name: "chat", arguments: [ { name: "who" } ], fn: chat } ]
})
i2 = reply(srv, plain, { jsonrpc: "2.0", id: 40, method: "initialize" })
check("a declaration advertises resources", has(i2.result.capabilities, "resources"), true)
check("and prompts", has(i2.result.capabilities, "prompts"), true)
check("and still tools", has(i2.result.capabilities, "tools"), true)
check("a bare toolset advertises neither", has(i.result.capabilities, "resources"), false)
t2 = reply(srv, plain, { jsonrpc: "2.0", id: 41, method: "tools/list" })
check("its tools are the toolset's", count(t2.result.tools), 6)
rl = reply(srv, plain, { jsonrpc: "2.0", id: 42, method: "resources/list" })
check("resources/list derives from the declaration", count(rl.result.resources), 2)
check("in MCP's spelling", rl.result.resources[0].mimeType, "application/json")
check("with its description", rl.result.resources[0].description, "Health.")
tl = reply(srv, plain, { jsonrpc: "2.0", id: 43, method: "resources/templates/list" })
check("templates are listed apart", tl.result.resourceTemplates[0].uriTemplate, "app://runs/{run_id}/transcript")
r1 = reply(srv, plain, { jsonrpc: "2.0", id: 44, method: "resources/read", params: { uri: "app://runs/r-17/transcript" } })
check("a template's part reaches its function, called as a method", r1.result.contents[0].text, "transcript of r-17 by transcript")
check("with the declared type", r1.result.contents[0].mimeType, "text/plain")
check("and the URI read", r1.result.contents[0].uri, "app://runs/r-17/transcript")
r2 = reply(srv, plain, { jsonrpc: "2.0", id: 45, method: "resources/read", params: { uri: "app://runs/r-17/steps/4" } })
check("two parts in one template", r2.result.contents[0].text, "r-17/4")
r3 = reply_as(srv, plain, { jsonrpc: "2.0", id: 46, method: "resources/read", params: { uri: "app://health" } }, { user: "cy" })
check("a resource runs under the request's principal, its record sent as JSON",
      r3.result.contents[0].text, json_encode({ ok: true, who: { user: "cy" } }))
r4 = reply(srv, plain, { jsonrpc: "2.0", id: 47, method: "resources/read", params: { uri: "app://runs/a/b/transcript" } })
check("a part never spans a /", has(r4, "error"), true)
r5 = reply(srv, plain, { jsonrpc: "2.0", id: 48, method: "resources/read", params: { uri: "app://nothing" } })
check("an unknown URI is MCP's resource-not-found", r5.error.code, -32002)
r6 = reply(srv, plain, { jsonrpc: "2.0", id: 49, method: "resources/read", params: { uri: "app://broken" } })
check("a resource that raises is an internal error, saying why", r6.error.message, "the resource failed: the store is down")
pl = reply(srv, plain, { jsonrpc: "2.0", id: 50, method: "prompts/list" })
check("prompts/list derives from the declaration", pl.result.prompts[0].arguments[0].required, true)
check("an argument not marked required is optional", pl.result.prompts[1].arguments[0].required, false)
p1 = reply(srv, plain, { jsonrpc: "2.0", id: 51, method: "prompts/get", params: { name: "diagnose", arguments: { run_id: "r-9" } } })
check("a prompt's text becomes one user message", p1.result.messages[0].content.text, "Why did run r-9 fail?")
check("as MCP shapes it", p1.result.messages[0].role + "/" + p1.result.messages[0].content.type, "user/text")
p2 = reply(srv, plain, { jsonrpc: "2.0", id: 52, method: "prompts/get", params: { name: "chat", arguments: { who: "ana" } } })
check("or several messages", p2.result.messages[1].content.text, "hi ana")
p3 = reply(srv, plain, { jsonrpc: "2.0", id: 53, method: "prompts/get", params: { name: "diagnose", arguments: {} } })
check("a required argument left out is refused", p3.error.message, "prompt 'diagnose' needs the argument 'run_id'")
p4 = reply(srv, plain, { jsonrpc: "2.0", id: 54, method: "prompts/get", params: { name: "diagnose", arguments: { run_id: "r", x: "y" } } })
check("an undeclared one too", p4.error.message, "prompt 'diagnose' has no argument 'x'")
p5 = reply(srv, plain, { jsonrpc: "2.0", id: 55, method: "prompts/get", params: { name: "nope" } })
check("an unknown prompt", p5.error.code, -32602)

' declarations are checked once, at start
function decl_error(decl)
    on error goto next
    x = mcp.server(ts, decl)
    m = error.message
    error.clear()
    return m
end function
check("a template with no part", decl_error({ templates: [ { uri: "app://x", name: "x", fn: health } ] }),
      "mcp: a template 'app://x' has no {name} part; a URI with none is a resource, not a template")
check("a resource with a part", decl_error({ resources: [ { uri: "app://{x}", name: "x", fn: health } ] }),
      "mcp: a resource 'app://{x}' holds a {...} part; declare it under templates")
check("two parts with nothing between", decl_error({ templates: [ { uri: "app://{a}{b}", name: "x", fn: health } ] }),
      "mcp: a template 'app://{a}{b}' has two {...} parts with nothing between them, so where one ends cannot be told")
check("a URI declared twice", decl_error({ resources: [ { uri: "app://h", name: "a", fn: health }, { uri: "app://h", name: "b", fn: health } ] }),
      "mcp: a resource 'app://h' is declared twice")
check("a misspelt field", decl_error({ prompts: [ { name: "p", fn: diagnose, argument: [] } ] }),
      "mcp: a prompt has no field 'argument'; it takes name, describe, arguments, fn")
check("no fn", decl_error({ prompts: [ { name: "p" } ] }), "mcp: prompt 'p' needs fn, the function that answers it")
check("an unknown section", decl_error({ resource: [] }),
      "mcp: the declaration has no field 'resource'; it takes resources, templates, prompts")

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
