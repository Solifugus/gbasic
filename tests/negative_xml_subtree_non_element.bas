program main(args)
    load xml
    p(file)= "examples/tmp_xml_sub.xml"
    write(p, "<a><b>x</b></a>")
    r = xml.reader("examples/tmp_xml_sub.xml")
    ev = xml.read(r)
    ev = xml.read(r)
    ev = xml.read(r)
    t = xml.subtree(r)
end program
