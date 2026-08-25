' THE case that motivated the change. An unqualified call to a `load`ed
' library's function with an IDENTIFIER argument, compared with `=`.
'
' `name(caseless) = "joe"` and `kind(x) = "record"` were the same tokens in the
' same order, so the parser had to guess -- and guessed clause, producing a
' RUNTIME error naming the caller's own argument as a missing modifier. With
' the clause spelled in braces there is nothing to guess: `(` opens a call and
' only a call.
program main( args )
    ' Loaded from a SEPARATE FILE deliberately: the old lookahead's function
    ' check re-scanned only the file being parsed, so a function declared here
    ' was invisible to it -- which is what made this case unreachable.
    load clause_probe from "../../examples/libs/clause_probe.bas"
    x = 1
    ' The probe's kind() returns "record" for anything, so a CALL makes this
    ' true. Under the paren spelling it never got here: the parser guessed
    ' "clause" and died at run time with `compare modifier not found: x`,
    ' naming the caller's own argument as a missing modifier.
    if kind(x) = "record" then
        print "unqualified call, identifier argument: parsed as a CALL"
    end if

    ' the other half: a real clause, in the spelling that cannot be mistaken
    name = "Joe"
    if name{caseless} = "joe" then
        print "and a clause is still a clause"
    end if
end program
