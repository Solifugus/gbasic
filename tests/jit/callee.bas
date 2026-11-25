' The library tests/jit/values.bas calls qualified: its functions are
' compiled under GBASIC_COMPILE=all like any other, and call each other
' unqualified and through a function value.
library callee
    function twice(x)
        return x * 2
    end function
    function via_own(x)
        return twice(x) + 1
    end function
    function via_value(x)
        g = twice
        return g(x) + 2
    end function
    function refuses(x)
        return x / 0 + undefined_name
    end function
    ' named as a process handle's verb
    function poll()
        return "the library's poll"
    end function
end library
