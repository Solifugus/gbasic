' PROPERTIES BY WORD -- one vocabulary on every backend.
'
' Run it:
'     ./gbasic examples/ui/properties.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/ui/properties.bas check
'
' A PART'S OPTIONS AND PROPERTIES ARE gBASIC WORDS: `hint`, `max_length`,
' `secret`, `tip`, `read_only`. Each backend turns a word into its own means --
' on Qt, `hint` is a QLineEdit's placeholderText -- so the same program runs on
' whichever backend draws it (docs/ui_design.md §3). A toolkit's own name is
' refused, and the refusal names the word to write instead.
'
' THERE ARE THREE DOORS AND THEY AGREE ABOUT WHAT A WORD IS:
'   the DECLARATION     `field pin( max_length: 4 )`    -- checked at load time
'   `ui.read`           what it is now
'   `ui.set`            change it, which the declaration cannot: it is written
'                       before anything has happened
'
' A MISSPELLING IS REFUSED BY NAME AT ALL THREE, with the nearest word.
'
' AND THE KIND IS THE WORD'S. `read_only` answers a BOOLEAN; a backend that
' answered the text "false" would read BACKWARDS in `if ui.read(...)`, every
' non-empty string being true in gBASIC.

window form( title: "Properties", width: 440, height: 340 )
    column( margin: 10, spacing: 8 )

        text heading( label: "Set in the declaration" )

        form to edit account
            ' EVERY ONE OF THESE IS A WORD; the backend says what it means
            ' on a field.
            field name(  label: "Name", hint: "as on the card" )
            field pin(   label: "PIN", max_length: 4, secret: true )
            field note(  label: "Reference", tip: "shows on the statement" )
            field locked( label: "Account", read_only: true )
        end form

        row
            button save( text: "Save", enabled: false )
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' A HANDLER THAT CHANGES A PROPERTY, which is what `ui.set` is for: the
    ' declaration is written before anybody has typed anything, so "enable Save
    ' once there is a name" has nowhere else to live.
    on name.changed
        form.set("save", "enabled", len(form.read("name")) > 0)
    end on

    on close.pressed
        ui.stop()
    end on
end window

program main(args)
    account = { name: "", pin: "", note: "", locked: "A-1041" }
    if not ui.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if
    ui.open(form)

    if contains(args, "check") then
        print "-- read back what the DECLARATION set"
        print "pin max_length:    " + string(form.read("pin", "max_length"))
        print "pin secret:        " + string(form.read("pin", "secret"))
        print "name hint:         " + form.read("name", "hint")
        print "note tip:          " + form.read("note", "tip")
        print "locked read_only:  " + string(form.read("locked", "read_only"))

        print "-- the KIND is the word's"
        print "max_length is a:   " + type(form.read("pin", "max_length"))
        print "read_only is a:    " + type(form.read("locked", "read_only"))

        print "-- `ui.set`: what the declaration could not know"
        print "save enabled:      " + string(form.read("save", "enabled"))
        form.write("name", "Ada Lovelace")
        print "after typing:      " + string(form.read("save", "enabled"))

        print "-- a misspelling is refused BY NAME, at both doors"
        print "reading `max_lenght`: " + probe_read("pin", "max_lenght")
        print "setting `max_lenght`: " + probe_set("pin", "max_lenght")
        print "-- and so is Qt's own name for it"
        print "reading `maxLength`: " + probe_read("pin", "maxLength")
        ' AND THE CONTROL, or "it refuses" is satisfied by refusing everything.
        form.set("pin", "max_length", 6)
        print "a REAL property set:  " + string(form.read("pin", "max_length"))
        ui.stop()
        return
    end if
    ui.run()
end program

function probe_read(part, prop)
    on error goto handle
    v = form.read(part, prop)
    return "ANSWERED " + string(v)
handle:
    m = error.message
    error.clear()
    return m
end function

function probe_set(part, prop)
    on error goto handle
    form.set(part, prop, 1)
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function
