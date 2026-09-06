' PLAT-HTTP: `http` -- requests that do not block the program.
' Self-checking; run by tests/run_http.sh against tests/http/fixture_server.py.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced. Every defect this
' module can have produces an ORDINARY-LOOKING RESULT: a status of 500 reported
' as a failure, a transfer that secretly blocked, a body that arrived all at
' once instead of in pieces. A golden records whatever came out and then
' defends it, and none of those would move a line a reader would question.
'
' THE LOAD-BEARING TIER IS TRANSPORT-VS-HTTP. `transport_ok` says the bytes
' arrived; `status` says what the server thought of the request. A field named
' `success` beside a status code gets read as "the request worked", once,
' quietly -- so the pair is asserted as a DIFFERENCE: a 500 must be a
' SUCCESSFUL transfer carrying status 500, and a refused connection must be a
' failed one carrying no status at all. Either half alone passes on a module
' that conflates them; both together do not.

load http
load webclient

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

base = "http://127.0.0.1:" + env("HTTP_FIXTURE_PORT")

' --- transport is not HTTP --------------------------------------------------
' A 500 is a transfer that SUCCEEDED. Asserting only that the refused
' connection fails would pass on a module reporting `status < 400`.
five = http.wait(http.start({ url: base + "/boom" }))
check("a 500 completed: transport_ok is TRUE", five.transport_ok, true)
check("a 500 carries its status", five.status, 500)
check("a 500 carries its reason", five.reason, "Internal Server Error")
check("a 500 carries its headers", five.headers["x-note"], "five hundred")
bh = http.start({ url: base + "/boom" })
http.wait(bh)
check("a 500 carries its body", http.read(bh).body, "server said no")
check("a completed transfer reports no error", five.error, "")

' Port 1 is privileged and unbound: the connection is refused at once, with no
' DNS in the path and nothing to wait for.
dead = http.wait(http.start({ url: "http://127.0.0.1:1/" }))
check("a refused connection: transport_ok is FALSE", dead.transport_ok, false)
check("a refused connection has NO status", dead.status, 0)
check("a refused connection says why", len(dead.error) > 0, true)
check("both finished, so `running` cannot be what separates them",
      five.running = dead.running, true)

' --- parity with webclient, which is the oracle -----------------------------
' webclient performs the same request over libcurl's EASY interface, blocking.
' It is a separate implementation of the same thing, so agreement is evidence
' rather than a restatement -- and the header parser is deliberately shared, so
' a divergence here means an option on the multi path changed the semantics.
via_easy = webclient.get(base + "/")
h = http.start({ url: base + "/" })
via_multi = http.wait(h)
body_multi = http.read(h).body
check("same status through both clients", via_multi.status, via_easy.status)
check("same body through both clients", body_multi, via_easy.body)
check("same response header through both clients",
      via_multi.headers["x-fixture"], via_easy.headers["x-fixture"])
check("the body is what the fixture sent", body_multi, "hello from the fixture")

' --- the request itself -----------------------------------------------------
p = http.start({ url: base + "/echo", method: "POST", body: "round trip" })
ps = http.wait(p)
check("POST status", ps.status, 200)
' COPYPOSTFIELDS, not POSTFIELDS: the request record is freed before the
' transfer runs, so a body curl did not copy is read after free.
check("POST body survives the start call returning", http.read(p).body, "round trip")

r = http.start({ url: base + "/reflect", headers: { "X-Probe": "sent" } })
http.wait(r)
check("request headers reach the server",
      contains(http.read(r).body, "X-Probe=sent"), true)

' --- incremental reads ------------------------------------------------------
' The reason the module exists: bytes are available BEFORE the transfer ends.
' A single-read assertion passes on a module that buffers the whole body.
s = http.start({ url: base + "/slow?n=4&ms=120" })
reads = 0
seen = ""
while true
    st = http.poll(s)
    chunk = http.read(s)
    if len(chunk.body) > 0 then
        reads = reads + 1
        seen = seen + chunk.body
    end if
    if not st.running then break
end while
check("the body arrived in more than one read", reads > 1, true)
check("and nothing was lost or duplicated across them",
      seen, "tick0\ntick1\ntick2\ntick3\n")
check("a drained handle reads empty", http.read(s).body, "")

' --- stop -------------------------------------------------------------------
sh = http.start({ url: base + "/slow?n=10&ms=200" })
http.stop(sh)
ss = http.poll(sh)
check("a stopped transfer is finished", ss.running, false)
check("a stopped transfer did not complete", ss.transport_ok, false)
check("a stopped transfer says so", ss.error, "stopped by http.stop")
check("stopping a finished transfer is harmless", http.stop(p), true)

' --- the handle -------------------------------------------------------------
check("a handle knows its kind", h.kind, "http")
check("a handle knows its id", h.id > 0, true)
check("two transfers are two handles", h.id != p.id, true)
check("a handle is not equal to another handle", h = p, false)
check("a handle equals itself", h = h, true)

print "checks " + string(tally.checks) + " mismatches " + string(tally.mismatches)
