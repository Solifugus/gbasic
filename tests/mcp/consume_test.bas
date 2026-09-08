' `mcp` CONSUMING: gBASIC as an MCP client.
' Self-checking; run by tests/run_mcp.sh.
'
' The other direction from publishing. Tested gBASIC-to-gBASIC -- our own
' publisher launched as a subprocess, and our own HTTP server -- so the whole
' loop is exercised with nothing external and no network beyond loopback.
'
' THE LOAD-BEARING TIER IS THE DECLARATION MEETING THE SERVER AT CONNECT TIME.
' §1.11 asks for load-time and connect-time failures to be distinct, and the
' reason is practical: a tool the caller declared and the server does not
' advertise must fail WHEN THE CONNECTION IS MADE, not at the first call hours
' later inside whatever the agent happened to be doing. Its CONTROL is a spec
' whose expectations the server does meet, without which "connect refuses" is
' satisfied by a client that refuses everything.
'
' A REMOTE TOOL RESULT HAS THE SAME SHAPE AS A LOCAL ONE. `mcp.call` returns
' what `tools.dispatch` returns, so whatever consumes a result cannot tell -- and
' should not have to -- whether the work happened here or somewhere else.

load mcp

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

function names_of(h)
    out = []
    for each t in mcp.tools_of(h)
        append(out, t.name)
    end for
    return join(out, ",")
end function

' ---- stdio: our own publisher, launched as a subprocess ----------------
h = mcp.connect({ transport: "stdio", command: "./gbasic",
                  args: ["--line-buffered", "tests/mcp/server.bas"],
                  expect: ["get_balance", "whoami"] })
check("the server's advertised tools come back", names_of(h), "get_balance,whoami,boom")

r = mcp.call(h, "get_balance", { id: "a1" })
check("a remote call returns its content", r.content, "42.00 for a1")
check("and is not an error", r.is_error, false)
check("in the SAME SHAPE a local tools.dispatch returns", r.type, "tool_result")
check("naming the tool", r.name, "get_balance")

' A tool that failed on the far side is an error RESULT here, not a raise:
' the call reached the server and the server answered.
b = mcp.call(h, "boom", {})
check("a tool that failed remotely is an error result", b.is_error, true)
check("carrying what the far side said", contains(b.content, "the tool raised"), true)

u = mcp.call(h, "nosuch", {})
check("an unknown tool is an error result too", u.is_error, true)

mcp.disconnect(h)

' ---- http: the same server behind a socket ------------------------------
' `env` returns UNKNOWN for a variable that is not set, not "" -- and
' `unknown != ""` is TRUE, so the obvious guard runs the branch it meant to
' skip. Same family as a missing record key, and the fourth time this shape
' appeared today.
if is_string(env("MCP_URL")) then
    hh = mcp.connect({ transport: "http", url: env("MCP_URL"), expect: ["get_balance"] })
    check("the http transport lists too", names_of(hh), "get_balance")
    hr = mcp.call(hh, "get_balance", { id: "a1" })
    check("and calls", hr.content, "42.00 for a1")
    check("with the same result shape as stdio", hr.type, r.type)
    mcp.disconnect(hh)
end if

' ---- THE DECLARATION MEETS THE SERVER ----------------------------------
on error goto next

x = mcp.connect({ transport: "stdio", command: "./gbasic",
                  args: ["--line-buffered", "tests/mcp/server.bas"],
                  expect: ["get_balance", "not_offered"] })
if error then
    check("a tool the server does not advertise fails AT CONNECT",
          contains(error.message, "does not advertise 'not_offered'"), true)
    check("and the message says what IS offered, so the fix is visible",
          contains(error.message, "get_balance"), true)
    error.clear()
end if

' THE CONTROL. Without it, "connect refuses" is satisfied by a client that
' refuses every server.
ok = mcp.connect({ transport: "stdio", command: "./gbasic",
                   args: ["--line-buffered", "tests/mcp/server.bas"],
                   expect: ["get_balance", "whoami", "boom"] })
check("the control: a declaration the server MEETS connects",
      names_of(ok), "get_balance,whoami,boom")
mcp.disconnect(ok)

' ---- refusals, before anything is launched -----------------------------
x = mcp.connect({ transport: "carrier-pigeon", command: "x" })
if error then
    check("an unknown transport is refused",
          contains(error.message, "unknown transport"), true)
    error.clear()
end if

x = mcp.connect({ transport: "stdio" })
if error then
    check("a stdio server with no command is refused",
          contains(error.message, "needs a command"), true)
    error.clear()
end if

x = mcp.connect({ transport: "http" })
if error then
    check("an http server with no url is refused",
          contains(error.message, "needs a url"), true)
    error.clear()
end if

x = mcp.connect({ transport: "stdio", command: "x", comand: "typo" })
if error then
    check("a misspelled spec field is refused BY NAME",
          contains(error.message, "no spec field 'comand'"), true)
    error.clear()
end if

x = mcp.connect("not a record")
if error then
    check("and a spec must be a record", contains(error.message, "spec record"), true)
    error.clear()
end if

' A server that is not there must fail with a diagnostic rather than hang.
x = mcp.connect({ transport: "stdio", command: "./gbasic",
                  args: ["--line-buffered", "tests/mcp/not_a_server.bas"] })
if error then
    check("a server that never answers is reported, not waited on forever",
          contains(error.message, "mcp:"), true)
    error.clear()
end if

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
