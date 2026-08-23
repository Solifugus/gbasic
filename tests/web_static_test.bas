' PLAT-WEB-1 Gap B: `web.static` -- serving a file from under a root, and
' refusing everything else. Driven by tests/run_web_routes.sh, which builds the
' tree (including the symlinks, which gBASIC cannot create) and passes the root
' in GBASIC_WEB_STATIC_ROOT.
'
' THE POINT OF THIS FIXTURE is the pairs. A server that refuses everything is
' trivially "safe" and useless, so every refusal here sits next to a case that
' must still be SERVED: a symlink pointing out of the root is 403 while a
' symlink pointing inside it is 200; "../secret" is 403 while "sub/deep.txt" is
' 200. Over-blocking fails as loudly as under-blocking.
'
' Every check states its own expected answer and prints ok or a MISMATCH.

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

root = env("GBASIC_WEB_STATIC_ROOT")
if is_unknown(root) then
    print "GBASIC_WEB_STATIC_ROOT is not set"
end if

' Since PLAT-WEB-4 a served response carries `file:` (the canonical path the
' server will stream) instead of a slurped `body`, so content is verified by
' reading the file the response points at.
function served_text(r)
    if not has(r, "file") then
        return "(no file in response)"
    end if
    f(file) = r.file
    return read(f)
end function

print "-- what must be served"
r = web.static("index.html", root)
append(results, check("a file in the root is served", 200, r.status))
append(results, check("with its content", "<h1>ok</h1>", served_text(r)))
append(results, check("and a content type from its extension", "text/html; charset=utf-8", r.headers["content-type"]))

r = web.static("sub/deep.txt", root)
append(results, check("a file in a subdirectory is served", 200, r.status))
append(results, check("with its content", "deep", served_text(r)))

r = web.static("img.png", root)
append(results, check("a binary file survives the round trip", 8, byte_count(served_text(r))))
append(results, check("and is typed by extension", "image/png", r.headers["content-type"]))

r = web.static("data.bin", root)
append(results, check("an unknown extension is bytes, not a guess", "application/octet-stream", r.headers["content-type"]))

r = web.static("inside", root)
append(results, check("a symlink that stays inside the root is served", 200, r.status))
append(results, check("and resolves to its target", "deep", served_text(r)))

print ""
print "-- what must not be"
r = web.static("escape", root)
append(results, check("a symlink pointing out of the root is refused", 403, r.status))

r = web.static("../secret/key.txt", root)
append(results, check("a .. that reaches a real file is refused", 403, r.status))

r = web.static("sub/../../secret/key.txt", root)
append(results, check("and so is one that climbs out through a subdirectory", 403, r.status))

r = web.static("../pub-secret/x.txt", root)
append(results, check("a sibling sharing the root's name prefix is refused", 403, r.status))

r = web.static("sub", root)
append(results, check("a directory is not a listing", 404, r.status))

r = web.static("nothing-here.txt", root)
append(results, check("a missing file is 404", 404, r.status))

r = web.static("/etc/passwd", root)
append(results, check("an absolute path does not escape", 404, r.status))

r = web.static("", root)
append(results, check("an empty path is 404, not the root", 404, r.status))

r = web.static("index.html" + chr(0) + "/../../etc/passwd", root)
append(results, check("a NUL byte is refused as a value, not a crash", 404, r.status))

r = web.static(42, root)
append(results, check("a non-string is 404", 404, r.status))

r = web.static("index.html", root + "/no-such-root")
append(results, check("a root that does not exist is the server's fault, not the client's", 500, r.status))

bad = 0
for each verdict in results
    if not verdict then
        bad = bad + 1
    end if
end for
print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
