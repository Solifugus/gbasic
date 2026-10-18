' A MENU BAR, A THEME, AND A LIST -- what a small application actually looks
' like, and the three increments of 2026-10-18 together.
'
' Run it:
'     ./gbasic examples/qt/notes.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/notes.bas   (no display)
'
' AN AUTHOR WRITES NO COORDINATE AND NO MENU BAR. `menu "File"` is not laid
' out -- a bar belongs to the window, so it is not in the row/column tree at
' all -- and the items under it are addressed by name exactly as a button is.

window notes( title: "Notes", width: 540, height: 360 )

    menu "File"
        item new_note(  label: "New",  shortcut: "Ctrl+N" )
        separator()
        item quit_app(  label: "Quit", shortcut: "Ctrl+Q" )
    end menu

    menu "View"
        ' AN ITEM IS A QAction, so `checkable:` reaches it through the same
        ' by-name door a widget property goes through. Nothing in the backend
        ' has a table of QAction properties; it asks Qt.
        item show_done( label: "Show finished", checkable: true, checked: true )
    end menu

    column( margin: 10, spacing: 8 )
        table note_list to show notes_held
            field title( label: "Note" )
            field tag(   label: "Tag" )
        end table

        group "Selected"
            form to show current
                field title( label: "Note" )
                field tag(   label: "Tag" )
            end form
        end group
    end column

    on note_list.itemSelectionChanged
        at = qt.selected(notes, "note_list")
        if not is_unknown(at) then
            picked = notes_held[at]
            x = qt.write(notes, "title", picked.title)
            x = qt.write(notes, "tag", picked.tag)
        end if
    end on

    on new_note.triggered
        print "new note"
    end on

    on show_done.toggled
        print "show finished is now " + string(qt.read(notes, "show_done", "checked"))
    end on

    on quit_app.triggered
        qt.stop()
    end on
end window

program main()
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
    else
        ' ONE STYLESHEET FOR THE WHOLE APPLICATION. A theme is TEXT, so a real
        ' program loads one with `qt.theme(read({file}"dark.qss"))` -- the
        ' platform supplies the sheet and the language supplies the file.
        ' A widget's own `styleSheet:` still wins; Qt resolves the cascade.
        qt.theme("QWidget { font-size: 11pt; }" +
                 "QHeaderView::section { background: #eceff4; padding: 4px; }" +
                 "QGroupBox { font-weight: bold; }")

        notes_held = [ { title: "Ring the bank", tag: "money" },
                       { title: "Read Lovelace", tag: "books" },
                       { title: "Fix the gate",  tag: "house" } ]
        current = {}

        qt.open(notes)
        qt.select(notes, "note_list", 0)
        qt.run()
    end if
end program
