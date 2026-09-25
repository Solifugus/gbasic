program demo(args)
    ' `sleep` ANSWERS NOTHING, since 2026-09-24. It used to answer the seconds
    ' you asked for -- "like seed returns its input", as this file's previous
    ' comment put it -- which is not an answer: the caller already has it. It
    ' cost two things. At a prompt `sleep(2)` printed a 2 nobody asked for, and
    ' because the resident program keeps a line that ACTS and drops one that
    ' merely ANSWERS, a `sleep` typed at the bench was read as a question and
    ' left out of the program.
    print(is_nothing(sleep(0)))
    ' Fractional seconds are still accepted -- the argument is what matters,
    ' and that it elapses is asserted against the WALL CLOCK in run_core.sh,
    ' which is the only honest oracle for it.
    print(is_nothing(sleep(0.001)))
    print(is_nothing(sleep(0.125)))
end program
