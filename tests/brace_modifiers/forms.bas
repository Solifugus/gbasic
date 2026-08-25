' Every modifier position in the brace spelling.
modifier shout for assign
    return upper(value) + "!"
end modifier

modifier wrap(head, tail) for compare
    return compare(left + head + tail, operator, right)
end modifier

program main( args )
    ' assignment, the form that did not exist before
    p{USD} = 19.95
    d{date} = "2026-03-01"
    f{file} = "tests/brace_modifiers/forms.bas"
    n{number} = "42"
    s{string} = 7
    t{trimmed} = "  padded  "
    l{lowered} = "MiXeD"
    u{uppered} = "MiXeD"

    print "money:   " + string(p)
    print "date:    " + string(d.year)
    print "file:    " + string(exists(f))
    print "number:  " + string(n + 1)
    print "string:  [" + s + "]"
    print "trimmed: [" + t + "]"
    print "lowered: " + l
    print "uppered: " + u

    ' a user-declared modifier, same spelling
    m{shout} = "hey"
    print "declared: " + m

    ' comparison, which already worked -- it must keep working
    name = "Joe"
    if name{caseless} = "joe" then
        print "compare:  caseless ok"
    end if
    if "x"{wrap "L", "T"} = "xLT" then
        print "compare:  with arguments ok"
    end if

    ' a field and an index as the target
    r = { }
    r.total{USD} = 5.5
    a = [0]
    a[0]{number} = "9"
    print "field:    " + string(r.total)
    print "index:    " + string(a[0] + 1)
end program
