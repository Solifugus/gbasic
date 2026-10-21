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

' THE SAME TREE WITH THE DEPTH DECLARED FIRST (§0.33). A separate window
' because the order is the whole subject: `nav` declares it LAST, where the
' drawn column indices and the declared ones agree BY ACCIDENT, so that fixture
' could not see what this one is for. Found by writing an unrelated fixture
' that happened to declare it the other way round -- and the consequence was
' not a wrong caption but a tree that READ BACK AS EMPTY, since `name`
' resolved to column 1 of a one-column widget and the message blamed the ROW.
window nav2( title: "Files", width: 360, height: 300 )
    column
        tree files2 to show rows2
            field depth( depth: true )
            field name(  label: "File" )
        end tree
    end column
end window

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

    ' ---- THE DEPTH DECLARED FIRST (§0.33) -------------------------------
    rows2 = [ { depth: 0, name: "src" }, { depth: 1, name: "eval.c" } ]
    qt.open(nav2)
    ' READ THROUGH A CAUGHT RAISE, because the defect this tier exists for
    ' RAISES rather than answering wrongly -- `name` resolved to a column the
    ' widget does not have, so `qt.cell` reported "has no row 0" and killed
    ' the fixture. A dead fixture reports "exited 1" and hides the assertion
    ' that had already named the problem (the run_discovery lesson), so the
    ' raise becomes the "got" instead.
    on error goto next
    first = qt.cell(nav2, "files2", 0, "name")
    if error then
        first = "RAISED: " + error.message
        error.clear()
    end if
    nested = qt.cell(nav2, "files2", 1, "name")
    if error then
        nested = "RAISED: " + error.message
        error.clear()
    end if
    cap = qt.heading(nav2, "files2", "name")
    if error then
        cap = "RAISED: " + error.message
        error.clear()
    end if
    on error stop
    check("a cell reads back whichever side the depth column was declared on",
          first, "src")
    check("and so does the nested row", nested, "eval.c")
    check("the caption is on the drawn column", cap, "File")
    check("and the depth still reads", string(qt.depth(nav2, "files2", 1)), "1")

    ' AND THE UNDRAWN COLUMN SAYS WHICH OF THE TWO THINGS IS WRONG: "there is
    ' no such column" would be false about a field the author can see in their
    ' own declaration.
    on error goto next
    qt.cell(nav2, "files2", 0, "depth")
    check("asking for the depth column as a cell says it is not drawn",
          error.message,
          "qt.cell: `files2`'s `depth` column carries the depth and is not " +
          "drawn, so it holds no cell -- qt.depth answers it")
    error.clear()
    qt.cell(nav2, "files2", 0, "nope")
    check("and a column nobody declared still gets the plain message",
          error.message, "qt.cell: table `files2` has no `nope` column")
    error.clear()
    on error stop
    qt.close(nav2)

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
