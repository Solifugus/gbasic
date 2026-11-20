' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' One way of ending with the terminal open, named by the first argument
' (tests/run_term.sh, tier restore). The driver holds the pty's settings
' after each to what they were before it started.
'   end      the program simply ends
'   close    term.close(), then more output
'   raise    a runtime error nobody catches
'   warn     a warning while the screen is up, then the end
'   wait     waits for a signal or a typed Ctrl-C
'   keys     waits for the key q, whatever else arrives (a stop and a continue)
'   actor    an actor, which the driver kills with SIGTERM while the
'            terminal is open (it inherits the restoring handlers, and must
'            not run them on the parent's terminal), then q
load term

function child(parent)
    send(parent, "started")
    m = receive()
end function

program main(args)
    how = args[0]
    term.open({ mouse: true })
    term.put(1, 1, "open")
    term.show()
    print to error "opened"
    if how = "close" then
        term.close()
        print "after close"
    else if how = "raise" then
        x = [1, 2][5]
    else if how = "warn" then
        print to error "a line written while the screen was up"
    else if how = "wait" then
        e = term.wait(20 seconds)
    else if how = "keys" then
        going = true
        while going
            e = term.wait(20 seconds)
            going = is_unknown(e) = false and not (e.kind = "key" and e.key = "q")
        end while
    else if how = "actor" then
        a = spawn child(self())
        r = receive()
        print to error "actor started"
        going = true
        while going
            e = term.wait(20 seconds)
            going = is_unknown(e) = false and not (e.kind = "key" and e.key = "q")
        end while
    end if
end program
