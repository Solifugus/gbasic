library alpha
    function helper()
        return "alpha"
    end function
    ' Calls its OWN helper, unqualified -- the natural way to write it.
    function outer()
        return helper()
    end function
end library
