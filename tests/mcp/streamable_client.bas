' The client side of Streamable HTTP (Transward §7.5) against
' streamable_server.bas, run by run_mcp.sh's streamable tier: the bearer token
' from the spec's headers, the session id and protocol version kept from
' initialize and sent on every call, across two workers.
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

function refused(spec)
    on error goto next
    h = mcp.connect(spec)
    m = error.message
    error.clear()
    return m
end function

url = env("MCP_URL")
auth = {}
auth["Authorization"] = "Bearer " + env("MCP_TOKEN")
h = mcp.connect({ transport: "http", url: url, headers: auth, client: "cli-test" })
check("the session id was kept from initialize", h.session != "", true)
check("and the version agreed", h.version, "2025-06-18")
check("the tools were listed in the session", count(mcp.tools_of(h)), 1)

' calls until two different workers have answered within the one session
workers = []
n = 0
while n < 500 and count(workers) < 2
    r = mcp.call(h, "whoami", {})
    p = decode(r.content)
    if not contains(workers, p.worker) then
        append(workers, p.worker)
    end if
    n = n + 1
end while
print "calls until two workers answered: " + string(n)
check("two workers served the one session, each checking its signature", count(workers), 2)
check("and the app knew the client from the session", p.client, "cli-test")
check("and the caller it built reached the tool", p.user, "token-holder")

check("without the token the app refuses", refused({ transport: "http", url: url }),
      "mcp: the server answered HTTP 401")
bad = {}
bad["Authorization"] = 7
check("a header that is not a string", refused({ transport: "http", url: url, headers: bad }),
      "mcp: the header Authorization must be a string")
check("headers on stdio", refused({ transport: "stdio", command: "true", headers: auth }),
      "mcp: headers are for the http transport; a stdio server is a child process")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
