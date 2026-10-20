' A `tree` -- rows with a DEPTH, shown as a tree (§0.28). Under
' QT_QPA_PLATFORM=offscreen.
'
' WHY IT EXISTS, and the user asked for it by name: gBASIC Studio's file
' browser is the left third of its window and the thing a person looks at most
' after the code. Converted to this vocabulary it had to become a `table` --
' the only widget here whose row COUNT can change without rebuilding the
' window -- and that is measurably worse to look at: a header row it does not
' want, grid lines, FLAT rows with the structure faked as two spaces per level
' inside the string, and no expand arrows, so Studio draws its own glyphs.
'
' MEASURED FIRST, AND IT MOVED THE WORK RATHER THAN THE ESTIMATE: ten of the
' things that request calls missing chrome are already Q_PROPERTYs on a
' QTreeWidget -- headerHidden, rootIsDecorated, itemsExpandable, indentation,
' animated, alternatingRowColors, uniformRowHeights, expandsOnDoubleClick,
' allColumnsShowFocus, sortingEnabled -- and `showGrid` does not exist on a
' tree at all, so there are no grid lines to turn off. So what `tree` adds is
' THE WIDGET AND THE DEPTH, not the look; the look was always one `qt.set`
' away and this fixture asserts that rather than assuming it.
'
' THE SUBJECT IS A FLAT LIST WITH A DEPTH, which is what a program producing a
' tree already has (`filetree.flatten` produces it flat, and Studio's row
' array is shared by its renderer and its click dispatcher AS ONE ARRAY -- a
' deliberate invariant, since deriving rows twice desynchronises the moment the
' filesystem changes between a render and a click). A nested subject is the
' other shape and is not what the consumer can supply today.
'
' AND AN INDEX MEANS THE DECLARED ROWS, NEVER WHAT IS ON SCREEN. Measured: a
' collapsed child is NOT `isHidden()`, so Qt's own NotHidden iterator still
' counts every row and there is no free "visible index". An index into the
' rows the program handed over is the one number that stays meaningful however
' the tree is expanded, so that is what `qt.selected` answers and what
' `qt.cell` and `qt.depth` take.

function rows_now(extra)
    out = [ { name: "src",      depth: 0 },
            { name: "main.bas", depth: 1 },
            { name: "lib",      depth: 1 },
            { name: "util.bas", depth: 2 },
            { name: "docs",     depth: 0 } ]
    if extra then
        out = append(out, { name: "README.md", depth: 1 })
    end if
    return out
end function

window nav( title: "Files", width: 360, height: 300 )
    column
        tree files to show rows
            field name(  label: "File" )
            field depth( depth: true )
        end tree
    end column
end window

program main()
    g = { checks: 0, bad: 0 }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    rows = rows_now(false)
    qt.open(nav)

    ' ---- THE ROWS ARE THE SUBJECT'S, INDEXED AS DECLARED ----------------
    check("the first row reads back", qt.cell(nav, "files", 0, "name"), "src")
    check("and so does a nested one", qt.cell(nav, "files", 3, "name"),
          "util.bas")

    ' ---- THE DEPTH REACHED THE WIDGET, which is the oracle: a populate that
    ' ignored it would answer 0 for every row and every check above would
    ' still pass, the names being right either way.
    check("a top-level row is at depth 0", string(qt.depth(nav, "files", 0)),
          "0")
    check("its child is at depth 1", string(qt.depth(nav, "files", 1)), "1")
    check("and the grandchild at depth 2", string(qt.depth(nav, "files", 3)),
          "2")
    check("the second top-level row is back at 0",
          string(qt.depth(nav, "files", 4)), "0")

    ' ---- THE DEPTH COLUMN IS DECLARED AND NOT DRAWN --------------------
    ' A perturbation that DREW it changed nothing else: cells are read by field
    ' name and the first heading is still `File`, so the only observable
    ' difference is that a second column exists. Asking for one is therefore
    ' the check -- and it has to be a REFUSAL, which is the honest answer for a
    ' column that is not there.
    check("the drawn column is the first", qt.heading(nav, "files", 0), "File")
    on error goto next
    qt.heading(nav, "files", 1)
    check("and there is no second column, the depth not being drawn",
          error.message, "qt.heading: `files` has no column 1")
    error.clear()
    on error stop

    ' ---- SELECTION IS AN INDEX INTO THE DECLARED ROWS -------------------
    check("nothing is chosen to begin with",
          string(is_unknown(qt.selected(nav, "files"))), "true")
    qt.select(nav, "files", 3)
    check("choosing a nested row answers its row index",
          string(qt.selected(nav, "files")), "3")

    ' ---- A DIRECTORY EXPANDS: the count changes and the tree follows ----
    ' `qt.refresh` re-reads the subject, the same verb a table uses -- which is
    ' the whole reason Studio's browser had to be a table in the first place.
    rows = rows_now(true)
    drew = qt.refresh(nav, "files")
    check("the refresh drew every row", string(drew), "6")
    check("the new row is there", qt.cell(nav, "files", 5, "name"),
          "README.md")
    check("at the depth the record gives it",
          string(qt.depth(nav, "files", 5)), "1")

    ' ---- THE CHROME WAS ALWAYS ONE `qt.set` AWAY ------------------------
    ' The correction to the request that asked for this: these are properties,
    ' so a tree looks right without any new vocabulary at all.
    qt.set(nav, "files", "headerHidden", true)
    check("the header can be hidden by name",
          string(qt.read(nav, "files", "headerHidden")), "true")
    qt.set(nav, "files", "alternatingRowColors", true)
    check("and the rows can alternate",
          string(qt.read(nav, "files", "alternatingRowColors")), "true")
    check("indentation is readable too",
          string(number(qt.read(nav, "files", "indentation")) > 0), "true")

    ' ---- THE REFUSALS --------------------------------------------------
    on error goto next
    qt.depth(nav, "files", 99)
    check("a row that is not there is refused by number",
          error.message, "qt.depth: `files` has no row 99")
    error.clear()
    qt.depth(nav, "name", 0)
    check("and a part that is not a tree says so",
          error.message, "qt.depth: `name` is not a tree")
    error.clear()
    on error stop

    qt.close(nav)
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
