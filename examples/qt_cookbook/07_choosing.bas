' CHOOSING ONE OF A LIST. A `field` means typing an id the author has to
' remember; a `choice` shows the names and the program keeps the ids.
'
' ITS ITEMS COME FROM DATA, so a declared `options:` list is for the fixed case
' -- a direction, a status -- and `qt.options` is for anything that moves. BAG's
' area editor needs both in one window, which is the measurement that said
' support both rather than one.
window move_form( title: "Choosing", width: 420, height: 220 )
    form to edit plan
        ' FIXED, so it is typed out.
        choice heading( label: "Direction",
                        options: ["north", "south", "east", "west"] )
        ' NOT FIXED: filled from data below.
        choice leads_to( label: "Leads to" )
    end form
end window

program main(args)
    ' THE DATA AND ITS IDS. The dropdown shows names; the record keeps ids,
    ' which is the translation a `choice` exists to make.
    areas = [ { id: 1, name: "The Cellar" },
              { id: 2, name: "The Garden" },
              { id: 3, name: "The Tunnel" } ]

    if not qt.can_display() then
        print "no display"
        return
    end if
    plan = { heading: "north", leads_to: "" }
    qt.open(move_form)

    ' A SENTINEL FIRST, so "nowhere" is choosable rather than absent.
    names = ["(nowhere)"]
    for each a in areas
        names = append(names, a.name)
    end for
    shown = qt.options(move_form, "leads_to", names)
    print "the dropdown offers " + string(shown) + " choices"

    ' CHOOSING BY NAME. `qt.write` on a choice REFUSES a name that is not in
    ' the list rather than silently doing nothing, which is what Qt's own
    ' setCurrentText does on an uneditable box.
    qt.write(move_form, "leads_to", "The Garden")
    print "the box shows:      " + qt.read(move_form, "leads_to")

    ' AND BACK TO AN ID, which is the program's half of the translation.
    chosen = qt.read(move_form, "leads_to")
    goes_to = 0
    for each a in areas
        if a.name = chosen then
            goes_to = a.id
        end if
    end for
    print "which is area id:   " + string(goes_to)
    print "the direction is:   " + qt.read(move_form, "heading")
    qt.stop()
end program
