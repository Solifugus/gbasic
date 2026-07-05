program main(args)
    load xml
    d = xml.parse("<a/>")
    x = xml.find(d, 42)
end program
