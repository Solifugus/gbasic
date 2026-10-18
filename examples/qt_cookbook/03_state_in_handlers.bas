' WHERE A HANDLER KEEPS STATE -- the one thing every author hits first.
'
' gBASIC has no closures, and a handler fires beneath `qt.run()`. It CAN READ a
' variable in `main` and it CANNOT REBIND one: a plain assignment there
' silently makes a handler-local instead, and warning 2104 says so and names
' the remedy. So a stateful window keeps its state in ONE RECORD, and handlers
' mutate its fields -- `g.count = ...` reaches main's record because a field
' write resolves through the frame chain.
window tally( title: "Tally", width: 280, height: 150 )
    column( margin: 10, spacing: 6 )
        text shown( label: "0" )
        row
            button up(    text: "Add one" )
            button reset( text: "Reset" )
        end row
    end column

    on up.clicked
        g.count = g.count + 1
        show_count()
    end on
    on reset.clicked
        g.count = 0
        show_count()
    end on
end window

' A FUNCTION CALLED FROM A HANDLER can read the same record and write widgets.
' It must `return nothing`, or warning 2101 fires on the discarded result.
function show_count()
    qt.write(tally, "shown", string(g.count) + " so far")
    return nothing
end function

program main(args)
    ' ONE RECORD, declared before the window opens.
    g = { count: 0 }

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(tally)

    qt.invoke(tally, "up", "click")
    qt.invoke(tally, "up", "click")
    qt.invoke(tally, "up", "click")
    print "after three clicks the widget says: " + qt.read(tally, "shown")
    ' AND MAIN CAN SEE IT, which is the whole point: the handler mutated the
    ' record this frame holds.
    print "and main's own record says:        " + string(g.count)

    qt.invoke(tally, "reset", "click")
    print "after reset:                       " + string(g.count)
    qt.stop()
end program
