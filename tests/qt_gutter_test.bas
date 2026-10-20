' A MARGIN ON `source` CARRYING A MARK PER LINE (§0.29), under
' QT_QPA_PLATFORM=offscreen.
'
' WHY IT EXISTS: docs/qt_studio_requests.md names two asks that BLOCK a feature
' gBASIC Studio already has, and this is the second (`tree` was the first).
' Studio's gutter is not a ruler -- it carries a `▶` at the start line of every
' runnable SECTION, which is the whole run-a-cell affordance, and an icon at
' every parse DIAGNOSTIC, which is the only thing in that window saying WHERE a
' syntax error is. The user widened it on reading the word: *"I was only
' thinking line numbers and forgot about break points and other things."*
'
' SO THE CATEGORY SET IS DERIVED RATHER THAN CHOSEN. Two were asked for --
' `section` and `break` -- and the other three ARE gBASIC's own diagnostic
' severities, `gb_severity` being exactly {error, warning, note}, so a program
' showing diagnostics has one category per severity instead of flattening
' three into one. Nothing here is invented.
'
' LINE 1 IS THE FIRST LINE, MEASURED RATHER THAN CHOSEN: `source_outline`
' reports `start_line = 1` and a parse diagnostic carries a 1-based line, so
' both of the consumer's own sources are 1-based and the gutter displays
' 1-based -- which means there is NO CONVERSION ANYWHERE. A conversion is
' exactly where an off-by-one would live, and an off-by-one here puts a mark
' one line from the error it is pointing at, which reads as a mark that works.
'
' WHAT THIS FIXTURE CANNOT SEE, and it is most of the increment: whether
' anything is PAINTED. `qt.marks` answers what was SET, so a backend that
' stored the lists and drew nothing satisfies every check below -- which is the
' green-perturbation shape this backend is tested against. A QWidget's pixels
' are not reachable from gBASIC at all, so that claim is made by
' tests/qt/gutter_probe.cpp, which links the REAL backend and counts what the
' painter drew. What IS observable here is the reservation, through the width:
' it follows the DIGIT COUNT, so a thousand-line document reserves more than a
' nine-line one.

window ed( title: "Editor", width: 620, height: 560 )
    column
        source code(  language: "gBASIC", line_numbers: true, marks: true )
        source only_marks( marks: true )
        source plain()
        memo notes()
    end column

    ' A BREAKPOINT YOU CANNOT CLICK TO SET IS DECORATIVE, which is why the
    ' click is in the same increment as the category. `on code.gutter` is the
    ' third EVENT here rather than a signal (after `closed` and `wheel`): the
    ' gutter is a plain child widget and emits nothing.
    on code.gutter
        c = qt.gutter_click()
        append(g.clicks, c.line)
        b = qt.marks(ed, "code", "break")
        if contains(b, c.line) then
            qt.mark(ed, "code", "break", b excluding [c.line])
        else
            qt.mark(ed, "code", "break", concat(b, [c.line]))
        end if
    end on
end window

function lines_of(n)
    out = ""
    i = 1
    while i <= n
        out = out + "line " + string(i) + chr(10)
        i = i + 1
    end while
    return out
end function

program main()
    g = { checks: 0, bad: 0, clicks: [] }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    qt.open(ed)
    qt.write(ed, "code", lines_of(12))
    qt.write(ed, "only_marks", lines_of(12))
    qt.write(ed, "plain", lines_of(12))

    ' ---- WHAT THE MARGIN IS -------------------------------------------
    ' A RECORD OR NOTHING AT ALL, the shape `qt.pane`, `qt.dialog` and
    ' `principal()` all take: `width: 0` would say a margin exists and is
    ' empty where the truth is that this `source` has none.
    gut = qt.gutter(ed, "code")
    check("the margin reports its numbers", string(gut.line_numbers), "true")
    check("and its marks", string(gut.marks), "true")
    check("and reserved a width", gut.width > 0, true)

    only = qt.gutter(ed, "only_marks")
    check("marks alone is marks alone", string(only.line_numbers), "false")
    check("and still carries marks", string(only.marks), "true")

    ' A `source` WITH NEITHER HAS NO MARGIN, answered as an absence rather
    ' than as a margin of width 0.
    check("a plain source has no margin", is_unknown(qt.gutter(ed, "plain")),
          true)

    ' ---- THE RESERVATION IS REAL ---------------------------------------
    ' THE ONE PART A gBASIC CHECK CAN SEE, asserted as a DIFFERENCE: the width
    ' follows the digit count, so a gutter that ignored the count passes every
    ' presence check above and fails this.
    narrow = qt.gutter(ed, "code").width
    qt.write(ed, "code", lines_of(1200))
    wide = qt.gutter(ed, "code").width
    check("a 1200-line document reserves more than a 12-line one",
          wide > narrow, true)
    qt.write(ed, "code", lines_of(12))

    ' ---- A MARK PER LINE, PER CATEGORY ---------------------------------
    qt.mark(ed, "code", "section", [1, 5, 9])
    qt.mark(ed, "code", "error", [3])
    check("a category reads back what was set",
          string(qt.marks(ed, "code", "section")), "[1,5,9]")
    check("and another holds its own", string(qt.marks(ed, "code", "error")),
          "[3]")
    ' THE CATEGORIES ARE INDEPENDENT, without which one list shared by all
    ' five would satisfy both checks above.
    check("a category nobody set is empty",
          string(qt.marks(ed, "code", "break")), "[]")
    check("all five categories exist", string(qt.marks(ed, "code", "note")),
          "[]")
    check("including warning", string(qt.marks(ed, "code", "warning")), "[]")

    ' REPLACE, NEVER ADD -- which is the consumer's own shape, since Studio
    ' recomputes the whole list on every redraw. An add/remove pair would make
    ' it keep a record of what it had already drawn.
    qt.mark(ed, "code", "section", [7])
    check("setting a category REPLACES it",
          string(qt.marks(ed, "code", "section")), "[7]")
    qt.mark(ed, "code", "section", [])
    check("and an empty list clears it",
          string(qt.marks(ed, "code", "section")), "[]")
    check("without disturbing its neighbour",
          string(qt.marks(ed, "code", "error")), "[3]")

    ' A LINE PAST THE END IS KEPT, not filtered: a program may mark line 40 and
    ' then shorten the document, and a read answering only the drawable lines
    ' would disagree with the write for a reason the caller cannot see --
    ' making `qt.mark` then `qt.marks` non-idempotent. The drawing is the
    ' widget's business; the list is the program's.
    qt.mark(ed, "code", "note", [900])
    check("a mark past the end of the document is still the program's",
          string(qt.marks(ed, "code", "note")), "[900]")
    qt.mark(ed, "code", "note", [])

    ' ---- THE CLICK, AND THE BREAKPOINT IT EXISTS FOR -------------------
    ' THE LINES PRESSED ARE ONES THAT ARE ON SCREEN, which this fixture's own
    ' first run established rather than assumed: a press at a line below the
    ' fold is REFUSED, correctly, since a mouse cannot land there -- and with
    ' four widgets sharing the column only the first handful are visible.
    qt.gutter_press(ed, "code", 2)
    check("the handler saw the line that was pressed", string(g.clicks),
          "[2]")
    check("and a breakpoint was set there",
          string(qt.marks(ed, "code", "break")), "[2]")
    qt.gutter_press(ed, "code", 4)
    check("a second press sets a second",
          string(qt.marks(ed, "code", "break")), "[2,4]")
    qt.gutter_press(ed, "code", 2)
    check("and pressing again clears that one",
          string(qt.marks(ed, "code", "break")), "[4]")

    ' THE CONTROL: the filter's reach is the declaration's and nothing wider.
    ' Without this, "the handler fired" is equally satisfied by a filter on
    ' every widget in the window -- which would cost an editor its own caret
    ' on an ordinary press.
    qt.gutter_press(ed, "only_marks", 3)
    check("a source with no handler fires none", string(g.clicks), "[2,4,2]")

    ' AND NOTHING IS BEING HANDLED NOW, which is a different claim from line 0
    ' -- the distinction `qt.wheel` and `principal()` already make.
    check("outside a handler there is no click", is_unknown(qt.gutter_click()),
          true)

    ' ---- THE REFUSALS, each beside its nearest legal neighbour ---------
    on error goto next

    qt.gutter(ed, "notes")
    check("a memo has no margin", contains(error.message,
          "only a `source` has a margin"), true)
    error.clear()

    ' DECLARE THE SPACE, rather than having the text shift sideways under
    ' somebody's eyes the first time a mark appears.
    qt.mark(ed, "plain", "section", [1])
    check("a source with no marks: is told to declare it",
          contains(error.message, "marks: true"), true)
    error.clear()

    ' THE CATEGORY SET IS CLOSED -- and it CAN be, because a category decides a
    ' GLYPH and a PRIORITY and the backend must know both. A leaf's properties
    ' cannot be closed (75 on a button), which is why that set is open and
    ' this one is not.
    qt.mark(ed, "code", "secton", [1])
    check("a near miss names the word", contains(error.message,
          "did you mean `section`"), true)
    error.clear()

    qt.mark(ed, "code", "banana", [1])
    check("and a word nothing is close to gets the list",
          contains(error.message, "`break`"), true)
    error.clear()

    ' LINE 0 IS THE PLAUSIBLE MISTAKE, so it is refused rather than dropped: a
    ' silently dropped mark reports success and draws nothing.
    qt.mark(ed, "code", "break", [0])
    check("line 0 is refused by name", contains(error.message,
          "line numbers start at 1"), true)
    error.clear()

    qt.mark(ed, "code", "break", ["three"])
    check("and so is a line that is not a number",
          contains(error.message, "the lines are numbers"), true)
    error.clear()

    ' THE CONTROL ON ALL SIX: a well-formed call is still accepted, or every
    ' refusal above is satisfied by a verb that refuses everything.
    took = qt.mark(ed, "code", "break", [2])
    check("a well-formed mark is still taken", took, true)
    check("and it took", string(qt.marks(ed, "code", "break")), "[2]")
    error.clear()

    qt.close(ed)
    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program

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
