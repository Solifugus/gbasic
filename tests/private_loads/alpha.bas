library alpha
    load util from "alpha_util.bas"
    function ask()
        return util.who()
    end function
    function handed()
        return util.who
    end function
    function boom()
        x = util.fail()
        return x
    end function
end library
