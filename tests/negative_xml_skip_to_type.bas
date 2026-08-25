program main(args)
    load xml
    p{file}= "examples/tmp_xml_sub.xml"
    write(p, "<a/>")
    r = xml.reader("examples/tmp_xml_sub.xml")
    ok = xml.skip_to(r, 42)
end program
