program main(args)
    load xml
    p(file)= "examples/tmp_xml_uac.xml"
    write(p, "<a/>")
    r = xml.reader("examples/tmp_xml_uac.xml")
    xml.close(r)
    ev = xml.read(r)
end program
