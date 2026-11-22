' The library tests/resolve/calls.bas loads: a library function calling its
' own helper unqualified.
library resolvelib
    function helper(x)
        return x + x
    end function

    function twice(x)
        return helper(x)
    end function
end library
