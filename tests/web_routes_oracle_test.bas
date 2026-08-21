' PLAT-WEB-1 routing ORACLE + ORDER-INDEPENDENCE fixture.
' Golden-compared by tests/run_web_routes.sh.
'
' The semantics fixture states expected answers a person worked out. That
' catches a router that is wrong the way a person can imagine. It cannot catch
' a router that is CONSISTENTLY wrong in a way nobody thought to write a case
' for -- and "which of these two patterns wins" has a large enough space of
' inputs that hand-picked cases are a sample, not a proof.
'
' So this fixture does two things no list of expectations can:
'
'   ORDER-INDEPENDENCE. The same route set is declared in four different
'   orders and every request must resolve identically in all four. A
'   first-match-wins router passes any single-order test and fails this one.
'   The property is checked between runs of the library against itself, which
'   is weak evidence of CORRECTNESS but conclusive about ORDER -- exactly the
'   claim being made.
'
'   AN ORACLE. Every request path over a small alphabet is enumerated and the
'   library's answer is checked against a matcher and a specificity rule
'   REIMPLEMENTED HERE, in gBASIC, from the definition. This is not a second
'   call into the same code: it parses the route strings `web.paths` reports
'   and decides for itself which route should win. Where the two disagree one
'   of them is wrong, and neither can be quietly wrong in the same direction.
'
' Both tiers print `ok` per property and a MISMATCH naming both sides.

load web

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

function h1(req)
    return { body: "1" }
end function
function h2(req)
    return { body: "2" }
end function
function h3(req)
    return { body: "3" }
end function
function h4(req)
    return { body: "4" }
end function
function h5(req)
    return { body: "5" }
end function

' ---------------------------------------------------------- an independent
' matcher and specificity rule, written from the definition rather than
' called out to the library.

function o_segments(path)
    if path = "/" then
        return []
    end if
    parts = split(path, "/")
    out = []
    n = count(parts)
    i = 1
    while i < n
        append(out, parts[i])
        i = i + 1
    end while
    return out
end function

' 2 static, 1 single capture, 0 greedy capture -- the whole rule.
function o_rank(seg)
    opens = starts_with(seg, "{")
    if not opens then
        return 2
    end if
    greedy = ends_with(seg, "...}")
    if greedy then
        return 0
    end if
    return 1
end function

function o_matches(pattern, path)
    ps = o_segments(pattern)
    rs = o_segments(path)
    pn = count(ps)
    rn = count(rs)
    i = 0
    while i < pn
        r = o_rank(ps[i])
        if r = 0 then
            return rn > i
        end if
        if rn <= i then
            return false
        end if
        if r = 2 then
            if ps[i] != rs[i] then
                return false
            end if
        else
            if len(rs[i]) = 0 then
                return false
            end if
        end if
        i = i + 1
    end while
    return pn = rn
end function

' Segments from `from` onward, rejoined. (`rest` drops only the first
' element, so it cannot express this.)
function o_tail(segs, from)
    out = []
    i = from
    n = count(segs)
    while i < n
        append(out, segs[i])
        i = i + 1
    end while
    return join(out, "/")
end function

' True when a is strictly more specific than b.
function o_beats(a, b)
    as_ = o_segments(a)
    bs = o_segments(b)
    an = count(as_)
    bn = count(bs)
    limit = an
    if bn < limit then
        limit = bn
    end if
    i = 0
    while i < limit
        ar = o_rank(as_[i])
        br = o_rank(bs[i])
        if ar != br then
            return ar > br
        end if
        i = i + 1
    end while
    return an > bn
end function

' ------------------------------------------------------------ the fixtures

set_a = [
    { method: "get", path: "/a",            handler: h1 },
    { method: "get", path: "/a/b",          handler: h2 },
    { method: "get", path: "/a/{id}",       handler: h3 },
    { method: "get", path: "/{top}/b",      handler: h4 },
    { method: "get", path: "/a/{rest...}",  handler: h5 }
]

set_b = [
    { method: "get", path: "/a/{rest...}",  handler: h5 },
    { method: "get", path: "/{top}/b",      handler: h4 },
    { method: "get", path: "/a/{id}",       handler: h3 },
    { method: "get", path: "/a/b",          handler: h2 },
    { method: "get", path: "/a",            handler: h1 }
]

set_c = [
    { method: "get", path: "/a/{id}",       handler: h3 },
    { method: "get", path: "/a",            handler: h1 },
    { method: "get", path: "/a/{rest...}",  handler: h5 },
    { method: "get", path: "/a/b",          handler: h2 },
    { method: "get", path: "/{top}/b",      handler: h4 }
]

set_d = [
    { method: "get", path: "/{top}/b",      handler: h4 },
    { method: "get", path: "/a/b",          handler: h2 },
    { method: "get", path: "/a",            handler: h1 },
    { method: "get", path: "/a/{rest...}",  handler: h5 },
    { method: "get", path: "/a/{id}",       handler: h3 }
]

table_a = web.routes(set_a)
table_b = web.routes(set_b)
table_c = web.routes(set_c)
table_d = web.routes(set_d)

' Every path of one, two and three segments over {a, b, x}, plus the root and
' a few shapes the alphabet cannot produce.
alphabet = ["a", "b", "x"]
probes = ["/"]
for each s1 in alphabet
    append(probes, "/" + s1)
    for each s2 in alphabet
        append(probes, "/" + s1 + "/" + s2)
        for each s3 in alphabet
            append(probes, "/" + s1 + "/" + s2 + "/" + s3)
        end for
    end for
end for
append(probes, "/a/")
append(probes, "//b")

print "-- probes: " + string(count(probes))

' ------------------------------------------------- tier 1: order-independence

disagreements = 0
for each p in probes
    ra = web.resolve(table_a, "GET", p)
    rb = web.resolve(table_b, "GET", p)
    rc = web.resolve(table_c, "GET", p)
    rd = web.resolve(table_d, "GET", p)

    label_a = "none"
    if ra.ok then
        label_a = ra.route.path
    end if
    label_b = "none"
    if rb.ok then
        label_b = rb.route.path
    end if
    label_c = "none"
    if rc.ok then
        label_c = rc.route.path
    end if
    label_d = "none"
    if rd.ok then
        label_d = rd.route.path
    end if

    if label_a != label_b or label_a != label_c or label_a != label_d then
        disagreements = disagreements + 1
        print "MISMATCH order-independence at " + p + ": " + label_a + " / " + label_b + " / " + label_c + " / " + label_d
    end if
end for
append(results, check("every probe resolves the same in all four declaration orders", 0, disagreements))

' ------------------------------------------------------------ tier 2: oracle

patterns = []
for each line in web.paths(table_a)
    append(patterns, mid(line, 4, len(line) - 4))
end for
append(results, check("the oracle reads five route patterns from web.paths", 5, count(patterns)))

wrong_winner = 0
missed_match = 0
false_match = 0

for each p in probes
    got = web.resolve(table_a, "GET", p)

    ' What the oracle says should happen, decided from the patterns alone.
    expect = "none"
    for each pat in patterns
        hit = o_matches(pat, p)
        if hit then
            if expect = "none" then
                expect = pat
            else
                better = o_beats(pat, expect)
                if better then
                    expect = pat
                end if
            end if
        end if
    end for

    actual = "none"
    if got.ok then
        actual = got.route.path
    end if

    if actual != expect then
        if expect = "none" then
            false_match = false_match + 1
            print "MISMATCH oracle at " + p + ": library matched " + actual + ", oracle says nothing matches"
        else
            if actual = "none" then
                missed_match = missed_match + 1
                print "MISMATCH oracle at " + p + ": library matched nothing, oracle says " + expect
            else
                wrong_winner = wrong_winner + 1
                print "MISMATCH oracle at " + p + ": library chose " + actual + ", oracle says " + expect
            end if
        end if
    end if
end for

append(results, check("the library matches nothing the oracle rejects", 0, false_match))
append(results, check("the library matches everything the oracle accepts", 0, missed_match))
append(results, check("and picks the same winner every time", 0, wrong_winner))

' ------------------------------------------------------- tier 3: the captures
' A capture's value must be the segment it stands for -- checked against the
' request path by position, not against what the router reported.

capture_wrong = 0
for each p in probes
    got = web.resolve(table_a, "GET", p)
    if got.ok then
        pat_segs = o_segments(got.route.path)
        req_segs = o_segments(p)
        i = 0
        while i < count(pat_segs)
            seg = pat_segs[i]
            r = o_rank(seg)
            if r = 1 then
                name = mid(seg, 1, len(seg) - 2)
                if got.params[name] != req_segs[i] then
                    capture_wrong = capture_wrong + 1
                    print "MISMATCH capture at " + p + ": " + name + " is '" + got.params[name] + "', the path segment is '" + req_segs[i] + "'"
                end if
            end if
            if r = 0 then
                name = mid(seg, 1, len(seg) - 5)
                tail = o_tail(req_segs, i)
                if got.params[name] != tail then
                    capture_wrong = capture_wrong + 1
                    print "MISMATCH greedy capture at " + p + ": " + name + " is '" + got.params[name] + "', the remainder is '" + tail + "'"
                end if
            end if
            i = i + 1
        end while
    end if
end for
append(results, check("every capture holds the segment it stands for", 0, capture_wrong))

bad = 0
for each verdict in results
    if not verdict then
        bad = bad + 1
    end if
end for

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
