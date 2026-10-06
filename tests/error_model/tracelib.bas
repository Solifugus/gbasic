' A raise INSIDE A LIBRARY, so `library` is pinned on both kinds of frame. The
' companion library lives beside this file, which is the only place `load` looks
' (run_library_depth.sh: beside the loading file, never below it).
load tracehelper
function mine()
    return tracehelper.boom()
end function
program main( args )
    on error goto next
    x = mine()
    if error then
        owners = []
        paths = 0
        for each fr in error.trace
            if fr.library = "" then
                append(owners, fr.name + "=root")
            else
                append(owners, fr.name + "=" + fr.library)
            end if
            if fr.path != "" then
                paths = paths + 1
            end if
        end for
        print "owners: " + join(owners, ",")
        print "frames with a path: " + string(paths) + " of " + string(count(error.trace))
    end if
end program
