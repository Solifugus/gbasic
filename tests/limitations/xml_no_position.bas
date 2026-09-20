' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' CONTROL for DOGFOOD.md's STRUCK entry -3 (`xml.parse` gives a node no
' POSITION), closed 2026-09-20. It was a negative control asserting the
' limitation still held; a struck bullet gets the OPPOSITE check, because a
' struck bullet with nothing behind it is just a deleted one.
'
' THE DEFAULT STILL CARRIES NO POSITION, AND THAT IS NOT THE LIMITATION -- it is
' the design. Positions are opt-in, so the naive probe (parse and look for a
' `line`) reports exactly what it reported before the fix, and would keep this
' entry open forever. What must be asserted is that ASKING WORKS.
load xml

program main( args )
    text = "<a>" + chr(10) + "  <b>text</b>" + chr(10) + "</a>"

    plain = keys(xml.find(xml.parse(text), "b"))
    missing = []
    for each k in [ "line", "byte_start", "byte_end" ]
        if not contains(keys(xml.find(xml.parse(text, { positions: true }), "b")), k) then
            append(missing, k)
        end if
    end for

    if count(missing) > 0 then
        print "REGRESSED: asking for positions did not yield " + join(missing, ",")
        return
    end if

    ' The range must cut the element out of the source, not merely be a number.
    b = xml.find(xml.parse(text, { positions: true }), "b")
    cut = byte_slice(text, b.byte_start, b.byte_end - b.byte_start)
    if not (cut = "<b>text</b>") then
        print "REGRESSED: the byte range cut [" + cut + "] rather than the element"
        return
    end if

    ' And the opt-in half: a parse that did not ask is unchanged.
    if contains(plain, "line") then
        print "REGRESSED: a default parse now carries a position; it is opt-in"
        return
    end if

    print "FIXED: positions yield line/byte_start/byte_end and cut [" + cut + "]; default carries " + join(plain, ",")
end program
