' PLAT-JSON: try_decode(text) -- decode that reports failure as a VALUE.
'
' `decode` raises, and gBASIC cannot catch a raise (docs/ai/ERRORS.md), so any
' caller reading a file it did not write has had to pre-validate in gBASIC first.
' That pre-validation is quadratic (every per-character scan is, because `mid` is
' O(i) on codepoint-indexed strings), which is what this builtin exists to remove.
'
' try_decode shares decode's parser -- there is no second JSON implementation -- and
' returns:
'   { ok: true,  value: <decoded>, message: "",    offset: 0, line: 0, column: 0 }
'   { ok: false, value: nothing,   message: <why>, offset: N, line: N, column: N }

function show(label, r)
    line = label + " ok=" + r.ok
    if r.ok then
        ' `unknown` is part of the dialect decode accepts but has no JSON form, so
        ' it is reported by type rather than re-encoded.
        if json_encodable(r.value) then
            line = line + " value=" + json_encode(r.value)
        else
            line = line + " value=(" + type(r.value) + ")"
        end if
    else
        line = line + " message=<" + r.message + "> at " + r.line + ":" + r.column + " offset=" + r.offset
    end if
    print line
    return nothing
end function

print "== valid: every shape the dialect accepts =="
show("object     ", try_decode("{\"a\":1,\"b\":\"two\"}"))
show("array      ", try_decode("[1,2,3]"))
show("nested     ", try_decode("{\"a\":[1,{\"b\":[true,false]}]}"))
show("string     ", try_decode("\"hello\""))
show("escapes    ", try_decode("\"a\\nb\\tc\\\"d\\\\e\\u0041\""))
show("number int ", try_decode("42"))
show("number neg ", try_decode("-7"))
show("number frac", try_decode("3.5"))
show("number exp ", try_decode("1e3"))
show("true       ", try_decode("true"))
show("false      ", try_decode("false"))
show("null       ", try_decode("null"))
show("empty obj  ", try_decode("{}"))
show("empty arr  ", try_decode("[]"))
show("ws around  ", try_decode("  \n\t {\"a\":1}  \n "))

print ""
print "== the gBASIC dialect decode accepts is accepted identically =="
' `decode` deliberately accepts bare nothing/unknown (the historical dialect);
' try_decode shares that parser, so it must accept exactly the same set.
show("nothing    ", try_decode("nothing"))
show("unknown    ", try_decode("unknown"))

print ""
print "== malformed: every class, reported as a value =="
show("truncated obj  ", try_decode("{\"a\":1"))
show("truncated arr  ", try_decode("[1,2,"))
show("missing value  ", try_decode("{\"a\":}"))
show("unterminated   ", try_decode("\"no closing quote"))
show("bad escape     ", try_decode("\"a\\qb\""))
show("bad unicode    ", try_decode("\"a\\u00zz\""))
show("empty input    ", try_decode(""))
show("whitespace only", try_decode("   \n  "))
show("trailing text  ", try_decode("{\"a\":1}junk"))
show("missing colon  ", try_decode("{\"a\" 1}"))
show("bare word      ", try_decode("tru"))
show("lone comma     ", try_decode("[,]"))
show("unclosed nest  ", try_decode("[[[[[["))
show("object as key  ", try_decode("{1:2}"))

print ""
print "== round-trip against json_encode =="
' Whatever json_encode emits, try_decode must read back, for every encodable shape.
samples = [
    { a: 1, b: "two", c: true, d: nothing, e: [1, 2], f: { g: "h" } },
    { empty_obj: {}, empty_arr: [] },
    { unicode: "héllo → wörld ✓", escapes: "quote\" back\\ nl\n tab\t" },
    { nums: [0, -1, 3.5, 1000000, 0.001] }
]
i = 0
for each s in samples
    text = json_encode(s)
    r = try_decode(text)
    again = ""
    if r.ok then
        again = json_encode(r.value)
    end if
    print "sample " + i + " ok=" + r.ok + " stable=" + (again = text)
    i = i + 1
end for

print ""
print "== bytes are preserved, not re-encoded =="
r = try_decode("{\"k\":\"h\\u00e9llo\"}")
print "ok=" + r.ok + " bytes=" + byte_count(r.value.k) + " chars=" + len(r.value.k)

print ""
print "== a large valid document =="
' 40 000 array elements: comfortably past anything the old gBASIC validator could
' read in reasonable time, and read here in well under a second.
big = "[" + repeat("1,", 39999) + "1]"
r = try_decode(big)
print "big_bytes=" + byte_count(big) + " ok=" + r.ok + " elements=" + count(r.value)

' A large OBJECT too -- the store shape that actually matters.
parts = []
i = 0
while i < 4000
    parts = append(parts, "\"k" + i + "\":\"value-" + i + "\"")
    i = i + 1
end while
bigobj = "{" + join(parts, ",") + "}"
r = try_decode(bigobj)
print "bigobj_bytes=" + byte_count(bigobj) + " ok=" + r.ok + " keys=" + count(keys(r.value))

print ""
print "== deep nesting is bounded, not fatal =="
' Unbounded recursion in the parser used to take the whole interpreter down with a
' segfault at around 45 000 levels on an 8 MB stack. A depth limit turns that into
' an ordinary reported failure -- a crash is the one outcome a non-raising decode
' must never have.
ok_depth = 500
d = "[" + repeat("[", ok_depth - 1) + repeat("]", ok_depth - 1) + "]"
r = try_decode(d)
print "depth " + ok_depth + " ok=" + r.ok

for each deep in [20000, 100000]
    d = repeat("[", deep) + repeat("]", deep)
    r = try_decode(d)
    print "depth " + deep + " ok=" + r.ok + " message=<" + r.message + ">"
end for

' Unbalanced AND deep: the limit must trip before the parser runs off the end.
d = repeat("[", 100000)
r = try_decode(d)
print "deep unclosed ok=" + r.ok + " message=<" + r.message + ">"
