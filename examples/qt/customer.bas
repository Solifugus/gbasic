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

    ' A handler is ordinary gBASIC. `qt.read` answers what the widget is
    ' showing right now, so this is the one place the form's state is read.
    on save.clicked
        print "saving:"
        print "  account " + qt.read(customer, "id")
        print "  name    " + qt.read(customer, "name")
        print "  email   " + qt.read(customer, "email")
        print "  limit   " + qt.read(customer, "limit")
        print "  on hold " + qt.read(customer, "hold")
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
        cust = { id: "A-1041", name: "Ada Lovelace",
                 email: "ada@example.com", limit: "2500.00", hold: false }

        qt.open(customer)

        ' POPULATION IS THE PROGRAM'S, for now. `show:`/`edit:` name a place to
        ' read in the frame where the subject lives, so the declaration records
        ' them by name and not by value -- which means a backend cannot read
        ' them out of the record. The watcher pair that makes `form to edit`
        ' genuinely two-way is the next increment; see
        ' docs/qt_gui_design.md §0.4.
        qt.write(customer, "id",    cust.id)
        qt.write(customer, "name",  cust.name)
        qt.write(customer, "email", cust.email)
        qt.write(customer, "limit", cust.limit)
        qt.write(customer, "hold",  cust.hold)

        qt.run()
    end if
end program
