' A named continue that names no loop escapes the program block's body as
' its result, which eval_program raises: so a compiled list must hand its
' result back, and this is what shows it (the run's only other way out of
' a program block, a raise, is reported by the frame, not the result).
program main()
    print "before"
    for i = 1 to 3
        continue nosuch
    end for
    print "not reached"
end program
