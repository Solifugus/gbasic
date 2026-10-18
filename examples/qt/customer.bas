' A CUSTOMER FORM -- the smallest useful Qt program, and the one
' docs/qt_gui_design.md §2 draws.
'
' Run it:
'     ./gbasic examples/qt/customer.bas
'
' With no display it says so and does nothing else. On a headless machine you
' can still render it to the offscreen platform, which is what the test suite
' does:
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/customer.bas
'
' AN AUTHOR NEVER WRITES A COORDINATE. There is no Top, Left, Width or Height
' on anything inside the window -- `form` aligns its label/field rows, `row`
' and `column` are linear boxes, and `stretch` absorbs the leftover space,
' which is how the two buttons end up on the right. Qt's layouts compute the
' rest, so a resize, a translation or a different font costs nothing.

window customer( title: "Customer", width: 420 )
    column( margin: 12, spacing: 10 )

        form to edit cust
            field id(    label: "Account" )
            field name(  label: "Name" )
            field email( label: "Email" )
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

    ' A handler is ordinary gBASIC. `qt.subject` answers the WHOLE record the
    ' form is working on, read from the widgets at this moment -- so a handler
    ' says `saving` once rather than naming every part it wants back.
    on save.clicked
        edited = qt.subject(customer, "cust")
        print "saving:"
        print "  account " + edited.id
        print "  name    " + edited.name
        print "  email   " + edited.email
        print "  limit   " + string(edited.limit) + " (" + type(edited.limit) + ")"
        print "  on hold " + string(edited.hold) + " (" + type(edited.hold) + ")"
        qt.stop()
    end on

    on cancel.clicked
        print "cancelled"
        qt.stop()
    end on
end window

program main()
    ' ASK FIRST. `qt.can_display()` answers rather than raising -- including on
    ' a build with no Qt in it at all -- so a program can do something else
    ' instead of ending.
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
    else
        ' THE SUBJECT, AND NOTHING ELSE TO WIRE. Both forms above say
        ' `form to edit cust`, and a field named `email` takes `cust.email` --
        ' the binding IS the name. `qt.open` reads `cust` from this frame, so
        ' the record only has to be in scope here.
        '
        ' NOTE `limit` IS MONEY, not text: the field declares `type: {USD}`, so
        ' it is shown in gBASIC's own words and comes back as money rather than
        ' as the digits somebody typed.
        cust = { id: "A-1041", name: "Ada Lovelace",
                 email: "ada@example.com", limit: {USD}"2500.00",
                 hold: false }

        qt.open(customer)
        qt.run()
    end if
end program
