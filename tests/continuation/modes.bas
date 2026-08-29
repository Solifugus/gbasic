' The two STATEFUL lexer modes, exercised beside the change that touches the
' lexer. A brace modifier's content is read in a mode of its own and a
' `consider` branch is recognised by COLUMN, so a change that counts braces or
' moves lines is exactly the kind that disturbs them -- and neither would show
' up in the continuation tiers, which never write a modifier or a consider.

program main( args )
    ' a lens clause: the LBRACE and RBRACE around it are ordinary tokens, so
    ' the depth must return to zero across it
    p{USD} = 19.95
    d{date} = "2026-03-01"
    print "money: " + string(p)
    print "date:  " + string(d.year)

    ' after a modifier clause the newline must still terminate
    n{number} = "42"
    print "number: " + string(n + 1)

    ' consider is column-sensitive; a continued line above it must not move
    ' the columns it reads
    kind = build(
        "circle"
    )
    consider kind
    if "circle" then
        print "round"
    if "square" then
        print "cornered"
    else
        print "unknown"
    end consider
end program

function build(name)
    return name
end function
