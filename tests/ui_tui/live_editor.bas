' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 6 in a real terminal (tests/run_ui_tui.sh, tier live_editor): a
' memo typed into, pasted into (bracketed paste, its line break kept),
' pressed with the mouse and dragged across, by the bytes a terminal sends.
' Shift+Tab leaves the memo for Done, which reports and ends the run.
window app( title: "Live" )
    column
        memo m()
        button done( label: "Done" )
    end column
    on done.pressed
        c = ui.caret(app, "m")
        print to error "text " + replace(ui.read(app, "m"), chr(10), "|") + " caret " + string(c.line) + ":" + string(c.column)
        ui.stop()
    end on
end window

print to error "backend " + ui.backend()
ui.open(app)
ui.run()
