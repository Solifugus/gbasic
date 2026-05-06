library math
    function add(a, b)
        return a + b
    end function
end library

library text
    export modifier shout for assign
        return upper(value)
    end modifier
end library

program demo(args)
    load math
    load text

    print(add(2, 3))

    msg(shout)= "hello"
    print(msg)
end program
