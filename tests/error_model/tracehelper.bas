library tracehelper
    function boom()
        error "from inside a library"
        return 1
    end function
end library
