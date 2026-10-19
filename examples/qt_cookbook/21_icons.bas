' ICONS -- a picture on a button or a menu item.
'
' `icon:` IS THE ONE OPTION THAT IS NOT AN ORDINARY PROPERTY. Qt will not turn
' a string into an icon by itself, so this is the single name with a conversion
' behind it -- and it is the same conversion in the declaration and in `qt.set`,
' so the two cannot disagree about what a source is.
'
' THREE SOURCES, TOLD APART BY FACTS. A name beginning `SP_` is one of Qt's own
' eighty-one standard icons, which work with NO desktop icon theme installed --
' the only source that always does, and the right choice for anything that must
' run on a server. A string that names an EXISTING FILE is that file, which is
' what an application shipping its own icons uses. Anything else is a
' freedesktop theme name.
'
' THE TWO KINDS OF FAILURE ARE ANSWERED DIFFERENTLY, because they are different
' in kind. A path that is not there, a file that is not an image and a
' misspelled `SP_` name are all REFUSED -- each is a fact that can be checked
' and the intent is unambiguous. A theme name this machine's theme lacks is a
' WARNING, never a raise: that is a fact about the machine, and refusing would
' stop a program running on a server with no icon theme at all.
'
' WHICH IS WHY `qt.has_icon` EXISTS. A QIcon has no text form -- a button
' carrying a real picture and one carrying nothing both read back as "" -- so
' `qt.read` on an icon refuses and names this instead. It is also how a program
' decides to fall back to a text-only button.

window app( title: "Icons", width: 440, height: 240 )
    menu "File"
        item act_open( label: "Open", icon: "SP_DialogOpenButton" )
        item act_quit( label: "Quit", icon: "SP_DialogCloseButton" )
    end menu
    column( margin: 10, spacing: 8 )
        text hint( label: "Qt's own icons work with no desktop theme." )
        row
            button ok_it(     label: "OK",     icon: "SP_DialogOkButton" )
            button cancel_it( label: "Cancel", icon: "SP_DialogCancelButton" )
            button plain_it(  label: "Plain" )
            stretch()
        end row
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    qt.open(app)

    ' WHETHER ONE ARRIVED. There is no other way to ask: an icon has no text.
    print "OK has an icon:      " + string(qt.has_icon(app, "ok_it"))
    print "Cancel has one:      " + string(qt.has_icon(app, "cancel_it"))
    print "Plain has none:      " + string(qt.has_icon(app, "plain_it"))
    print "the menu item has:   " + string(qt.has_icon(app, "act_open"))

    ' SET ONE AT RUN TIME, through the same conversion.
    qt.set(app, "plain_it", "icon", "SP_DialogApplyButton")
    print "and now Plain has:   " + string(qt.has_icon(app, "plain_it"))

    ' AND TAKE IT AWAY, which a play/pause button needs.
    qt.set(app, "plain_it", "icon", "")
    print "cleared again:       " + string(qt.has_icon(app, "plain_it"))

    ' THE REFUSALS, each naming what was wrong.
    on error goto next
    qt.set(app, "ok_it", "icon", "SP_NotAThing")
    print "a misspelled Qt name: " + error.message
    error.clear()
    qt.set(app, "ok_it", "icon", "/no/such/icon.png")
    print "a path that is not there: " + error.message
    error.clear()
    qt.read(app, "ok_it", "icon")
    print "reading one as text: " + error.message
    error.clear()
    on error stop

    qt.close(app)
end program
