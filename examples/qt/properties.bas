' PROPERTIES BY NAME -- the route that makes this backend small.
'
' Run it:
'     ./gbasic examples/qt/properties.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/properties.bas check
'
' QPushButton ALONE EXPOSES 75 PROPERTIES. A table of bindings for them would
' be bindings forever and would always be missing the one you want, so an
' option this backend does not own is handed to the widget's own QMetaObject BY
' NAME -- which is why `maxLength`, `placeholderText`, `toolTip`, `styleSheet`
' and `echoMode` all work without a line of code naming them.
'
' THERE ARE THREE DOORS AND THEY MUST AGREE ABOUT WHAT A NAME IS:
'   the DECLARATION     `field pin( maxLength: 4 )`     -- before anything runs
'   `qt.read`           what it is now
'   `qt.set`            change it, which the declaration cannot: it is written
'                       before anything has happened
'
' A MISSPELLING IS REFUSED BY NAME AT ALL THREE. That matters more than it
' looks: `setProperty` on a name a widget does not carry SUCCEEDS in Qt, storing
' a dynamic property nothing reads -- so an option would silently do nothing,
' which is exactly the class the closed head vocabulary exists to prevent one
' level up.
'
' AND THE KIND COMES FROM Qt, NOT FROM THE CALLER. `readOnly` answers a
' BOOLEAN; a backend that answered the text "false" would read BACKWARDS in
' `if qt.read(...)`, every non-empty string being true in gBASIC.

window form( title: "Properties", width: 440, height: 340 )
    column( margin: 10, spacing: 8 )

        text heading( label: "Set in the declaration" )

        form to edit account
            ' EVERY ONE OF THESE IS A Qt PROPERTY and none is known to this
            ' backend. They reach QLineEdit's QMetaObject by name.
            field name(  label: "Name", placeholderText: "as on the card" )
            field pin(   label: "PIN", maxLength: 4, echoMode: 2 )
            field note(  label: "Reference", toolTip: "shows on the statement" )
            field locked( label: "Account", readOnly: true,
                          styleSheet: "background: #eceff4" )
        end form

        row
            button save( text: "Save", enabled: false )
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' A HANDLER THAT CHANGES A PROPERTY, which is what `qt.set` is for: the
    ' declaration is written before anybody has typed anything, so "enable Save
    ' once there is a name" has nowhere else to live.
    on name.textChanged
        qt.set(form, "save", "enabled", len(qt.read(form, "name")) > 0)
    end on

    on close.clicked
        qt.stop()
    end on
end window

program main(args)
    account = { name: "", pin: "", note: "", locked: "A-1041" }
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if
    qt.open(form)

    if contains(args, "check") then
        print "-- read back what the DECLARATION set"
        print "pin maxLength:     " + string(qt.read(form, "pin", "maxLength"))
        print "pin echoMode:      " + string(qt.read(form, "pin", "echoMode"))
        print "name placeholder:  " + qt.read(form, "name", "placeholderText")
        print "note toolTip:      " + qt.read(form, "note", "toolTip")
        print "locked readOnly:   " + string(qt.read(form, "locked", "readOnly"))

        ' AND AN ENUM ANSWERS ITS NAME: `echoMode: 2` goes in as a number and
        ' comes back as "Password", because the QVariant Qt hands over carries
        ' the enumerator. Worth knowing before comparing one to the number you
        ' set.
        print "-- the KIND is Qt's, not the caller's"
        print "maxLength is a:    " + type(qt.read(form, "pin", "maxLength"))
        print "readOnly is a:     " + type(qt.read(form, "locked", "readOnly"))

        print "-- `qt.set`: what the declaration could not know"
        print "save enabled:      " + string(qt.read(form, "save", "enabled"))
        qt.write(form, "name", "Ada Lovelace")
        print "after typing:      " + string(qt.read(form, "save", "enabled"))

        print "-- a misspelling is refused BY NAME, at both doors"
        print "reading `maxLenght`: " + probe_read("pin", "maxLenght")
        print "setting `maxLenght`: " + probe_set("pin", "maxLenght")
        ' AND THE CONTROL, or "it refuses" is satisfied by refusing everything.
        qt.set(form, "pin", "maxLength", 6)
        print "a REAL property set:  " + string(qt.read(form, "pin", "maxLength"))
        qt.stop()
        return
    end if
    qt.run()
end program

function probe_read(part, prop)
    on error goto handle
    v = qt.read(form, part, prop)
    return "ANSWERED " + string(v)
handle:
    m = error.message
    error.clear()
    return m
end function

function probe_set(part, prop)
    on error goto handle
    qt.set(form, part, prop, 1)
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function
