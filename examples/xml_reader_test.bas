' WP-XML-4 — streaming reader: xml.reader / xml.read / xml.close. Event stream
' (kinds, names, depths, lines), idempotent close, use-after-close guard.
'
' `line` WAS libxml2's parser read-position and this comment used to say so --
' which is a golden DOCUMENTING A DEFECT as expected behaviour. Measured, that
' number is where the parser's input BUFFER has reached and not where the node
' is: every event here reported line 9 for an 8-line document, because the whole
' file is buffered before the first node is handed back. Fixed 2026-09-20 to
' read the node's own line, so the values below are the real ones.
'
' NOTE WHAT AN `end` EVENT REPORTS: the line the ELEMENT STARTED on, not the
' line its closing tag is on -- `end book` says 2 while `</book>` is on line 4.
' That is the node's line, it identifies the element rather than the token, and
' it is stated here because the difference is invisible in a one-line element.
program main(args)
    load xml

    src = "<catalog>\n  <book id=\"b1\">\n    <title>XML</title>\n  </book>\n  <book id=\"b2\">\n    <title>Streaming</title>\n  </book>\n</catalog>\n"
    f{file}= "examples/tmp_xml_reader.xml"
    if exists(f) then
        delete(f)
    end if
    write(f, src)

    r = xml.reader("examples/tmp_xml_reader.xml")
    n = 0
    while true
        ev = xml.read(r)
        if is_nothing(ev) then
            break
        end if
        n = n + 1
        if ev["kind"] = "text" then
            print(ev["kind"] + " [" + ev["text"] + "] depth=" + string(ev["depth"]) + " line=" + string(ev["line"]))
        else
            attrnote = ""
            if ev["kind"] = "element" then
                if has(ev["attrs"], "id") then
                    attrnote = " id=" + ev["attrs"]["id"]
                end if
            end if
            print(ev["kind"] + " " + ev["name"] + attrnote + " depth=" + string(ev["depth"]) + " line=" + string(ev["line"]))
        end if
    end while
    print("total_events=" + string(n))

    ' idempotent close
    xml.close(r)
    xml.close(r)
    print("double_close_ok=true")

    delete(f)
end program
