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
end program
