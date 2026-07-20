' NAP-2: multiple OUT parameters + a real return, and string/scalar/nullable outs.
' GLib.uri_split(uri_ref, flags, OUT scheme, OUT userinfo, OUT host, OUT port,
'                OUT path, OUT query, OUT fragment) returns a gboolean.
'
' Seven out-params of mixed kinds: five transfer-full utf8 strings (scheme,
' userinfo, host, path, query, fragment) and one gint (port). With multiple outs
' the result is a record keyed by each out-param's introspected name, in addition
' to `result` for the bool return. Ordering is by name, so it is unambiguous.
load gi
gi.require("GLib", "2.0")

r = gi.invoke("GLib.uri_split", "https://bob@example.com:8080/p?q=1#frag", 0)
print r.result
print r.scheme
print r.userinfo
print r.host
print r.port
print r.path
print r.query
print r.fragment

' Nullable outs: a URI with no userinfo/query/fragment leaves those out-params NULL,
' which surfaces as `nothing` (not an empty string).
r2 = gi.invoke("GLib.uri_split", "https://example.com/x", 0)
print r2.host
if r2.userinfo = nothing then
    print "userinfo-nothing"
end if
if r2.fragment = nothing then
    print "fragment-nothing"
end if
