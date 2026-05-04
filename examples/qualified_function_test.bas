library math
    function add(a, b)
        return a + b
    end function
end library

library weirdmath
    function add(a, b)
        return a + b + 100
    end function
end library

program demo(args)
    use math
    use weirdmath

    print math.add(2, 3)
    print weirdmath.add(2, 3)
end program
