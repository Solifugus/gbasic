' A FORM IS A RECORD ON SCREEN. Say so, and nothing populates a widget by hand.
'
' `form to edit cust` takes `cust` from the frame that opens the window and
' fills every part from the field of the same name. `form to show cust` is the
' same thing one way: the parts are read-only and the record never follows.
window customer( title: "Customer", width: 420, height: 240 )
    form to edit cust
        field name(  label: "Name" )
        field limit( label: "Credit limit", type: {USD} )
        check hold(  label: "On hold" )
        memo  notes( label: "" )
    end form
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if

    ' THE SUBJECT LIVES IN THIS FRAME. The form reads it at open.
    cust = { name: "Ada Lovelace", limit: {USD}2500.00, hold: false,
             notes: "Pays on time." }
    qt.open(customer)

    print "the form filled itself:"
    print "  name:    " + qt.read(customer, "name")
    print "  limit:   " + qt.read(customer, "limit")
    print "  on hold: " + string(qt.read(customer, "hold"))

    ' A PERSON EDITS; here the program does the typing.
    qt.write(customer, "limit", {USD}4000.00)
    qt.write(customer, "hold", true)

    ' AND THE RECORD IS FOLDED BACK ON DEMAND -- read at the moment you ask,
    ' never tracked between, so no change can be missed. Each field KEEPS ITS
    ' KIND: `limit` comes back as money because the field declared
    ' `type: {USD}`, not as the text the widget holds.
    back = qt.subject(customer, "cust")
    print "after editing:"
    print "  limit:   " + string(back.limit) + "  (type " + type(back.limit) + ")"
    print "  on hold: " + string(back.hold) + "  (type " + type(back.hold) + ")"
    print "  notes:   " + back.notes
    ' THE CALLER'S OWN RECORD IS UNTOUCHED, because a gBASIC record is a VALUE.
    print "  and `cust` itself still says " + string(cust.limit)
    qt.stop()
end program
