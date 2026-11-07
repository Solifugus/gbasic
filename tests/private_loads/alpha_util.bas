' alpha's own `util`, a different library from beta's and the program's
library util
    function who()
        return "alpha's util"
    end function
    function fail()
        error "raised in alpha's util"
        return nothing
    end function
end library
