' crypto.json_decode reads JWT payloads, which are ATTACKER-SUPPLIED. Its whole
' contract is that out-of-domain input comes back `unknown` -- and a raise is not
' a refusal, it is a denial of service, because gBASIC cannot catch one.
'
' It did raise. `_json_parse_value` dispatches on the first character and falls
' through to a number for everything that is not `"`, `t`, `f` or `n`, so
' `{"a":inf}` reached number("") and killed the program. The old scanner also
' collected "number-ish characters" rather than scanning the grammar, so `+1`,
' `1.2.3`, `1e` and a bare `-` all reached number() too. The door was as wide as
' the alphabet.
'
' Fixed in 0.1.0-rc8 by scanning RFC 8259's number grammar and refusing anything
' that is not one. This fixture is the proof, and it is written as a LOOP over
' hostile inputs rather than a transcript: what matters is that NONE of them
' escapes, so adding a case is one line and the assertion does not move.
program main(args)
    load crypto from "../stdlib/crypto.bas"

    ' Refusal, not a raise, for every one of these.
    hostile = [
        "{\"a\":inf}", "{\"a\":nan}", "{\"a\":-inf}", "{\"a\":Infinity}",
        "{\"a\":}", "{\"a\":]", "{\"a\":x}", "{\"a\":+1}", "{\"a\":-}",
        "{\"a\":.}", "{\"a\":1.}", "{\"a\":1e}", "{\"a\":1e+}", "{\"a\":1.2.3}",
        "{\"a\":01}", "{\"a\":--5}", "{\"a\":0x10}", "{\"a\":1_000}",
        "{\"a\":\"x}", "{\"a\"}", "{\"a\":1,}", "{", "}", "", "   ",
        "[1,2]", "null", "{\"a\":tru}", "{\"a\":fals}", "{\"a\":nul}",
        "{\"a\":nothing}", "{\"a\":unknown}"
    ]
    escaped = 0
    i = 0
    while i < len(hostile)
        r = crypto.json_decode(hostile[i])
        if not is_unknown(r) then
            escaped = escaped + 1
            print "ESCAPED: " + hostile[i] + " -> " + string(r)
        end if
        i = i + 1
    end while
    print "hostile inputs: " + string(len(hostile)) + ", escaped: " + string(escaped)

    ' Magnitudes no double can hold are syntactically valid JSON and convert to
    ' infinity WITHOUT raising, so they would smuggle a non-finite value into a
    ' token payload through a door the grammar leaves open. Refused separately.
    print "1e400:  " + string(is_unknown(crypto.json_decode("{\"a\":1e400}")))
    print "-1e400: " + string(is_unknown(crypto.json_decode("{\"a\":-1e400}")))

    ' Tightening the scanner must not have cost any legal number form.
    ok = crypto.json_decode("{\"z\":0,\"n\":-1.5,\"e\":2e3,\"m\":-0.5E-2,\"b\":1234567890,\"s\":\"t\",\"t\":true,\"f\":false,\"u\":null}")
    print "zero:     " + string(ok.z)
    print "negfrac:  " + string(ok.n)
    print "exp:      " + string(ok.e)
    print "signedexp:" + string(ok.m)
    print "big:      " + string(ok.b)
    print "string:   " + ok.s
    print "true:     " + string(ok.t)
    print "false:    " + string(ok.f)
    print "null:     " + string(is_unknown(ok.u))
end program
