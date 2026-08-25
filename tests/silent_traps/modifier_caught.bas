' All five, catchable, with the domain in error.source. The valid cases beside
' each refusal are the point: over-refusing would be as much a defect.
program main( args )
    on error goto next

    d{date} = "2021-13-45"
    if error then
        print "date: " + error.source
    end if
    ok{date} = "2021-03-01"
    print "valid date: " + string(ok.year)

    t{time} = "25:99"
    if error then
        print "time: " + error.source
    end if

    dt{datetime} = "nope"
    if error then
        print "datetime: " + error.source
    end if

    f{file} = 42
    if error then
        print "file: " + error.source
    end if
    okf{file} = "tests/silent_traps/modifier_caught.bas"
    print "valid file: " + string(exists(okf))

    g{dir} = 7
    if error then
        print "dir: " + error.source
    end if
    print "still running"
end program
