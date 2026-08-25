program main(args)
    load xml
    p{file}= "examples/tmp_xml_deep.xml"
    write(p, "<root>" + repeat("<a>", 300) + repeat("</a>", 300) + "</root>")
    r = xml.reader("examples/tmp_xml_deep.xml")
    while true
        ev = xml.read(r)
        if is_nothing(ev) then
            break
        end if
    end while
end program
