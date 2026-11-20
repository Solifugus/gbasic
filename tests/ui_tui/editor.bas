' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 6 of the tui backend (tests/run_ui_tui.sh, tier editor): a
' `memo` and a `source`, driven by the keys a person would press (ui.key),
' with no terminal (GBASIC_TUI_SCREEN). Each line printed is something gBASIC
' observes, and the screens among them come from GBASIC_TUI_DUMP; the suite
' holds all of it to tests/ui_tui/editor.txt.
window ed( title: "Editor" )
    column
        memo notes( hint: "a few words" )
        source code( line_numbers: true, marks: true )
        row
            field after( label: "After" )
            button ok( label: "OK", default: true )
        end row
    end column
    on ok.pressed
        print "ok pressed"
    end on
    on notes.changed
        g.changes = g.changes + 1
    end on
    on code.gutter
        c = ui.gutter_click()
        print "gutter pressed at line " + string(c.line)
    end on
end window

function snap(name)
    ' a size read lays the window out, which draws it and writes the dump
    w = ui.read(ed, "notes", "width")
    print "--- " + name
    lines = split(read({file}g.dump), chr(10))
    for each line, i in lines
        if i < count(lines) - 1 then
            print "|" + line
        end if
    end for
    return nothing
end function

' each character of `s` typed into `part`
function type_in(part, s)
    i = 0
    while i < len(s)
        k = ui.key(ed, part, mid(s, i, 1))
        i = i + 1
    end while
    return nothing
end function

function press(part, list)
    for each k in list
        h = ui.key(ed, part, k)
    end for
    return nothing
end function

function caret(part)
    c = ui.caret(ed, part)
    out = string(c.line) + ":" + string(c.column)
    if c.selecting then
        out = out + " selecting"
    end if
    return out
end function

function focused()
    for each n in ["notes", "code", "after", "ok"]
        if ui.read(ed, n, "focused") then
            return n
        end if
    end for
    return "none"
end function

program main(args)
    g = { dump: env("GBASIC_TUI_DUMP"), changes: 0 }
    ui.open(ed)
    snap("empty: the hint shows, the margin is one digit")

    ' ---- the memo: typing wraps at a word
    type_in("notes", "The quick brown fox jumps over the lazy dog and keeps on running")
    snap("a memo wraps at the last space that fits")
    print "text [" + ui.read(ed, "notes") + "]"
    print "caret " + caret("notes") + ", changed " + string(g.changes) + " times"

    ' Enter is a new line, and Up keeps the column it came from
    press("notes", ["Enter"])
    type_in("notes", "second")
    print "after Enter and typing: caret " + caret("notes")
    press("notes", ["Up"])
    print "Up from 2:7 lands on the wrapped row's column 7: caret " + caret("notes")
    press("notes", ["Down"])
    print "Down returns: caret " + caret("notes")

    ' a selection by Shift, deleted by Backspace, put back by undo
    press("notes", ["Home", "Shift+End"])
    s = ui.selection(ed, "notes")
    print "Home, Shift+End selects [" + s.text + "] " + string(s.line) + ":" + string(s.column) + "-" + string(s.line2) + ":" + string(s.column2)
    press("notes", ["Backspace"])
    print "Backspace takes it: [" + ui.read(ed, "notes") + "]"
    press("notes", ["Ctrl+Z"])
    print "Ctrl+Z puts it back: " + string(ui.read(ed, "notes") = "The quick brown fox jumps over the lazy dog and keeps on running" + chr(10) + "second")
    press("notes", ["Ctrl+Z"])
    print "Ctrl+Z again takes the typing as one step: [" + replace(ui.read(ed, "notes"), chr(10), "|") + "]"
    press("notes", ["Ctrl+Y"])
    print "Ctrl+Y redoes it: [" + replace(ui.read(ed, "notes"), chr(10), "|") + "]"

    ' Tab is the memo's; Ctrl+Tab and Shift+Tab leave it
    press("notes", ["Ctrl+End", "Tab"])
    print "Tab typed a tab: " + string(ends_with(ui.read(ed, "notes"), "second" + chr(9)))
    press("notes", ["Ctrl+Tab"])
    print "Ctrl+Tab moves on to " + focused()
    press("notes", ["Shift+Tab"])
    print "Shift+Tab from the memo goes back, round the window, to " + focused()

    ' ---- the source: no wrap, the margin, the program's verbs
    lines = []
    i = 1
    while i <= 14
        one = "line " + string(i)
        if i = 9 then
            one = one + " is long enough to run past the right side of the box"
        end if
        lines = append(lines, one)
        i = i + 1
    end while
    ui.write(ed, "code", join(lines, chr(10)))
    snap("a source: numbered, not wrapped")
    ui.caret_to(ed, "code", 9, 50)
    print "caret_to 9:50 gives " + caret("code")
    ' line 8 is short: Up lands at its end, and Down goes back to column 50
    press("code", ["Up"])
    print "Up onto the short line 8: " + caret("code")
    press("code", ["Down"])
    print "Down keeps the column it came from: " + caret("code")
    ui.mark(ed, "code", "error", [9])
    ui.mark(ed, "code", "break", [9, 8])
    ui.mark(ed, "code", "warning", [10])
    ui.set(ed, "code", "whitespace", true)
    snap("scrolled to the caret, marks beside lines 8-10, white space shown")
    print "marks: break " + string(ui.marks(ed, "code", "break")) + ", error " + string(ui.marks(ed, "code", "error"))
    gut = ui.gutter(ed, "code")
    print "gutter " + string(gut.line_numbers) + " " + string(gut.marks) + " " + string(gut.width)
    ui.gutter_press(ed, "code", 9)

    ui.select_text(ed, "code", 2, 1, 3, 5)
    s = ui.selection(ed, "code")
    print "select_text 2:1-3:5 holds [" + replace(s.text, chr(10), "|") + "], caret " + caret("code")
    ui.replace_text(ed, "code", [{ line: 1, column: 1, line2: 1, column2: 5, text: "LINE" }, { line: 3, column: 1, line2: 3, column2: 5, text: "LINE" }])
    print "replace_text, both at once: [" + mid(ui.read(ed, "code"), 0, 20) + "]"
    press("code", ["Ctrl+Z"])
    print "one Ctrl+Z undoes both: [" + mid(ui.read(ed, "code"), 0, 20) + "]"
    ui.highlight_text(ed, "code", [{ line: 4, column: 1, line2: 4, column2: 5 }])
    press("code", ["Ctrl+Home"])
    type_in("code", "new")
    press("code", ["Enter"])
    h = ui.highlights(ed, "code")
    print "a marked range moves with its text, a line down: " + string(h[0].line) + ":" + string(h[0].column) + "-" + string(h[0].line2) + ":" + string(h[0].column2)
    ui.set(ed, "code", "whitespace", false)
    ui.select_text(ed, "code", 2, 3, 3, 2)
    snap("back at the top: the caret where the selection ends, 3:2 (a text dump shows no shading)")

    ui.set(ed, "notes", "read_only", true)
    press("notes", ["x"])
    print "read_only takes no typing: " + string(ends_with(ui.read(ed, "notes"), chr(9)))
    press("notes", ["Enter"])
    print "and Enter there is the window's"
    print "done"
end program
