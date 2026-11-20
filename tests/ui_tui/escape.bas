' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Escape then Tab leaves an editor forward (tests/run_ui_tui.sh, tier
' editor): by the keys a person presses, with no terminal. Each line printed
' is what gBASIC observes; the suite holds them to lines written by hand.
window w( title: "Escape" )
    column
        field before( label: "Before" )
        memo notes()
        field after( label: "After" )
    end column
end window

window box( title: "Box" )
    memo say()
    row
        button ok( label: "OK", default: true )
    end row
    on ok.pressed
        x = ui.accept(box)
    end on
end window

function focused()
    for each n in ["before", "notes", "after"]
        if ui.read(w, n, "focused") then
            return n
        end if
    end for
    return "none"
end function

function press_keys(list)
    for each k in list
        h = ui.key(w, "notes", k)
    end for
    return nothing
end function

program main()
    ui.open(w)
    h = ui.key(w, "before", "Tab")
    print "in: " + focused()
    press_keys(["Tab"])
    print "Tab types a tab: [" + ui.read(w, "notes") + "] " + focused()
    press_keys(["Escape"])
    print "Escape alone stays: " + focused() + ", text [" + ui.read(w, "notes") + "]"
    press_keys(["Tab"])
    print "Escape then Tab leaves forward: " + focused() + ", text [" + ui.read(w, "notes") + "]"
    h = ui.key(w, "after", "Shift+Tab")
    print "Shift+Tab back in: " + focused()
    press_keys(["Escape", "a", "Tab"])
    print "Escape, a key, then Tab types it: [" + ui.read(w, "notes") + "] " + focused()
    press_keys(["Alt+Tab"])
    print "Alt+Tab, a quick Escape then Tab, leaves: " + focused()
    h = ui.key(w, "after", "Shift+Tab")
    press_keys(["Escape"])
    r = ui.click(w, "before")
    r = ui.click(w, "notes")
    press_keys(["Tab"])
    print "a press between forgets the Escape: [" + ui.read(w, "notes") + "] " + focused()
    ui.close(w)
    ' IN A MODAL WINDOW ESCAPE STILL REJECTS, from inside its editor
    g = { n: 0 }
    beat = timer.every(0.02)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.n = g.n + 1
            if g.n = 2 then
                h = ui.key(box, "say", "Escape")
            end if
        end while
    end watch
    print "a modal, Escape in its memo: " + string(ui.open_modal(box))
    c = timer.cancel(beat)
end program
