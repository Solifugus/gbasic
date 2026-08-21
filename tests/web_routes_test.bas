' PLAT-WEB-1 routing semantics fixture. Golden-compared by tests/run_web_routes.sh.
'
' EVERY CHECK STATES ITS OWN EXPECTED ANSWER and prints `ok` or a MISMATCH
' naming both sides. A plain golden would record whatever the library answers
' AS the expected output, which for a router is exactly the wrong instrument:
' the failure mode is a plausible wrong route, not a crash. "/products/new"
' resolving to the "/products/{id}" handler produces a perfectly ordinary page
' about a product whose id is the word "new".
'
' The golden pins that every line says ok. The lines themselves decide what is
' correct, and their expectations were worked out from the rule, not from a run.

load web

' Results accumulate at TOP LEVEL, not in a counter the checker increments:
' a function has no closure over the outer scope, so `checks = checks + 1`
' inside `check` would raise the count on a local copy and report 0 forever --
' with every mismatch still printed but the summary claiming a clean run. The
' array is appended to from top level, where the mutation is real.
results = []

function h_root(req)
    return { body: "root" }
end function

function h_id(req)
    return { body: "id=" + req.params.id }
end function

function h_new(req)
    return { body: "new" }
end function

function h_rest(req)
    return { body: "rest=" + req.params.tail }
end function

function h_pair(req)
    return { body: req.params.a + "/" + req.params.b }
end function

function h_post(req)
    return { body: "posted" }
end function

function h_bad(req)
    return "not a record"
end function

' ---------------------------------------------------------------- helpers

' Every check goes through here and hands back its verdict, so a tier that
' quietly stopped running checks shows up as a smaller count rather than as a
' clean run.
function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

table = web.routes([
    { method: "get",  path: "/",                 handler: h_root },
    { method: "get",  path: "/products/{id}",    handler: h_id },
    { method: "get",  path: "/products/new",     handler: h_new },
    { method: "get",  path: "/files/{tail...}",  handler: h_rest },
    { method: "get",  path: "/x/{a}/y/{b}",      handler: h_pair },
    { method: "post", path: "/products",         handler: h_post },
    { method: "put",  path: "/products",         handler: h_post }
])

print "-- the table as data"
print join(web.paths(table), " | ")

print ""
print "-- exact matches"
r = web.dispatch(table, { method: "GET", path: "/" })
append(results, check("GET / body", "root", r.body))
append(results, check("GET / status", 200, r.status))

r = web.dispatch(table, { method: "POST", path: "/products" })
append(results, check("POST /products body", "posted", r.body))

print ""
print "-- captures"
r = web.dispatch(table, { method: "GET", path: "/products/42" })
append(results, check("a capture reaches the handler", "id=42", r.body))

r = web.dispatch(table, { method: "GET", path: "/x/1/y/2" })
append(results, check("two captures in one path", "1/2", r.body))

' Raw, deliberately: percent-decoding here would make %2F and / the same
' thing, which is a traversal bug in any handler that joins a capture to a
' directory.
r = web.dispatch(table, { method: "GET", path: "/products/a%2Fb" })
append(results, check("a capture is NOT percent-decoded", "id=a%2Fb", r.body))

r = web.dispatch(table, { method: "GET", path: "/files/css/site.css" })
append(results, check("a greedy capture takes the remainder", "rest=css/site.css", r.body))

r = web.dispatch(table, { method: "GET", path: "/files/one" })
append(results, check("a greedy capture takes a single segment too", "rest=one", r.body))

r = web.dispatch(table, { method: "GET", path: "/files" })
append(results, check("a greedy capture needs at least one segment", 404, r.status))

print ""
print "-- specificity, which is what decides -- not table order"
' "/products/new" is declared AFTER "/products/{id}", and wins anyway.
r = web.dispatch(table, { method: "GET", path: "/products/new" })
append(results, check("a static segment beats a capture", "new", r.body))

r = web.dispatch(table, { method: "GET", path: "/products/newest" })
append(results, check("and only for the exact word", "id=newest", r.body))

print ""
print "-- misses"
r = web.dispatch(table, { method: "GET", path: "/nothing" })
append(results, check("an unknown path is 404", 404, r.status))

r = web.dispatch(table, { method: "GET", path: "/products/1/2" })
append(results, check("an extra segment does not match", 404, r.status))

r = web.dispatch(table, { method: "GET", path: "/products/" })
append(results, check("a trailing slash is a different path", 404, r.status))

r = web.dispatch(table, { method: "GET", path: "/x//y/2" })
append(results, check("an empty segment never fills a capture", 404, r.status))

print ""
print "-- a wrong verb is not a missing page"
r = web.dispatch(table, { method: "DELETE", path: "/products" })
append(results, check("the status is 405", 405, r.status))
append(results, check("and Allow lists what is declared", "POST, PUT", r.headers["allow"]))

r = web.dispatch(table, { method: "DELETE", path: "/no/such/thing" })
append(results, check("but an unknown path stays 404", 404, r.status))

print ""
print "-- resolve, with no handler called and no response built"
found = web.resolve(table, "GET", "/products/42")
append(results, check("resolve reports the winning path", "/products/{id}", found.route.path))
append(results, check("resolve hands back the captures", "42", found.params.id))
append(results, check("resolve on a miss says so", false, web.resolve(table, "GET", "/zzz").ok))
append(results, check("method case does not matter", true, web.resolve(table, "get", "/").ok))

print ""
print "-- the response record"
r = web.dispatch(table, { id: 7, method: "GET", path: "/" })
append(results, check("the request id is carried onto the response", 7, r.id))
r = web.dispatch(table, { method: "GET", path: "/" })
append(results, check("a request with no id yields a response with none", false, has(r, "id")))

print ""
print "-- a handler that does not return a response record"
' 500 rather than a raise: with no worker pool yet, raising would take the
' whole listener down over one bad handler. The reason goes to standard
' error, which the runner captures separately.
broken = web.routes([{ method: "get", path: "/oops", handler: h_bad }])
r = web.dispatch(broken, { method: "GET", path: "/oops" })
append(results, check("the status is 500", 500, r.status))

bad = 0
for each verdict in results
    if not verdict then
        bad = bad + 1
    end if
end for

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
