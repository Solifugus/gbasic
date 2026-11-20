' SHORTCUTS, ONE OR SEVERAL, AND PRESSING A KEY -- `shortcut: [ ... ]`,
' `ui.shortcuts` and `ui.key`. Under QT_QPA_PLATFORM=offscreen.
'
' ASKED FOR BY gBASIC STUDIO, measured by hand on a real desktop with a US
' layout: its Bigger Text item declared `shortcut: "Ctrl++"`, Ctrl+Shift+=
' triggered it and plain Ctrl+= did NOT, because plain Ctrl+= delivers
' Key_Equal and matches no "Ctrl++". Editors bind zoom-in to both. And no
' accelerator in Studio could be tested at all, because nothing could press
' a key -- which is the kind of thing that would have caught it.
'
' THE LOAD-BEARING CHECK IS A DIFFERENCE: the same press of Ctrl+= does
' nothing against the single shortcut and fires against the list. Either half
' alone passes on a backend that ignores the list, or on a `ui.key` that
' triggers every action regardless of what was pressed.
'
' THE LIMIT, stated rather than tested around: `ui.key` sends the key Qt
' DELIVERS, not the physical keys. On a real keyboard Ctrl+Shift+= reaches
' "Ctrl++" through the platform key mapper; a synthesized event has no layout
' behind it, so "Ctrl++" is how that route is pressed here.

window app( title: "Keys", width: 360, height: 220 )
    menu "View"
        item big( label: "Bigger", shortcut: ["Ctrl++", "Ctrl+="] )
        item small( label: "Smaller", shortcut: "Ctrl+-" )
    end menu
    column( margin: 10 )
        field note( label: "Note" )
        button go( text: "Go" )
    end column
    on big.pressed
        g.big = g.big + 1
    end on
    on small.pressed
        g.small = g.small + 1
    end on
end window

program main()
    g = { checks: 0, bad: 0, big: 0, small: 0 }
    if not ui.can_display() then
        print "NODISPLAY"
        return
    end if
    ui.open(app)

    ' ---- READ BACK: every shortcut, and why a verb is needed -------------
    check("a declared list binds both shortcuts",
          join(ui.shortcuts(app, "big"), "|"), "Ctrl++|Ctrl+=")
    check("while the `shortcut` property reports only the first",
          ui.read(app, "big", "shortcut"), "Ctrl++")
    check("a single text still means one shortcut",
          join(ui.shortcuts(app, "small"), "|"), "Ctrl+-")

    ' ---- PRESSING: each route reaches the item, and only that item -------
    took = ui.key(app, "note", "Ctrl+=")
    check("plain Ctrl+= triggers the item bound to it", g.big, 1)
    check("and ui.key says something took the key", took, true)
    ' ITS CONTROL: a key nothing is bound to and the widget ignores answers
    ' false, or "something took it" would be satisfied by a verb that always
    ' said yes.
    check("a key nothing takes answers false", ui.key(app, "go", "F9"), false)
    took = ui.key(app, "note", "Ctrl++")
    check("Ctrl++ triggers the same item", g.big, 2)
    took = ui.key(app, "note", "Ctrl+-")
    check("Ctrl+- triggers the other item", g.small, 1)
    check("and only that one", g.big, 2)
    check("a shortcut typed nothing into the focused field",
          ui.read(app, "note"), "")

    ' ---- THE DIFFERENCE: Studio's bug, then its fix ----------------------
    x = ui.set(app, "big", "shortcut", "Ctrl++")
    before = g.big
    took = ui.key(app, "note", "Ctrl+=")
    check("with ONE shortcut \"Ctrl++\", plain Ctrl+= does nothing (Studio's bug)",
          g.big - before, 0)
    x = ui.set(app, "big", "shortcut", ["Ctrl++", "Ctrl+="])
    took = ui.key(app, "note", "Ctrl+=")
    check("and with the list set by ui.set it fires", g.big - before, 1)
    check("ui.set's list reads back whole",
          join(ui.shortcuts(app, "big"), "|"), "Ctrl++|Ctrl+=")

    ' ---- ORDINARY TYPING still reaches the widget ------------------------
    took = ui.key(app, "note", "x")
    took = ui.key(app, "note", "Shift+Y")
    check("an unmodified key types into the field", ui.read(app, "note"), "xY")

    ' ---- CLEARING --------------------------------------------------------
    x = ui.set(app, "big", "shortcut", [])
    check("an empty list clears every shortcut", len(ui.shortcuts(app, "big")), 0)
    before = g.big
    took = ui.key(app, "note", "Ctrl+=")
    check("and the key then reaches nothing", g.big - before, 0)

    ' ---- A BUTTON TAKES ONE ----------------------------------------------
    x = ui.set(app, "go", "shortcut", "Ctrl+G")
    check("a button's single shortcut reads back",
          join(ui.shortcuts(app, "go"), "|"), "Ctrl+G")

    ' ---- REFUSALS, each beside its legal neighbour above -----------------
    refused("a misspelled modifier is refused rather than bound to nothing",
            "ui.set: `Ctlr+S` is not a key Qt recognises (modifiers are Ctrl, Shift, Alt and Meta, written like Ctrl+S)",
            1)
    refused("a button refuses several shortcuts",
            "ui.set: a `button` takes one shortcut; only a menu or toolbar `item` takes several",
            2)
    refused("a list for any other property is refused",
            "ui.set: `text` takes one value, not a list; only `shortcut` takes several",
            3)
    refused("a chord is refused by ui.key",
            "ui.key: `Ctrl+K, Ctrl+C` is a chord of several presses; send each with its own ui.key",
            4)
    refused("a key cannot be pressed on a menu item",
            "ui.key: `big` is an `item`, which cannot take a key press -- press the key on a widget and its shortcut reaches the item",
            5)
    refused("an empty key is refused",
            "ui.key: ui.key needs a key to press", 6)
    refused("a non-text entry in a list is refused",
            "ui.set: a list of values must hold only text", 7)
    refused("a field has no shortcut to read",
            "ui.shortcuts: `note` is a `field`, which has no shortcut", 8)

    ui.close(app)
    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program

function refused(what, want, which)
    on error goto next
    if which = 1 then x = ui.set(app, "small", "shortcut", "Ctlr+S")
    if which = 2 then x = ui.set(app, "go", "shortcut", ["Ctrl+1", "Ctrl+2"])
    if which = 3 then x = ui.set(app, "note", "text", ["a", "b"])
    if which = 4 then x = ui.key(app, "note", "Ctrl+K, Ctrl+C")
    if which = 5 then x = ui.key(app, "big", "Ctrl+=")
    if which = 6 then x = ui.key(app, "note", "")
    if which = 7 then x = ui.set(app, "big", "shortcut", ["Ctrl+1", 2])
    if which = 8 then x = ui.shortcuts(app, "note")
    if error then
        check(what, error.message, want)
    else
        check(what, "no error", want)
    end if
    return nothing
end function

function check(what, got, want)
    g.checks = g.checks + 1
    if got = want then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function
