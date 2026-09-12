' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A NACHA-shaped fixed-width file, for the Phase 0 provenance measurement
' (docs/financial_adapters_design.md §21).
'
' FIXED WIDTH IS THE RIGHT SHAPE TO MEASURE ON: it is the representation whose
' provenance is most expensive to hold, because every field has an exact byte
' offset and length of its own -- §4's FixedWidthLocation. A delimited or JSON
' source carries LESS location than this, so a number measured here is an upper
' bound rather than a best case.
'
' The layout is the real NACHA entry detail record (type 6), 94 bytes, 11
' fields. The design's §21 estimate says "100,000 records at fifteen fields" --
' 11 is what the format actually has, so the location count at 100k records is
' 1.1M rather than 1.5M, the same order and measured rather than assumed.
'
' The CONTENT is fabricated. Nothing here needs a real customer file: the
' measurement is about the size of the provenance structure, which depends on
' the record and field COUNT and not on what the fields say.

program main( args )
    n = 1000
    if count(args) > 0 then
        n = number(args[0])
    end if
    out = ""
    lines = []
    ' File header (type 1) -- present so the file is shaped like a real one
    append(lines, _pad("101 021000021 1234567890" + _now_stamp(), 94))
    append(lines, _pad("5220ACME PAYROLL" + repeat(" ", 24) + "1234567890PPDPAYROLL", 94))
    i = 0
    while i < n
        append(lines, _entry(i))
        i = i + 1
    end while
    append(lines, _pad("82200000" + _right(string(n), 6, "0"), 94))
    append(lines, _pad("9999999", 94))
    f {file}= "nacha.txt"
    if count(args) > 1 then
        f {file}= args[1]
    end if
    write(f, join(lines, chr(10)) + chr(10))
    print "records " + string(n) + " bytes " + string(len(join(lines, chr(10))) + 1)
end program

function _entry(i)
    ' 1 type, 2 txn code, 8 rdfi, 1 check digit, 17 account, 10 amount,
    ' 15 individual id, 22 individual name, 2 discretionary, 1 addenda,
    ' 15 trace  =  94 bytes, 11 fields
    s = "6"
    s = s + "22"
    s = s + "02100002"
    s = s + "1"
    s = s + _pad("ACCT" + _right(string(i), 9, "0"), 17)
    s = s + _right(string(125000 + (i * 37)), 10, "0")
    s = s + _pad("EMP" + _right(string(i), 8, "0"), 15)
    s = s + _pad("EMPLOYEE " + _right(string(i), 6, "0"), 22)
    s = s + "  "
    s = s + "0"
    s = s + "021000021" + _right(string(i + 1), 6, "0")
    return s
end function

function _pad(s, width)
    if len(s) >= width then
        return left(s, width)
    end if
    return s + repeat(" ", width - len(s))
end function

function _right(s, width, fill)
    if len(s) >= width then
        return right(s, width)
    end if
    return repeat(fill, width - len(s)) + s
end function

function _now_stamp()
    return "0000000000000000000000000000000000000000000000"
end function
