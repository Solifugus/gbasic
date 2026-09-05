' A `load` OUTSIDE the program block, which now runs.
'
' STRUCK 2026-09-05, and this fixture is the negative control that keeps it
' struck. It used to assert the opposite: `load` was an executable statement and
' the statements outside a program block are not walked, so a top-level import
' silently did nothing and warned "this `load` is outside the program block, so
' it never runs -- move it inside `program`".
'
' The warning was true about the parent and blind to the ACTOR CHILD, which is a
' separate process that never enters the program block and for which the
' top-level position was the only one that worked. Parent and child ran two
' registration passes over different sets, so whichever position an author
' picked, one process was missing the import -- and taking the warning's advice
' produced a hang, not an error.
'
' `load` is a declaration now, hoisted from either side of the block by one pass
' both processes share. This file must print the library's answer and stderr
' must be empty.
load shadowlib

program main(args)
    print shadowlib.start_server(7)
end program
