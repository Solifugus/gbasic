' `on warning stop` raises AT THE WARNING'S OWN SITE, and a caught escalation
' says what it started as.
function makes()
    return { a: 1 }
end function

function boom()
    on warning stop
    makes()                  ' line 9 -- the raise must be attributed HERE, not at the call site
    return "unreached"
end function

program main( args )
    on error goto next
    r = boom()
    if error then
        print "severity: " + error.severity
        print "line: " + string(error.line)
    end if
end program
