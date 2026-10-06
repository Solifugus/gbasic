' Every error carries the call stack at raise time, innermost first. The
' field is `name` (function is a keyword and cannot follow a dot).
function deepest()
    error "boom"
    return 0
end function

function middle()
    return deepest()
end function

program main( args )
    on error goto next
    r = middle()
    if error then
        names = []
        for each fr in error.trace
            append(names, fr.name)
        end for
        print "trace: " + join(names, ",")

        ' EVERY FRAME SAYS WHICH FILE IT IS IN. `source_path` is stamped at
        ' registration only for an IMPORTED function, so a root-program frame
        ' carried NULL and reported `path: ""` -- measured, a library frame said
        ' `./lib.bas` while the frame that CALLED it said nothing. The path was
        ' known all along; it was never asked for. Asserted as NON-EMPTY rather
        ' than as a literal, because the literal is the one part that can vary
        ' with how the fixture is invoked, and "it has a path" is the claim.
        missing = 0
        for each fr in error.trace
            if fr.path = "" then
                missing = missing + 1
            end if
        end for
        print "frames without a path: " + string(missing)

        ' `library` SEPARATES A LIBRARY FRAME FROM A ROOT ONE, which is what
        ' lets a report name a library without listing every frame inside it.
        ' Empty means THE ROOT PROGRAM -- not "unknown", which is what it would
        ' have meant while `path` was also empty. Both of these frames are root.
        libs = []
        for each fr in error.trace
            if fr.library = "" then
                append(libs, "root")
            else
                append(libs, fr.library)
            end if
        end for
        print "frame owners: " + join(libs, ",")
    end if
end program
