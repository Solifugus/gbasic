' The other `library toolkit`. See tests/libs/vendor_a/toolkit.bas.
library toolkit
    function describe()
        return "vendor B"
    end function
    function format(x)
        return "B:" + string(x)
    end function
    function only_b()
        return "b-only"
    end function
end library
