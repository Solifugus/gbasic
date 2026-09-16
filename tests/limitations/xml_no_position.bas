' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' NEGATIVE CONTROL for DOGFOOD.md's open entry -3: `xml.parse` gives a node no
' POSITION. This asserts the limitation STILL HOLDS and is meant to GO RED WHEN
' IT IS FIXED, naming the entry to strike -- the same shape run_limitations.sh
' uses for the accepted-limitations ledger, applied to the "worth fixing" list
' because a to-do that has quietly come true is exactly as misleading as an
' accepted limitation that has.
load xml

program main( args )
    doc = xml.parse("<a><b>text</b></a>")
    b = xml.find(doc, "b")
    fields = keys(b)
    carries = []
    for each k in [ "line", "byte_offset", "byte_length", "position", "column" ]
        if contains(fields, k) then
            append(carries, k)
        end if
    end for
    if count(carries) = 0 then
        print "HOLDS: an xml.parse node carries " + join(fields, ",") + " and no position"
    else
        print "FIXED: an xml.parse node now carries " + join(carries, ",")
    end if
end program
