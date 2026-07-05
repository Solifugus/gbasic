program main(args)
    load xml
    p(file)= "examples/tmp_xml_sub.xml"
    write(p, "<a/>")
    r = xml.reader("examples/tmp_xml_sub.xml")
    xml.close(r)
    t = xml.subtree(r)
end program
