' THE SAME ATTEMPT, UNCAUGHT. What must NOT happen is the process dying by a
' signal with a core dumped and nothing on stdout; what must happen is an
' ordinary located gBASIC runtime error, after the program's own output.
window tiny( title: "Tiny" )
    column
        button go( text: "Go" )
    end column
end window

program main()
    print "progress line one"
    ok = qt.open(tiny)
    print "MISMATCH this line must not be reached"
end program
