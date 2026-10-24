' The ORACLE for G-SQL's key encoder (src/gsql/key.c, tests/run_gsql_key.sh).
' gBASIC writes every value in `serialize` version 3 and its OWN answer for every
' pair it will order, and tests/gsql/key_check.c requires memcmp on the keys to
' give the same answer. Nothing the key layout claims is accepted on reasoning.
'
' OUTPUT, one record per line:
'   V <index> <hex>          a value (serialize header included)
'   C <i> <j> <answer>       -1, 0 or 1, or x when gBASIC refuses to order them
'   N <index>                a NaN, which gBASIC gives no order (asserted in C)
'   R <hex>                  a value with no index order, which must be refused
'   P <hex> <key hex>        a value and the key bytes the layout says it has
'
' WHICH PAIRS ARE COMPARED IS gBASIC's DECISION: two values of different
' `type()` are never asked (a column holds one kind, and `0 = false` is a
' coercion an index has no business modelling), and a pair gBASIC refuses -- two
' currencies -- is written as x. An absence against a value is decided by gBASIC's
' own `sort`, which is where the absence ruling lives.

function absent(v)
    return type(v) = "unknown" or type(v) = "nothing"
end function

function is_nan(v)
    return type(v) = "number" and (string(v) = "nan" or string(v) = "-nan")
end function

function order(a, b)
    if absent(a) and absent(b) then return "0"
    if absent(a) or absent(b) then
        s = sort([a, b])
        if absent(s[1]) then
            if absent(a) then return "1"
            return "-1"
        end if
        if absent(a) then return "-1"
        return "1"
    end if
    if type(a) != type(b) then return ""
    on error goto next
    lt = a < b
    if error then return "x"
    eq = a = b
    gt = b < a
    if lt then return "-1"
    if eq then return "0"
    if gt then return "1"
    return "?"
end function

function battery()
    out = []
    ' --- numbers: the exactness edge, subnormals, signed zero, infinities
    for each n in [0, -0.0, 1, 5, 5.0, -5, -5.0, 0.5, 1.5, -1.5, 0.1, 0.2, 0.1 + 0.2, 0.3,
                   9007199254740991, 9007199254740992, 9007199254740992 * 1.0,
                   9007199254740993, -9007199254740993, -9007199254740992 * 1.0,
                   9223372036854775807, -9223372036854775807 - 1,
                   9223372036854775807 * 1.0, (-9223372036854775807 - 1) * 1.0,
                   number("1e308"), number("-1e308"), number("5e-324"), number("-5e-324"),
                   number("2.2250738585072014e-308"), number("2.225073858507201e-308"),
                   number("1e-300"), decode("inf"), decode("-inf"),
                   123456789.125, 123456789, 4503599627370496.5, 4503599627370497]
        append(out, n)
    end for
    ' A deterministic spread, so the battery is not only edges.
    i = 1
    x = 7
    while i <= 40
        x = mod(x * 1103 + 12345, 65521)
        append(out, (x - 32760) * 0.37)
        append(out, x - 32760)
        i = i + 1
    end while
    ' --- strings: byte order, the escaped NUL, and prefixes
    for each s in ["", "a", "a" + chr(0), "a" + chr(0) + chr(0), "a" + chr(0) + "b", "ab",
                   "b", "B", "Z", "é", "e", chr(0), chr(0) + "a", "ÿ", "☃", "abc", "abd"]
        append(out, s)
    end for
    append(out, true)
    append(out, false)
    ' --- temporal: a date, a datetime at its midnight, precisions, times
    d1 {date}= "2026-06-25"
    d2 {date}= "2026-06-26"
    d0 {date}= "1999-12-31"
    t1 {datetime}= "2026-06-25 00:00:00"
    t2 {datetime}= "2026-06-25 13:04:05"
    t3 {datetime}= "2026-06-25 13:04:06"
    t4 {datetime}= "2026-06-25 13:04"
    m1 {time}= "07:08:09"
    m2 {time}= "07:08"
    m3 {time}= "23:59:59"
    for each t in [d1, d2, d0, t1, t2, t3, t4, m1, m2, m3]
        append(out, t)
    end for
    ' --- money: guard digits, sign, and more than one currency
    for each text in ["0", "19.95", "19.9512", "19.9500", "-3", "-3.0001", "1000000", "-1000000"]
        u {USD}= text
        append(out, u)
    end for
    j {JPY}= 1200
    k {KWD}= -3.125
    e {EUR}= 19.95
    append(out, j)
    append(out, k)
    append(out, e)
    ' --- absence
    append(out, unknown)
    append(out, nothing)
    return out
end function

program main()
    vs = battery()
    for each v, i in vs
        print "V " + string(i) + " " + hex_encode(serialize(v))
        if is_nan(v) then print "N " + string(i)
    end for
    nan = decode("nan")
    append(vs, nan)
    print "V " + string(len(vs) - 1) + " " + hex_encode(serialize(nan))
    print "N " + string(len(vs) - 1)
    for each a, i in vs
        for each b, k in vs
            if not is_nan(a) and not is_nan(b) then
                r = order(a, b)
                if r != "" then print "C " + string(i) + " " + string(k) + " " + r
            end if
        end for
    end for
    ' PINNED KEYS, worked out by hand from §3.2 rather than read off the
    ' encoder. Keys are on disk, so their bytes are a format and not only an
    ' order: an encoding that keeps every comparison and moves a constant (a
    ' subnormal's exponent one out, say) passes every pair above and still
    ' cannot read an index the right one wrote.
    usd {USD}= 19.95
    day {date}= "2026-06-25"
    pins = [
        { v: 5, key: "24" + "044e" + "4000000000000000" },
        { v: 5.0, key: "24" + "044e" + "4000000000000000" },
        { v: -1.5, key: "22" + "fbb3" + "7fffffffffffffff" },
        { v: number("5e-324"), key: "24" + "001a" + "0000000000000000" },
        { v: 0, key: "23" },
        { v: -0.0, key: "23" },
        { v: decode("inf"), key: "25" },
        { v: "a" + chr(0), key: "30" + "61" + "00ff" + "0000" },
        { v: true, key: "1001" },
        { v: unknown, key: "f0" },
        { v: nothing, key: "f0" },
        { v: usd, key: "41" + "555344" + "00" + "80000000013069b0" },
        { v: day, key: "52" + "00" + "800007ea" + "80000006" + "80000019" + "80000000" + "80000000" + "80000000" + "03" }
    ]
    for each p in pins
        print "P " + hex_encode(serialize(p.v)) + " " + p.key
    end for
    for each v in [2 days, [1, 2], { a: 1 }, {file}"/tmp", {dir}"/tmp"]
        print "R " + hex_encode(serialize(v))
    end for
end program
