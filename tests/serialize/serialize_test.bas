' `serialize` VERSION 3 -- the portable value encoding
' (docs/gsql_storage_design.md §3.3, exact_number_design.md increment 4).
'
' SELF-CHECKING NOT GOLDEN, and forced: every defect a serializer can have is a
' PLAUSIBLE VALUE. 2^53 + 1 coming back as 2^53 prints as an ordinary number
' one digit out, which is exactly how it survived until now, and a golden would
' record it as expected.
'
' MODES: `check` (the default) runs every tier; `dump` prints one
' `name<TAB>hex` line per value for the independent decoder in
' tests/serialize/decode_v3.py; `write_reg PATH` and `read_reg PATH` are the two
' halves of the registered-currency tier, which needs two processes.

function check(what, got, want)
    tally.checks = tally.checks + 1
    if type(got) = type(want) and got = want then
        print "ok   " + what
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + what + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

function back(v)
    return deserialize(serialize(v))
end function

' A refusal is asked for by NAME and must raise the corruption message, never
' answer: a decoder that accepted a damaged payload answers a plausible value.
function refused(what, payload)
    on error goto next
    x = deserialize(payload)
    if error then
        check(what, error.message, "deserialize: corrupt or truncated serialized data")
    else
        check(what, "accepted: " + string(x), "deserialize: corrupt or truncated serialized data")
    end if
    return nothing
end function

function hexof(v)
    return hex_encode(serialize(v))
end function

' The values the decoder oracle is handed, by name. One list, so the oracle and
' the round-trip tier cannot drift onto different sets.
function battery()
    d {date}= "2026-06-25"
    t {datetime}= "2026-06-25 13:04:05"
    tm {time}= "07:08:09"
    m {USD}= 19.9512
    j {JPY}= 1200
    k {KWD}= -3.125
    return [
        { name: "zero", v: 0 },
        { name: "exact_big", v: 9007199254740993 },
        { name: "exact_min", v: -9223372036854775807 - 1 },
        { name: "exact_max", v: 9223372036854775807 },
        { name: "neg_exact", v: -42 },
        { name: "half", v: 1.5 },
        { name: "neg_zero", v: -0.0 },
        { name: "tiny", v: number("5e-324") },
        { name: "inf", v: decode("inf") },
        { name: "neg_inf", v: decode("-inf") },
        { name: "empty", v: "" },
        { name: "nul_utf8", v: "a" + chr(0) + "é☃" },
        { name: "long", v: repeat("x", 300) },
        { name: "yes", v: true },
        { name: "no", v: false },
        { name: "nothing", v: nothing },
        { name: "unknown", v: unknown },
        { name: "date", v: d },
        { name: "datetime", v: t },
        { name: "time", v: tm },
        { name: "span", v: 2 days 3 hours },
        { name: "usd", v: m },
        { name: "jpy", v: j },
        { name: "kwd", v: k },
        { name: "nested", v: { list: [1, [2.5, "x"], {k: "v"}], u: unknown } },
        { name: "file", v: {file}"/etc/hostname" },
        { name: "dir", v: {dir}"/tmp" }
    ]
end function

function echo_back(parent, v)
    send(parent, v)
end function

program main(args)
    tally = { checks: 0, mismatches: 0 }
    mode = "check"
    if len(args) > 0 then mode = args[0]

    if mode = "dump" then
        for each item in battery()
            print item.name + chr(9) + hexof(item.v)
        end for
        return
    end if
    if mode = "write_reg" then
        ' Registered in an order that gives XBB a DIFFERENT number from the one
        ' the reading process gives it.
        a = money.register("XAA", 2)
        b = money.register("XBB", 3)
        v {XBB}= 7.125
        write({file}args[1], hex_encode(serialize(v)))
        return
    end if
    if mode = "read_reg" then
        b = money.register("XBB", 3)
        v = deserialize(hex_decode(read({file}args[1])))
        w {XBB}= 7.125
        check("a registered currency is found by its CODE, not its number", v = w, true)
        check("and it renders as that code", string(v), string(w))
        print "ok   " + string(tally.checks) + " checks, " + string(tally.mismatches) + " mismatches"
        return
    end if

    print "-- THE LOAD-BEARING TIER: exactness crosses the boundary"
    ' Until v3 this was a NEGATIVE CONTROL in tests/exact/exact_test.bas: the
    ' payload carried only the double. The check is ARITHMETIC, because below
    ' and around 2^53 an exact and an inexact number RENDER the same.
    b = 9007199254740993
    check("2^53 + 1 survives", back(b) = b, true)
    check("and still adds exactly", back(b) + 1, 9007199254740994)
    check("the int64 floor survives", back(-9223372036854775807 - 1) + 1, -9223372036854775807)
    check("the int64 ceiling survives", back(9223372036854775807) - 1, 9223372036854775806)
    ' THE CONTROL: an inexact number must STAY inexact, or "exactness travels"
    ' is satisfied by calling every integral double exact -- the same defect
    ' pointing the other way. 2^53 as a double plus 1 is still 2^53.
    big = 9007199254740992 * 1.0
    check("an inexact integer stays inexact", back(big) + 1 = big, true)
    check("a fraction is a fraction", back(1.5), 1.5)
    check("-0 keeps its sign", string(back(-0.0)), "-0")
    check("the smallest subnormal survives", back(number("5e-324")) > 0, true)
    check("+inf survives", string(back(decode("inf"))), "inf")
    check("-inf survives", string(back(decode("-inf"))), "-inf")
    check("nan survives as nan", string(back(decode("nan"))), "nan")

    print ""
    print "-- every kind round-trips, compared by KIND as well as value"
    for each item in battery()
        check("round trip " + item.name, back(item.v), item.v)
    end for
    t {datetime}= "2026-06-25 13:04:05"
    check("a datetime keeps its precision", back(t).second, 5)
    d {date}= "2026-06-25"
    check("a date is not widened to a datetime", back(d) = t, false)

    print ""
    print "-- money keeps its storage scale, not only its display"
    ' 19.9512 displays as 19.95, so only arithmetic separates a value kept at
    ' the storage scale from one rounded at the door (run_money's lesson).
    m {USD}= 19.9512
    check("guard digits survive", back(m) * 10000, m * 10000)
    j {JPY}= 1200
    check("a JPY still refuses USD after the trip", money_mix(back(j), m), "cannot add money in different currencies (JPY and USD)")

    print ""
    print "-- THE BYTES ARE PINNED: the layout is the format, on every host"
    ' A little-endian host writes these with memcpy too, so this tier is what
    ' says a big-endian one must write the SAME bytes; the independent decoder
    ' is the other half. Header `gBS` + version 03.
    check("header and version", left(hexof(nothing), 8), "67425303")
    check("an exact int is flag 1 + int64 LE", hexof(9007199254740993), "674253030401" + "0100000000002000")
    check("an inexact number is flag 0 + double bits LE", hexof(1.5), "674253030400" + "000000000000f83f")
    check("a string is a LEB128 length", hexof("hi"), "6742530305" + "02" + "6869")
    check("a length past 127 takes two LEB128 bytes", left(hexof(repeat("x", 300)), 14), "6742530305" + "ac02")
    check("an array count is LEB128", hexof([true]), "6742530306" + "01" + "0301")
    check("a record field name is LEB128 + bytes", hexof({a: nothing}), "6742530307" + "01" + "0161" + "01")
    m2 {USD}= 19.95
    check("money: units, exponent, numeric, then the code", hexof(m2), "674253030a" + "b069300100000000" + "02" + "4803" + "03555344")
    check("a date is six int32 LE then two flag bytes", hexof(d), "6742530308" + "ea070000" + "06000000" + "19000000" + "00000000" + "00000000" + "00000000" + "00" + "03")
    check("a duration is seven int32 LE", hexof(2 days 3 hours), "6742530309" + "00000000" + "00000000" + "00000000" + "02000000" + "03000000" + "00000000" + "00000000")

    print ""
    print "-- OLD PAYLOADS STILL READ: a v2 file written by the binary before v3"
    ' Committed as hex, written by the pre-v3 build (commit 2f77bb9) rather than
    ' constructed by hand, because a fixture this binary wrote would test v3
    ' against itself. v1 has its own tier in tests/money_serialize_test.bas.
    old = deserialize(hex_decode(trim(read({file}"tests/serialize/v2_payload.hex"))))
    check("v2: an integer", old.n, 42)
    check("v2: a fraction", old.f, 2.5)
    check("v2: a negative", old.neg, -7)
    check("v2: a string with a NUL and UTF-8", old.s, "a" + chr(0) + "é")
    check("v2: a boolean", old.yes, true)
    check("v2: nothing", old.none, nothing)
    check("v2: unknown", is_unknown(old.u), true)
    check("v2: a date", old.d, d)
    check("v2: a datetime", old.t, t)
    check("v2: a duration", old.span, 2 days 3 hours)
    check("v2: money at its storage scale", old.m * 10000, m * 10000)
    check("v2: JPY", old.j, j)
    check("v2: nesting", old.list[2].k, "v")
    check("v2: a file", old.p, {file}"/etc/hostname")
    check("v2: a dir", old.q, {dir}"/tmp")

    print ""
    print "-- CORRUPTION IS REFUSED, NEVER ANSWERED"
    ' EVERY PREFIX of a payload holding every kind must be refused: a reader
    ' that accepts a short payload answers a plausible value with a field
    ' missing. With the control that the whole payload is accepted, or "every
    ' prefix is refused" is satisfied by refusing everything.
    whole = serialize(battery())
    check("the whole payload is accepted (the control)", deserialize(whole), battery())
    short = 0
    n = byte_count(whole)
    i = 4
    while i < n
        if prefix_refused(left_bytes(whole, i)) then short = short + 1
        i = i + 1
    end while
    check("every one of the " + string(n - 4) + " short prefixes is refused", short, n - 4)
    refused("trailing bytes", whole + chr(0))
    refused("an exactness flag that is neither 0 nor 1", hex_decode("674253030402" + "0000000000000000"))
    refused("a LEB128 length running past ten bytes", hex_decode("6742530305" + "ffffffffffffffffffff01"))
    ' 2^64 + 2 in ten LEB128 bytes. Shifted into a uint64 it WRAPS to 2, and the
    ' two bytes after it are there -- so a reader without the tenth-byte check
    ' answers "hi", a plausible string, rather than finding anything too long.
    refused("a LEB128 length that wraps past 64 bits", hex_decode("6742530305" + "82808080808080808002" + "6869"))
    refused("a length longer than the payload", hex_decode("6742530305" + "05" + "6869"))
    refused("a date precision that is no precision", hex_decode("6742530308" + repeat("00", 24) + "00" + "09"))
    refused("a currency code too long to be one", hex_decode("674253030a" + repeat("00", 8) + "02" + "4803" + "09" + repeat("41", 9)))
    check("a version from the future is refused", refusal(hex_decode("6742530400")), "deserialize: not a valid serialized value")

    print ""
    print "-- the actor wire uses the same encoding"
    me = self()
    w = spawn echo_back(me, { big: 9007199254740993, m: m })
    r = receive()
    check("an exact integer crosses spawn and send", r.big + 1, 9007199254740994)
    check("and money keeps its guard digits", r.m * 10000, m * 10000)

    print "ok   " + string(tally.checks) + " checks, " + string(tally.mismatches) + " mismatches"
end program

function money_mix(a, b)
    on error goto next
    x = a + b
    if error then return error.message
    return "no error"
end function

function refusal(p)
    on error goto next
    x = deserialize(p)
    if error then return error.message
    return "accepted"
end function

function prefix_refused(p)
    on error goto next
    x = deserialize(p)
    if error then return true
    return false
end function

' The first `count` BYTES of a string, which `left` cannot give: it counts
' codepoints, and a payload is bytes.
function left_bytes(s, count)
    return hex_decode(left(hex_encode(s), count * 2))
end function
