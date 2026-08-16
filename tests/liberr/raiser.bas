' A library whose function raises.
'
' The padding below is deliberate: it pushes the `error` statement well past the
' end of the CALLING file, so a diagnostic naming the caller cannot be explained
' away as a coincidence. The caller is 6 lines long; the error is at line 20 of
' this file.
'
'
'
'
'
'
library raiser
    function boom(what)
        if what = "now" then
            ' The line below is line 20.
            error "raiser: deliberate failure"
        end if
        return "ok"
    end function
end library
