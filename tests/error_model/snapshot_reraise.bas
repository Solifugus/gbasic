' `e = error` acknowledges and snapshots in one move; `error e` re-raises a
' snapshot, preserving code, source and details -- one rule covers both
' structured raises and re-raises, because a snapshot carries `message`.
' The relay disarms (`on error stop`) before re-raising, or its own armed
' handler would swallow the re-raise.
function original()
    error { message: "custom failure", code: 4321, kind: "demo" }
    return 0
end function

function relay()
    on error goto next
    r = original()
    e = error
    if e then
        print "snapshot: " + e.message + " / " + string(e.code)
        print "detail kind: " + e.details.kind
        on error stop
        error e
    end if
    return 0
end function

program main( args )
    on error goto next
    r = relay()
    if error then
        print "re-raised code " + string(error.code) + " detail " + error.details.kind
    end if
end program
