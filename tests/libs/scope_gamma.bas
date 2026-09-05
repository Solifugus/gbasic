' A library in its OWN file. `outer` calls `inner` unqualified and must reach
' THIS library's inner even though scope_delta also defines one and is loaded
' afterwards.
library scope_gamma
    function inner()
        return "gamma inner"
    end function
    function outer()
        return inner()
    end function

    export modifier doubled for assign
        ' A MODIFIER BODY IS LIBRARY CODE TOO, and it is invoked by a different
        ' path than a function, so it needs its own tracking. `dates`'s
        ' `end of month` modifier calling `_end_of_month` is the real case.
        return twice(value)
    end modifier

    function twice(x)
        return x * 2
    end function
end library
