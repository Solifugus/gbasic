' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The tui backend with no terminal (GBASIC_TUI_SCREEN), driven by the verbs a
' person's keys would reach: ui.key, ui.click, ui.invoke, ui.write. Each line
' printed is something gBASIC observes; tests/run_ui_tui.sh holds them to what
' the keys must have done, written by hand.
window signup( title: "Sign up" )
    group "Account"
        form to edit acct
            field name( label: "Name", hint: "your name", max_length: 8 )
            field pass( label: "Password", secret: true )
            field code( label: "Code", read_only: true )
        end form
        check news( label: "Send me news" )
        check off( label: "Disabled", enabled: false )
    end group
    row
        stretch()
        button cancel( label: "Cancel" )
        button ok( label: "OK", default: true )
    end row
    on ok.pressed
        print "ok pressed"
    end on
    on cancel.pressed
        print "cancel pressed"
    end on
    on news.pressed
        print "news pressed, checked " + ui.read(signup, "news")
    end on
    on news.changed
        print "news changed"
    end on
    on name.changed
        print "name changed to [" + ui.read(signup, "name") + "]"
    end on
    on pass.entered
        print "pass entered"
    end on
    on signup.closed
        print "closed, name " + ui.read(signup, "name")
    end on
end window

function focus_name()
    names = ["name", "pass", "code", "news", "off", "cancel", "ok"]
    found = ""
    for each n in names
        if ui.read(signup, n, "focused") then
            found = n
        end if
    next
    return found
end function

program main(args)
    acct = { name: "Bo", pass: "", code: "X1" }
    print ui.backend()
    ui.open(signup)
    print "focus " + focus_name()
    ' typing at the end of what the form put there
    k = ui.key(signup, "name", "A")
    k = ui.key(signup, "name", "Left")
    k = ui.key(signup, "name", "Backspace")
    print ui.read(signup, "name")
    k = ui.key(signup, "name", "Home")
    k = ui.key(signup, "name", "Delete")
    print ui.read(signup, "name")
    k = ui.key(signup, "name", "End")
    for i = 1 to 9
        k = ui.key(signup, "name", "z")
    next
    print "max_length 8: " + ui.read(signup, "name")
    k = ui.key(signup, "name", "Ctrl+U")
    print "cleared: [" + ui.read(signup, "name") + "]"
    k = ui.key(signup, "name", "J")
    ' a secret shows bullets and reads back its text
    k = ui.key(signup, "pass", "s")
    k = ui.key(signup, "pass", "é")
    print ui.read(signup, "pass") + " shown " + ui.read(signup, "pass", "shown_text")
    print "pass enter handled " + string(ui.key(signup, "pass", "Enter"))
    ' read only takes no typing
    print "code typed handled " + string(ui.key(signup, "code", "q")) + ", still " + ui.read(signup, "code")
    ' Tab order skips the disabled check and wraps
    k = ui.key(signup, "code", "Tab")
    print "after code, Tab: " + focus_name()
    k = ui.key(signup, "news", "Tab")
    print "after news, Tab: " + focus_name()
    k = ui.key(signup, "ok", "Tab")
    print "after ok, Tab: " + focus_name()
    k = ui.key(signup, "name", "Shift+Tab")
    print "before name, Shift+Tab: " + focus_name()
    ' Space toggles a check: pressed, then changed
    k = ui.key(signup, "news", "Space")
    print "news now " + ui.read(signup, "news")
    ' Enter on a check is the window's Enter: the default button
    k = ui.key(signup, "news", "Enter")
    ' a click presses
    ui.click(signup, "cancel")
    ' invoke
    ui.invoke(signup, "news", "press")
    print "disabled check pressed: " + ui.read(signup, "off")
    ui.invoke(signup, "off", "press")
    print "still " + ui.read(signup, "off")
    ' write and read, and the subject back
    ui.write(signup, "code", "Z9")
    edited = signup.subject("acct")
    print edited
    ' visible false leaves the layout; a property read back
    ui.set(signup, "news", "visible", false)
    print "news visible " + string(ui.read(signup, "news", "visible"))
    print "a chord: " + string(try ui.key(signup, "name", "Ctrl+K Ctrl+C"))
    ui.invoke(signup, "signup", "close")
    print "open after close: " + string(ui.is_open(signup))
end program
