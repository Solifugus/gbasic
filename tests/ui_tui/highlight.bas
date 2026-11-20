' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 7 of the tui backend (tests/run_ui_tui.sh, tier highlight): a
' `source` coloured from a KDE syntax definition, with no terminal
' (GBASIC_TUI_SCREEN). The colours come from GBASIC_TUI_STYLES, each cell's
' `fg:bg:attrs`; the suite holds every line printed to
' tests/ui_tui/highlight.txt and works the important ones by hand.
window hl( title: "Highlight" )
    column
        source code( language: "gBASIC", theme: "Breeze Light" )
        source blocks( language: "Blocks Test", theme: "dark" )
    end column
end window

' the screen as text and as styles, one list each, after a frame
function frame()
    w = ui.read(hl, "code", "width")
    return { text: split(read({file}g.dump), chr(10)),
             styles: split(read({file}g.styles), chr(10)) }
end function

' the style of the cell at column `col` of the row holding `needle`, both
' 0-based; the row's runs are `count:fg:bg:attrs`
function style_at(f, needle, offset)
    for each line, r in f.text
        at = find(line, needle)
        if not is_nothing(at) then
            runs = split(f.styles[r], " ")
            col = len(left(line, at)) + offset
            x = 0
            i = 1
            while i < count(runs)
                parts = split(runs[i], ":")
                n = number(parts[0])
                if col < x + n then
                    return parts[1] + ":" + parts[2] + ":" + parts[3]
                end if
                x = x + n
                i = i + 1
            end while
        end if
    end for
    return "none"
end function

program main(args)
    g = { dump: env("GBASIC_TUI_DUMP"), styles: env("GBASIC_TUI_STYLES") }
    ui.open(hl)
    ui.write(hl, "code", "print \"hi\" ' greet" + chr(10) + "x = 42")
    ui.write(hl, "blocks", "begin /* one" + chr(10) + "two */ end" + chr(10) + "\"s\" begin")
    f = frame()
    print "--- the screen"
    for each line, i in f.text
        if i < count(f.text) - 1 then
            print "|" + line
        end if
    end for
    print "--- its colours"
    for each line, i in f.styles
        if i < count(f.styles) - 1 then
            print line
        end if
    end for
    print "--- gBASIC, Breeze Light"
    print "keyword print: " + style_at(f, "print", 0)
    print "string \"hi\": " + style_at(f, "\"hi\"", 1)
    print "comment: " + style_at(f, "' greet", 2)
    print "number 42: " + style_at(f, "42", 0)
    print "operator =: " + style_at(f, "x = 42", 2)
    print "plain x: " + style_at(f, "x = 42", 0)
    h = ui.highlighting(hl, "code")
    print "highlighting: " + h.language + ", " + h.theme
    c = ui.read(hl, "code", "colors")
    print "colors: " + c.base + " " + c.fg
    print "--- Blocks Test, dark"
    print "the comment runs on: two -> " + style_at(f, "two */", 0)
    print "and ends: end -> " + style_at(f, "*/ end", 3)
    print "a keyword before it: begin -> " + style_at(f, "begin /*", 0)
    print "the third line plain again: begin -> " + style_at(f, "\"s\" begin", 4)
    print "highlighting: " + ui.highlighting(hl, "blocks").theme
    ' AN EDIT ABOVE CHANGES WHAT IS BELOW: the opener taken away, line 2 is
    ' no longer in a comment -- the states from line 1 down are forgotten
    r = ui.replace_text(hl, "blocks", [ { line: 1, column: 7, line2: 1, column2: 9, text: "" } ])
    f = frame()
    print "opener removed: two -> " + style_at(f, "two */", 0)
    print "and end is a keyword: " + style_at(f, "*/ end", 3)
    r = ui.invoke(hl, "blocks", "undo")
    f = frame()
    print "undone: two -> " + style_at(f, "two */", 0)
    ' A LONG TEXT, the comment opened on line 1 and the caret sent to the
    ' last of 3000 lines: every state between is worked out, once
    big = "/* open" + chr(10)
    for i = 1 to 2998
        big = big + "line " + string(i) + chr(10)
    end for
    big = big + "end"
    ui.write(hl, "blocks", big)
    r = ui.caret_to(hl, "blocks", 3000, 1)
    f = frame()
    print "3000 lines down, still in the comment: end -> " + style_at(f, "end", 0)
    ui.close(hl)
end program
