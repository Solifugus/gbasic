' A WINDOW SHOWN MODALLY -- what `qt.confirm` is not.
'
' A message box offers two buttons and that is the whole of it, so anything
' richer -- a form to fill in, a list to choose from -- is an ordinary `window`
' declaration shown modally. IT IS THE SAME WINDOW: one shared open path, so
' the declaration, the binding and the handlers are the same code, and nothing
' here is modal-specific except the two verbs that answer it.
'
' `qt.open_modal` BLOCKS until somebody answers, and answers TRUE for an accept
' and FALSE for a reject, a close, or the window being dismissed. It CLOSES THE
' WINDOW BEFORE RETURNING, which is why the `closed` handler is where you read
' the form -- the same rule an ordinary window follows.
'
' AND IT DOES NOT STARVE THE REST OF THE PROGRAM. Opened from `main`, gBASIC's
' own sources keep running: a webserver answers and a `timer` fires, which is
' what lets this recipe answer its own window. The limitation is the other case
' and `qt.confirm` already has it: a modal window raised from INSIDE a handler
' suspends every source until it is answered.
window edit( title: "Customer", width: 360, height: 200 )
    column( margin: 10, spacing: 8 )
        form to edit cust
            field who(   label: "Name" )
            field limit( label: "Limit", type: {USD} )
        end form
        row
            stretch()
            button save(   label: "Save" )
            button cancel( label: "Cancel" )
        end row
    end column

    ' TWO VERBS, AND THEY ARE NOT `qt.answer`. That one drives a DIALOG from
    ' the program when nobody is there -- a timeout, an unattended run. These
    ' are the person's own answer arriving through a button they pressed.
    on save.clicked
        x = qt.accept(edit)
    end on
    on cancel.clicked
        x = qt.reject(edit)
    end on

    ' THE ONE PLACE THE FORM IS READ, because the next thing that happens is
    ' the window closing. `g` is one record whose fields handlers mutate, which
    ' is where GUI state lives in gBASIC.
    on edit.closed
        g.typed = qt.subject(edit, "cust")
    end on
end window

program main(args)
    g = { typed: {}, ticks: 0, press: "save" }

    if not qt.can_display() then
        print "no display -- nothing to open"
        return
    end if

    cust = { who: "Ada Lovelace", limit: {USD}"500.00" }

    ' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one -- and that it
    ' CAN is the measurement that matters: the timer only fires because
    ' gBASIC's sources are serviced inside the modal loop.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks = 2 then
                y = qt.write(edit, "who", "Grace Hopper")
                z = qt.invoke(edit, g.press, "click")
            end if
        end while
    end watch

    ' SAVED.
    if qt.open_modal(edit) then
        print "saved:     " + g.typed.who + ", " + string(g.typed.limit)
    else
        print "cancelled"
    end if
    print "limit kind: " + type(g.typed.limit)
    print "open after: " + string(qt.is_open(edit))

    ' CANCELLED. The same window, the same declaration, a different button --
    ' and the handler still read the form, because `closed` fires either way.
    ' What decides whether to USE it is the answer, not the handler.
    g.ticks = 0
    g.press = "cancel"
    if qt.open_modal(edit) then
        print "saved:     " + g.typed.who
    else
        print "cancelled, and the form read anyway: " + g.typed.who
    end if

    cancelled = timer.cancel(beat)
end program
