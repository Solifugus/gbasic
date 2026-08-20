function setup()
    watch inner(g)
        print g
    end watch
    return 0
end function

g = 1
x = setup()
