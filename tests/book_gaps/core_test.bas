' The gBASIC books' last gap batch (gbasic-books/docs/volume-3/gap-ledger.md,
' G31-G65), the core items that need nothing outside gBASIC to check. Every
' answer is written by hand; each check names its gap. Run by
' tests/run_book_gaps.sh, which adds the oracles from outside (stat, a second
' process, `gbasic --version`).

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

function details_of(f, arg)
    on error goto next
    v = f(arg)
    e = error
    if e then
        return encode(e.details)
    end if
    return "no error"
end function

function usd_of(text)
    return money.of("USD", text)
end function

function usd_mod(text)
    return {USD}text
end function

function strict(text)
    on error goto next
    v = json_decode(text)
    e = error
    if e then
        return "refused: " + e.message
    end if
    return encode(v)
end function

' G62: an exit code is an exact integer, as a count is (G-SQL's integer
' column refused even 0)
r = process.run({ command: "sh", args: ["-c", "exit 3"] })
check("G62 exit code is exact", r.exit_code + 9007199254740993, 9007199254740996)
check("G62 a signal number is exact too", r.signal + 9007199254740993, 9007199254740993)

' G39: byte_find answers exact, as find does
check("G39 byte_find is exact", byte_find("abcdefgh", "c") + 9007199254740991, 9007199254740993)

' G56: money negates to money, in its currency
x = {USD}"12.34"
check("G56 unary minus on money", -x, {USD}"-12.34")
check("G56 keeps its currency", money.currency(-x), "USD")
check("G56 and is exact", -(-x), x)

' G41: the two construction refusals say which they are
' the success path too: valgrind found a leak there that only refusals exercised
check("G41 money.of still builds money", usd_of("12.34"), {USD}"12.34")
check("G41 too many places, money.of", details_of(usd_of, "1.1234567"), "{\"kind\":\"places\",\"allowed\":6,\"found\":7}")
check("G41 out of range, money.of", details_of(usd_of, "99999999999999"), "{\"kind\":\"range\"}")
check("G41 not a number", details_of(usd_of, "lots"), "{\"kind\":\"text\"}")
check("G41 the modifier says the same", details_of(usd_mod, "1.1234567"), "{\"kind\":\"places\",\"allowed\":6,\"found\":7}")

' G36: which gBASIC is running (the runner checks it against --version)
rt = runtime()
check("G36 runtime() has a version", len(rt.version) > 0, true)
check("G36 and a stdlib", has(rt, "stdlib"), true)
print "RUNTIME " + rt.version + " " + rt.build

' G64: is a pid alive, and is it still the same process
me = process.self()
check("G64 this process is alive", process.alive(me.pid), true)
info = process.info(me.pid)
check("G64 its start time is the one self() gave", info.started, me.started)
check("G64 and its command line is known", type(info.command), "array")
gone = process.info(999999)
check("G64 a pid nobody has is not alive", gone.alive, false)
check("G64 and has no start time", is_unknown(gone.started), true)

' G61: strict JSON, nested, and nothing else
check("G61 nested JSON reads", strict("{\"a\": [1, {\"b\": null}], \"c\": true}"), "{\"a\":[1,{\"b\":nothing}],\"c\":true}")
check("G61 gBASIC's own notation is refused", left(strict("{kind: \"repair\"}"), 32), "refused: json_decode: not JSON: ")
check("G61 a key written twice is refused", contains(strict("{\"a\": 1, \"a\": 2}"), "written twice"), true)
check("G61 text after the value is refused", contains(strict("[1] x"), "text after the JSON value"), true)
check("G61 a NUL is refused", contains(strict("[1]" + chr(0) + "z"), "a NUL byte"), true)

' G37: default takes a missing field
rec = { a: 1, inner: { b: 2 } }
check("G37 a missing field takes the fallback", default(rec.candidates, []), [])
check("G37 a nested missing field too", default(rec.inner.missing, "fallback"), "fallback")
check("G37 a present field is itself", default(rec.inner.b, 0), 2)

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
