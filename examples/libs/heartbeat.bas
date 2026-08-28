' Fixture for examples/qualified_function_value_test.bas. A separate file
' because the point is a LIBRARY function used as a value -- a same-file
' function would resolve unqualified and prove nothing.
library heartbeat
    function tick(n)
        return "tick " + string(n)
    end function

    function label()
        return "heartbeat"
    end function
end library
