' Strict JSON serialization — json_encode / json_encodable.
'
' gBASIC has TWO serializers with different jobs, and this pins the boundary:
'   * encode/decode — the historical gBASIC dialect, which spells the empty values
'     `nothing`/`unknown` so a value survives a gBASIC round-trip. NOT valid JSON.
'   * json_encode   — RFC 8259 for external interchange (HTTP APIs, LLM providers).
'     `nothing` becomes null and anything with no faithful JSON form is REFUSED
'     rather than emitted as an invented token.
'
' Every JSON line printed here is additionally parsed by a real, independent JSON
' parser in tests/run_json_strict.sh — gBASIC's own `decode` is deliberately
' permissive (it accepts the dialect) and so cannot prove standards compliance.
program main(args)
    ' ---- scalars ----------------------------------------------------------
    print(json_encode(nothing))
    print(json_encode(true))
    print(json_encode(false))
    print(json_encode(0))
    print(json_encode(42))
    print(json_encode(-17))
    print(json_encode(1.5))
    print(json_encode(""))
    print(json_encode("plain"))

    ' ---- escaping ---------------------------------------------------------
    print(json_encode("quote \" backslash \\ slash /"))
    print(json_encode("tab\there\nnewline"))
    print(json_encode("ctrl:" + chr(1) + chr(31)))
    print(json_encode("unicode: caf\u{e9} \u{1F600}"))

    ' ---- containers -------------------------------------------------------
    print(json_encode([]))
    print(json_encode({}))
    print(json_encode([1, 2, 3]))
    print(json_encode(["a", true, nothing]))
    print(json_encode({ a: 1, b: "two", c: false }))

    ' ---- nothing in every position ---------------------------------------
    print(json_encode(nothing))
    print(json_encode([nothing]))
    print(json_encode({ v: nothing }))
    print(json_encode({ a: [ { b: nothing } ] }))

    ' ---- a representative HTTP API body ----------------------------------
    payload = {
        name: "test",
        optional: nothing,
        items: [1, 2, 3],
        nested: { enabled: true, note: "caf\u{e9}" }
    }
    print(json_encode(payload))

    ' ---- json_encodable preflight ----------------------------------------
    ' A raising serializer cannot be caught from a library (on error resume next
    ' unwinds past the callee), so callers preflight instead.
    print("encodable {a:1}      : " + string(json_encodable({ a: 1 })))
    print("encodable nested ok  : " + string(json_encodable({ a: [1, { b: nothing }] })))
    print("encodable unknown    : " + string(json_encodable(unknown)))
    print("encodable {a:unknown}: " + string(json_encodable({ a: unknown })))
    print("encodable [unknown]  : " + string(json_encodable([unknown])))
    print("encodable nan        : " + string(json_encodable(number("nan"))))
    print("encodable inf        : " + string(json_encodable(number("inf"))))
    print("encodable date       : " + string(json_encodable(now())))
    print("encodable function   : " + string(json_encodable(helper)))
    print("encodable nothing    : " + string(json_encodable(nothing)))

    ' ---- the native dialect is UNCHANGED ---------------------------------
    print("encode nothing : " + encode(nothing))
    print("encode unknown : " + encode(unknown))
    print("encode record  : " + encode({ a: nothing, b: unknown }))
    print("decode dialect : " + string(decode("[nothing,unknown]")[0]))
    print("string array   : " + string([1, unknown, nothing]))
end program

function helper(a)
    return a
end function
