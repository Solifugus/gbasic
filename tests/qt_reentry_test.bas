' HANDLERS THAT CALL BACK INTO THE BACKEND (§0.31), under
' QT_QPA_PLATFORM=offscreen.
'
' WHY THIS FIXTURE EXISTS, and it is the answer to a CLASS rather than to a
' defect: three defects in a row have been paths that only exist once something
' CALLS BACK INTO the backend from inside it, and a fixture that drives the
' verbs in sequence never builds that frame --
'
'   §0.22's use-after-free, which showed up as `sizes` silently never being set;
'   §0.27's reconcile never connecting a BUILT child's handlers, invisible
'     because no perturbation put a handler on a new child;
'   §0.31's own, reported from the Studio bench: `qt.select` on a tree enters
'     the selection handler, the handler calls `qt.refresh`, a tree refresh
'     DELETES EVERY ITEM, and `setSelected` runs on with `this` freed --
'     segfaulting about three runs in five and otherwise answering `unknown`
'     for a row that had just been selected.
'
' SO THE HANDLERS HERE DO WHAT A SHELL'S DO -- select, refresh, read, set --
' rather than counting, which is the gBASIC Studio session's own suggestion and
' is the only shape that builds the frame at all. Its own fixture could not see
' the defect because it selects and then refreshes, and connects no selection
' handler, so nothing re-enters.

function rows_now(extra)
    out = [ { name: "src",      depth: 0 },
            { name: "main.bas", depth: 1 },
            { name: "docs",     depth: 0 } ]
    if extra then
        out = append(out, { name: "README.md", depth: 1 })
    end if
    return out
end function

window shell( title: "Shell", width: 520, height: 400 )
    column
        tree files to show rows
            field name(  label: "File" )
            field depth( depth: true )
        end tree
        table orders to show lines
            field who(   label: "Customer" )
            field total( label: "Total" )
        end table
        source code( line_numbers: true, marks: true )
        text status( label: "idle" )
    end column

    ' A SELECTION HANDLER THAT REDRAWS BLUNTLY -- which the report names as
    ' "the obvious way to write one". It refreshes the very tree whose
    ' selection called it, reads a cell back, writes the editor and marks a
    ' line: four re-entries from inside one Qt call.
    on files.itemSelectionChanged
        g.tree_fired = g.tree_fired + 1
        qt.refresh(shell, "files")
        g.seen = qt.cell(shell, "files", 0, "name")
        qt.write(shell, "code", "row " + string(qt.selected(shell, "files")))
        qt.mark(shell, "code", "section", [1])
        qt.set(shell, "status", "text", "chose " + string(g.tree_fired))
    end on

    ' AND ONE ON THE TABLE, because the two kinds reach DIFFERENT code --
    ' a QTreeWidget's items and a QTableWidget's cells -- and the fix had to
    ' be made in both.
    on orders.itemSelectionChanged
        g.table_fired = g.table_fired + 1
        qt.refresh(shell, "orders")
        qt.set(shell, "status", "text", "order " + string(g.table_fired))
    end on
end window

' AND THE SHARPEST SHAPE OF ALL, found by perturbing the first fix rather than
' by reasoning: a handler that RECONCILES AWAY THE VERY WIDGET whose signal is
' running. §0.27's reconcile DELETES a child the record no longer mentions, so
' this frees the widget mid-call -- a strictly worse version of the item case,
' one level up. MEASURED: with the re-lookup removed it segfaults three runs of
' three, and with it the by-name lookup finds nothing and the rest of the call
' is skipped. It is here because it was NOT here: removing that re-lookup left
' every check in this file green until this window was added.
window vanisher( title: "Vanisher", width: 360, height: 240 )
    column stack
        table orders to show lines
            field who( label: "Customer" )
        end table
    end column

    on orders.itemSelectionChanged
        g.vanish_fired = g.vanish_fired + 1
        qt.refresh(vanisher, "stack",
                   [ { kind: "part", word: "text", name: "gone",
                       options: { label: "the table is gone" } } ])
    end on
end window

program main()
    g = { checks: 0, bad: 0, tree_fired: 0, table_fired: 0, seen: "",
          vanish_fired: 0 }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    rows = rows_now(false)
    lines = [ { who: "Ada",  total: 10 },
              { who: "Grace", total: 20 } ]
    qt.open(shell)

    ' ---- THE SHAPE THAT CRASHED: select, with a handler that refreshes ----
    ' The program SURVIVING is the whole assertion. Before the fix this
    ' segfaulted about three runs in five and corrupted memory otherwise.
    qt.select(shell, "files", 1)
    check("a selection handler that refreshes did not take the program with it",
          g.tree_fired > 0, true)
    qt.select(shell, "files", 2)
    check("and a second select still works", g.tree_fired > 1, true)

    ' THE HANDLER READ THE TREE BACK FROM INSIDE ITSELF, which is what says
    ' the items it was handed are LIVE rather than merely not crashing.
    check("the handler read a live cell from inside the refresh", g.seen,
          "src")

    ' ---- THE SAME ON A TABLE, a different widget and different code -------
    qt.select(shell, "orders", 1)
    check("a table's selection handler survives its own refresh",
          g.table_fired > 0, true)

    ' ---- THE LOOP IS BROKEN RATHER THAN RUN TO THE END OF THE STACK -------
    ' A handler that changes what it is watching calls itself, and every round
    ' genuinely alters the selection -- so this is a feedback loop in the
    ' PROGRAM. What the backend owes is a bound and a sentence, not a crash.
    ' MEASURED: the deepest legitimate nesting anywhere in this tree is TWO.
    check("the recursion was bounded", g.tree_fired < 200, true)

    ' ---- AND EVERYTHING IS STILL USABLE AFTERWARDS -----------------------
    ' The control on the whole fixture: a window that merely SURVIVED a crash
    ' would pass every check above. These say it still works.
    check("the tree still answers", qt.cell(shell, "files", 0, "name"), "src")
    check("the table still answers", qt.cell(shell, "orders", 0, "who"), "Ada")
    check("the editor still holds what a handler wrote",
          contains(qt.read(shell, "code", "plainText"), "row"), true)
    check("the margin still holds its mark",
          string(qt.marks(shell, "code", "section")), "[1]")
    check("and a part a handler set reads back",
          contains(qt.read(shell, "status", "text"), "order"), true)

    ' A REFRESH FROM OUTSIDE A HANDLER IS UNCHANGED, which is the control on
    ' the deferral: with nothing on the stack the items are freed at once as
    ' they always were, and the tree still grows.
    rows = rows_now(true)
    qt.refresh(shell, "files")
    check("a refresh outside a handler still re-reads the subject",
          qt.cell(shell, "files", 3, "name"), "README.md")

    qt.close(shell)

    ' ---- A HANDLER THAT DELETES ITS OWN WIDGET ---------------------------
    lines = [ { who: "Ada" }, { who: "Grace" } ]
    qt.open(vanisher)
    qt.select(vanisher, "orders", 1)
    check("a handler that reconciles away its own widget does not crash",
          g.vanish_fired, 1)
    ' AND THE WIDGET REALLY IS GONE, which is what says the reconcile ran
    ' rather than being skipped -- without it "no crash" is satisfied by a
    ' refresh that did nothing.
    on error goto next
    qt.read(vanisher, "orders", "enabled")
    check("and the widget really was removed",
          contains(error.message, "orders"), true)
    error.clear()
    check("while what replaced it is there",
          qt.read(vanisher, "gone", "text"), "the table is gone")
    qt.close(vanisher)

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
