' The encode/decode DIALECT has exactly one promise -- an exact gBASIC-to-gBASIC
' round trip -- and it did not hold for the values IEEE arithmetic produces.
' `encode` wrote bare `nan` and `inf`; its own `decode` refused them. So a
' program could write a file it could not read back, with no diagnostic on
' either side of the gap. (DOGFOOD ledger item 2.)
'
' Not an exotic corner: multiplying two large numbers overflows to infinity
' quietly, and the next `encode` produces the unreadable file.
'
' `-inf` decoded FINE the whole time, by accident -- strtod happens to parse it
' and the dialect's number branch is entered on a leading '-'. One of three
' spellings working is the clearest evidence this was a bug and not a policy.
'
' The strict path (json_encode / json_encodable) is unaffected and must stay
' that way: RFC 8259 has no infinity, and `decode` in wire mode must not accept
' one either. See tests/run_json_strict.sh.

inf = number("inf")
ninf = number("-inf")
nan = number("nan")
overflow = number("1e308") * 10

print("-- what encode writes --")
print(encode({a = inf}))
print(encode({a = ninf}))
print(encode({a = nan}))
print(encode({a = overflow}))

print("-- what decode reads back --")
back = decode(encode({i = inf, n = ninf, q = nan}))
print(string(back.i))
print(string(back.n))
print(string(back.q))

print("-- nested, and beside the other dialect values --")
deep = decode(encode({
    xs = [inf, ninf, nan],
    empty = nothing,
    unset = unknown,
    finite = 0.1
}))
print(string(deep.xs[0]))
print(string(deep.xs[1]))
print(string(deep.xs[2]))
print(string(deep.empty))
print(string(deep.unset))
print(string(deep.finite))

print("-- all four spellings, because the formatter emits all four --")
' `-nan` is not a curiosity: number("-nan") produces it and the shared number
' formatter renders it, so a fix covering only inf/-inf/nan would have left a
' fourth spelling unreadable.
negnan = number("-nan")
print(encode({a = negnan}))
print(string(decode(encode({a = negnan})).a))

print("-- the property, stated --")
' Infinity compares equal to itself, so the round trip is assertable directly.
' NaN never equals NaN by IEEE rule, which is why its check goes through the
' rendering both sides already agree on.
print(string(decode(encode({v = inf})).v = inf))
print(string(decode(encode({v = ninf})).v = ninf))
print(string(string(decode(encode({v = nan})).v) = string(nan)))

print("-- the strict path is UNCHANGED --")
print(string(json_encodable(inf)))
print(string(json_encodable(nan)))
print(string(json_encodable({a = 1, b = "two"})))
