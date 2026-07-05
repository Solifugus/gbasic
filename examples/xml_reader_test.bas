' WP-XML-4 — streaming reader: xml.reader / xml.read / xml.close. Event stream
' (kinds, names, depths, lines), idempotent close, use-after-close guard. `line`
' is libxml2's parser read-position (buffered small docs report the last line;
' large streamed docs advance) — depths/kinds/names are the per-element signal.
program main(args)
    load xml

    src = "<catalog>\n  <book id=\"b1\">\n    <title>XML</title>\n  </book>\n  <book id=\"b2\">\n    <title>Streaming</title>\n  </book>\n</catalog>\n"
    f(file)= "examples/tmp_xml_reader.xml"
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
