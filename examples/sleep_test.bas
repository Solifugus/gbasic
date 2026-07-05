program demo(args)
    ' sleep returns the requested seconds (like seed returns its input)
    print(sleep(0) = 0)
    ' fractional seconds are accepted
    print(is_number(sleep(0.001)))
    ' an exactly-representable fractional returns unchanged
    print(sleep(0.125) = 0.125)
end program
