' Every way out of a `for each` over a csv source, for valgrind --track-fds:
' the loop owns the file, so none of these may leave it open.
function by_return(f)
    for each r in csv.records(f)
        return r.a
    end for
    return "none"
end function

function by_raise(f)
    for each r in csv.records(f)
        x = r.no_such_field
    end for
    return "none"
end function

function by_reader_raise(g)
    for each r in csv.records(g)
        x = r.a
    end for
    return "none"
end function

program main(args)
    f{file}= args[0]
    g{file}= args[1]
    n = 0
    for each r in csv.records(f)
        n = n + 1
    end for
    print("end: " + string(n))
    for each r in csv.records(f)
        break
    end for
    print("break")
    for each outer in [1, 2]
        for each r in csv.records(f)
            break outer
        end for
    end for
    print("break outer")
    for each outer in [1, 2]
        for each r in csv.records(f)
            continue outer
        end for
    end for
    print("continue outer")
    print("return: " + by_return(f))
    on error goto next
    x = by_raise(f)
    if error then
        print("raise in the body")
    end if
    x = by_reader_raise(g)
    e = error
    if e then
        print("raise in the reader: " + e.message)
    end if
end program
