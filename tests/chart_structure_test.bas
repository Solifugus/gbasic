' The chart STRUCTURE tier (chart_design.md §9): every rendered chart is fed
' back through OUR OWN xml.parse — a second pair of eyes that is not the
' renderer agreeing with itself. Asserts: well-formed (parse raises on the
' malformed, incl. any escaping bug the hostile title would cause), every
' circle inside the viewBox, one path per line series, and the element census.
'
' The walkers RETURN their answers: arrays are value-semantic in gBASIC, so
' appending to an argument mutates a local copy the caller never sees.

function count_of(node, name)
    if not is_record(node) then return 0
    n = 0
    if node["name"] = name then n = 1
    for each c in node["children"]
        n = n + count_of(c, name)
    end for
    return n
end function

' 1 when every <circle> in the subtree sits inside w x h, else 0.
function circles_inside(node, w, h)
    if not is_record(node) then return 1
    if node["name"] = "circle" then
        cx = number(node["attrs"]["cx"])
        cy = number(node["attrs"]["cy"])
        if cx < 0 or cx > w or cy < 0 or cy > h then return 0
    end if
    for each c in node["children"]
        if circles_inside(c, w, h) = 0 then return 0
    end for
    return 1
end function

' 1 when some text NODE (not markup) in the subtree contains the needle.
function has_text(node, needle)
    if is_string(node) then
        if contains(node, needle) then return 1
        return 0
    end if
    if not is_record(node) then return 0
    for each c in node["children"]
        if has_text(c, needle) = 1 then return 1
    end for
    return 0
end function

program main(args)
    load chart from "../stdlib/chart.bas"
    load xml

    ' The HOSTILE title: if escaping ever regresses, xml.parse raises here
    ' and the whole tier fails — on inputs no golden thought of.
    df = {
        m: [1, 2, 3, 4],
        a: [10, unknown, 30, 40],
        b: [5, 15, 25, 35]
    }
    s = chart.spec("line", df)
    s = chart.x(s, "m")
    s = chart.y(s, ["a", "b"])
    s = chart.title(s, "R&D <script>alert(1)</script> " + chr(34) + "q" + chr(34) + " & 'z'")
    s = chart.options(s, { markers: true, width: 400, height: 240 })
    svg = chart.render(s)

    doc = xml.parse(svg)
    print "root " + doc["name"]
    print "paths " + string(count_of(doc, "path"))
    ' markers: 3 finite points in series a, 4 in b
    print "circles " + string(count_of(doc, "circle"))
    print "circles inside viewBox " + string(circles_inside(doc, 400, 240))
    ' legend on (2 series): one swatch rect per series
    print "legend swatches " + string(count_of(doc, "rect"))
    ' the hostile title survived INTO the document as text, not markup
    print "hostile title is TEXT " + string(has_text(doc, "<script>alert(1)</script>"))
end program
