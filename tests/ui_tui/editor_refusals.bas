' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The editor's refusals on the tui backend (tests/run_ui_tui.sh, tier editor):
' each sentence is Qt's own, word for word, measured against
' QT_QPA_PLATFORM=offscreen when this was written.
window w( title: "x" )
    column
        memo m( label: "notes" )
        source s( line_numbers: true )
    end column
end window
ui.open(w)
' outside a form a memo's label is its first text, as on Qt
print ui.read(w, "m")
x = try ui.mark(w, "m", "error", [1])
print error.message
x = try ui.mark(w, "s", "error", [1])
print error.message
x = try ui.caret_to(w, "m", 3, 1)
print error.message
x = try ui.select_text(w, "m", 1, 4, 1, 2)
print error.message
ui.set(w, "m", "read_only", true)
x = try ui.replace_text(w, "m", { line: 1, column: 1, line2: 1, column2: 2, text: "N" })
print error.message
ui.set(w, "s", "text", "ab")
x = try ui.replace_text(w, "s", [{ line: 1, column: 1, line2: 1, column2: 2, text: "x" }, { line: 1, column: 1, line2: 1, column2: 3, text: "y" }])
print error.message
print ui.read(w, "s")
x = try ui.caret(w, "notthere")
print error.message
c = ui.caret(w, "s")
print c
lines = []
for i = 1 to 50
    lines = append(lines, "line " + string(i))
end for
ui.write(w, "s", join(lines, chr(10)))
x = try ui.gutter_press(w, "s", 45)
print error.message
