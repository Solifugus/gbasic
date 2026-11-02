' A window's methods are qt's verbs (docs/handle_methods_design.md):
' `app.read(part, prop)` is `qt.read(app, part, prop)`. Each method is asked
' beside the module form, and they must agree.

window app( title: "Methods", width: 240, height: 120 )
    column( margin: 8 )
        field who( label: "Who" )
        button save( text: "Save" )
    end column
    on save.clicked
        g.clicks = g.clicks + 1
    end on
end window

function check(what, got, want)
    if string(got) = string(want) and type(got) = type(want) then
        print "ok " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    g = { clicks: 0 }
    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    o = app.open()
    check("open makes it open", app.is_open(), true)
    w = app.write("who", "Ada Lovelace")
    check("read through a method", app.read("who", "text"), "Ada Lovelace")
    check("the module form agrees", qt.read(app, "who", "text"), app.read("who", "text"))
    s = app.set("save", "enabled", false)
    check("set through a method", qt.read(app, "save", "enabled"), false)
    s = app.set("save", "enabled", true)
    i = app.invoke("save", "click")
    check("invoke fires the handler", g.clicks, 1)
    c = app.close()
    check("close through a method", qt.is_open(app), false)
end program
