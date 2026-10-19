' THE WHEEL -- Ctrl + wheel zooms the editor, which is what every code editor
' does and what gBASIC Studio uses a scroll controller for.
'
' A WHEEL IS AN EVENT, NOT A SIGNAL. A Qt widget has no wheel signal -- it
' RECEIVES one -- so `on code.wheel` is handled the way `on <window>.closed` is,
' and the handler asks `qt.wheel()` for the event rather than taking arguments.
' `dy` is in NOTCHES (one click of the wheel) and positive means away from you.
'
' THE ZOOM NEEDS NO VERB. `font` is an ordinary property that reads back as
' text -- "monospace,9,-1,5,400,..." -- so the size is just a number in a list:
' split it, change it, write it back.
'
' `qt.swallow()` IS WHAT STOPS THE EDITOR ALSO SCROLLING, and it is not
' optional: Ctrl + wheel scrolls a text view FASTER than a plain wheel, so
' without it the view jumps about while the text resizes. A handler that
' DECLINES leaves the wheel alone, which is what keeps ordinary scrolling
' working -- so the rule is "only with Ctrl", exactly as Studio's is.

window app( title: "Zoom", width: 520, height: 360 )
    column( margin: 8, spacing: 6 )
        text hint( label: "Ctrl + wheel over the code zooms it." )
        source code()
    end column

    on code.wheel
        ev = qt.wheel()
        if not ev.ctrl then
            ' NOTHING, and the event is left alone so the editor scrolls.
            return
        end if
        f = split(qt.read(app, "code", "font"), ",")
        bigger = 1
        if ev.dy < 0 then
            bigger = 0 - 1
        end if
        qt.set(app, "code", "font", f[0] + "," + string(number(f[1]) + bigger))
        g.zooms = g.zooms + 1
        qt.swallow()
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    g = { zooms: 0 }
    qt.open(app)

    lines = []
    for i = 1 to 200
        lines = append(lines, "print " + string(i))
    end for
    x = qt.write(app, "code", join(lines, chr(10)))

    size = number(split(qt.read(app, "code", "font"), ",")[1])
    print "font at first:   " + string(size)

    ' A PLAIN WHEEL SCROLLS and does not zoom. `qt.scroll` drives the wheel the
    ' way a mouse does, and answers whether anything CLAIMED the event.
    qt.scroll_to(app, "code", 50)
    claimed = qt.scroll(app, "code", -2)
    print ("plain wheel:     claimed " + string(claimed) +
           ", zooms " + string(g.zooms))
    print ("                 and it scrolled to " +
           string(qt.scrolled(app, "code")))

    ' CTRL + WHEEL ZOOMS and claims the event, so the view stays put.
    was = qt.scrolled(app, "code")
    claimed = qt.scroll(app, "code", 3, "ctrl")
    print ("ctrl+wheel:      claimed " + string(claimed) +
           ", zooms " + string(g.zooms))
    print ("                 font now " +
           string(number(split(qt.read(app, "code", "font"), ",")[1])))
    print "                 view stayed at " + string(qt.scrolled(app, "code") = was)

    qt.close(app)
end program
