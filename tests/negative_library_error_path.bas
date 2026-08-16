' The error is raised inside tests/liberr/raiser.bas, so the diagnostic must
' name THAT file -- not this one, which is only 8 lines long.
program main(args)
    load raiser from "liberr/raiser.bas"
    print raiser.boom("now")
end program
