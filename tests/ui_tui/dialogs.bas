' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 5 of the tui backend (tests/run_ui_tui.sh, tier dialogs): a
' message box, the file chooser and a modal window, each answered by the
' keys a person would press. With no terminal (GBASIC_TUI_SCREEN), so the
' keys come from a timer: each tick takes the next step, which is a key, a
' word typed, or a snapshot of the screen (GBASIC_TUI_DUMP). The suite holds
' the lines printed, the screens among them, to tests/ui_tui/dialogs.txt.
'
' args[0] is a folder the suite made: notes.txt, c.md, .hidden, and docs/
' holding a.txt and b.md.
window app( title: "Dialogs" )
    column
        text t( label: "under the boxes" )
    end column
end window

window ed( title: "Edit" )
    field who( label: "Name" )
    row
        button ok( label: "OK", default: true )
        button no( label: "Cancel" )
    end row
    on ok.pressed
        x = ui.accept(ed)
    end on
    on no.pressed
        x = ui.reject(ed)
    end on
    on ed.closed
        print "closed, the name read " + ui.read(ed, "who")
    end on
end window

function snap(name)
    print "--- " + name
    lines = split(read({file}g.dump), chr(10))
    for each line, i in lines
        if i < count(lines) - 1 then
            print "|" + line
        end if
    end for
    return nothing
end function

' one step a tick: "key:Down", "type:a.txt" or "snap:name"
function next_step()
    if g.at >= count(g.steps) then
        return nothing
    end if
    s = g.steps[g.at]
    g.at = g.at + 1
    colon = find(s, ":")
    verb = mid(s, 0, colon)
    arg = mid(s, colon + 1, len(s) - colon - 1)
    if verb = "snap" then
        snap(arg)
    else if verb = "key" then
        k = ui.key(g.win, g.part, arg)
    else
        i = 0
        while i < len(arg)
            k = ui.key(g.win, g.part, mid(arg, i, 1))
            i = i + 1
        end while
    end if
    return nothing
end function

function steps(list)
    g.steps = list
    g.at = 0
    return nothing
end function

program main(args)
    g = { dump: env("GBASIC_TUI_DUMP"), steps: [], at: 0, win: app, part: "t" }
    pick = args[0]
    ui.open(app)
    beat = timer.every(0.02)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            next_step()
        end while
    end watch

    steps(["snap:a question, No the default", "key:Enter"])
    print "Enter on the default: " + string(ui.confirm(app, "Delete the three files?"))
    steps(["key:Left", "key:Enter"])
    print "Left, then Enter: " + string(ui.confirm(app, "Delete the three files?"))
    steps(["key:y"])
    print "y: " + string(ui.confirm(app, "Delete?"))
    steps(["key:Escape"])
    print "Escape: " + string(ui.confirm(app, "Delete?"))
    steps(["snap:a statement wraps at its words", "key:Space"])
    print "inform: " + string(ui.inform(app, "Saved. The ledger was written to its folder, and the old copy kept beside it as a backup."))

    steps(["snap:the chooser: folders first, hidden files and other kinds left out",
           "key:Down", "key:Enter", "snap:into docs, where the filter shows a.txt only",
           "key:Tab", "key:Tab", "key:Right", "snap:the second filter, All, shows b.md",
           "key:Shift+Tab", "type:a.txt", "key:Enter"])
    got = ui.choose_file(app, { title: "Open a note", folder: pick, filter: ["Text (*.txt)", "All (*)"] })
    print "chosen " + replace(got, pick, "PICK")

    steps(["key:Down", "key:Down", "snap:moving onto a file names it", "key:Enter"])
    got = ui.choose_file(app, { folder: pick, filter: "Text (*.txt)" })
    print "chosen by the list " + replace(got, pick, "PICK")

    steps(["type:nope.txt", "key:Enter", "snap:a name that is not there is refused in the box", "key:Escape"])
    got = ui.choose_file(app, { folder: pick })
    print "then Escape: unknown " + string(is_unknown(got))

    steps(["key:Enter"])
    got = ui.choose_save(app, { folder: pick, name: "out.txt" })
    print "saved as " + replace(got, pick, "PICK")

    steps(["key:Down", "key:Enter", "key:Tab", "key:Enter"])
    got = ui.choose_folder(app, { folder: pick })
    print "folder " + replace(got, pick, "PICK")

    g.win = ed
    g.part = "who"
    steps(["type:Zed", "snap:a modal window is a box over the window under it", "key:Enter"])
    print "modal, Enter on the default button: " + string(ui.open_modal(ed))
    steps(["type:Amy", "key:Escape"])
    print "modal, Escape: " + string(ui.open_modal(ed))

    cancelled = timer.cancel(beat)
    print "done"
end program
