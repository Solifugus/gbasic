' THE QT BACKEND, RENDERING A `window` DECLARATION. Runs under
' QT_QPA_PLATFORM=offscreen, which is real Qt with no display -- the same
' arrangement docs/qt_gui_design.md §0.3 keeps available to a USER so a GUI
' program can render on a headless server.
'
' SELF-CHECKING AND FORCED. Every defect this backend can have produces an
' ordinary-looking RUN: a property silently stored as a dynamic one nothing
' reads, a handler connected to nothing, a value written to the wrong part --
' and none of them raises, so a golden would record the damaged run as expected
' and defend it. Each check states its own expected answer.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window customer( title: "Customer", width: 480, height: 320 )
    column( margin: 8, spacing: 8 )

        form to edit cust
            field id(   label: "Account" )
            field name( label: "Name" )
        end form

        group "Credit"
            form to edit cust
                field limit( label: "Limit", type: {USD} )
                check hold(  label: "On hold" )
            end form
        end group

        row
            stretch()
            button save(   text: "Save", default: true )
            button cancel( text: "Cancel" )
        end row

    end column

    ' RE-ENTRY INTO THE EVALUATOR FROM A REAL QT SIGNAL. The handler prints
    ' rather than assigning: gBASIC has no closures, so a handler is a function
    ' and a write to an enclosing name would be a function-local -- which is
    ' the read-then-shadow rule run_core.sh exists for, not a defect here.
    ' TWO LINES, AND THE SPLIT IS LOAD-BEARING. The first needs nothing but the
    ' handler having run, so a handler fired during CONSTRUCTION is visible in
    ' the ordering; the second reads the live widget, which is what says the
    ' signal arrived with the window actually built. With one line doing both,
    ' a connect-time fire raises inside `qt.read` and prints NOTHING -- so the
    ' ordering control could not see it, which is how the perturbation written
    ' for that control came back green.
    on save.clicked
        print "SIGNAL save.clicked"
        print "SIGNAL read -> " + qt.read(customer, "name")
    end on
end window

program main()
    ' THE PROGRESS LINE, FIRST. A crash takes block-buffered stdout with it, so
    ' this line arriving at all is part of what the no-display tier asserts one
    ' fixture over.
    print "before open"

    check("the declaration is still just a record", customer.kind, "window")
    check("and the handler carries something callable",
          type(customer.items[1].handler), "function")

    check("qt.open answers", qt.open(customer), true)
    print "opened"

    ' A PROPERTY SET AT CONSTRUCTION, read back THROUGH QT rather than from our
    ' own record -- which is the only thing that distinguishes a property that
    ' reached the widget from one we merely remembered.
    check("a button's text reached the widget", qt.read(customer, "save"), "Save")

    check("a field starts empty", qt.read(customer, "name"), "")
    check("writing reaches it", qt.write(customer, "name", "Ada Lovelace"), true)
    check("and reads back", qt.read(customer, "name"), "Ada Lovelace")

    ' MONEY THROUGH THE ONE RENDERER (PLAT-RENDER), so a typed value reaches a
    ' widget in the words the language itself uses.
    m {USD}= "1250.50"
    check("money is written in gBASIC's own words", qt.write(customer, "limit", m), true)
    check("and reads back as that text", qt.read(customer, "limit"), "1250.50")

    check("a check box starts clear", qt.read(customer, "hold"), "false")
    check("and can be set", qt.write(customer, "hold", true), true)
    check("reading a box answers its state", qt.read(customer, "hold"), "true")

    ' THE SIGNAL. `click` is invoked BY NAME through QMetaObject, which emits
    ' the real `clicked` signal through the real connection -- so what is being
    ' tested is the whole path and not a short cut around it.
    check("invoking a method by name", qt.invoke(customer, "save", "click"), true)
    print "after click"

    ' REFUSALS, each beside a legal neighbour above it.
    on error goto next
    x = qt.read(customer, "nosuchpart")
    if error then
        print "refused a part that does not exist: " + error.message
    else
        print "MISMATCH reading an absent part was allowed"
    end if

    on error goto next
    x = qt.invoke(customer, "save", "nosuchmethod")
    if error then
        print "refused a method that does not exist: " + error.message
    else
        print "MISMATCH invoking an absent method was allowed"
    end if

    on error goto next
    x = qt.open(customer)
    if error then
        print "refused opening the same window twice: " + error.message
    else
        print "MISMATCH the same window opened twice"
    end if

    print "done"
end program
