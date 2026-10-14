' `warning` is a SOFT name: resolved after the environment walk, so a variable
' shadows it -- the same rule bare function names already follow. This is what
' lets it exist without a reserved word.
program main( args )
    print "no warning pending: " + string(warning)

    warning = 41
    print "shadowed by a local: " + string(warning + 1)

    r = { warning: "a field" }
    print "record field: " + r.warning

    board = { warning: [] }
    append(board.warning, "kept")
    print "field array: " + board.warning[0]

    ' AND IT IS ANSWERED FOR, which until 2026-10-14 it was not. `warning` was
    ' the fourth of the four names the thirteen-libraries report measured as
    ' DENIED BUT CALLABLE on the published 0.7.0 archive: `has_builtin` said
    ' false while `warning()` answered "warning expects one argument", an ARITY
    ' error, which proves it was in the dispatch the whole time. That is the
    ' worse of the two directions this invariant fails in -- a program that
    ' CHECKED FIRST and then called took its fallback path for a function the
    ' binary runs, which is the one thing `has_builtin` exists to prevent.
    '
    ' THE OTHER THREE WERE FIXED BY REGISTERING THEM AND THIS ONE COULD NOT BE:
    ' registering the name would make the parser know it, and every line above
    ' this one would stop working. It is answered from `dispatch_only[]`, which
    ' `gbasic_has_builtin` reads and nothing else does -- so the lines above are
    ' the CONTROL on the line below, and they are deliberately in the same
    ' fixture for that reason.
    print "has_builtin: " + string(has_builtin("warning"))
end program
