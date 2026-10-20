' A CONTAINER WHOSE CHILDREN CHANGE (§0.27) -- `qt.refresh(win, name, items)`,
' the third form of a verb that already means "re-read what this shows".
' Under QT_QPA_PLATFORM=offscreen.
'
' WHY IT EXISTS, and it was asked for by the consumer with measurements:
' `qt.refresh` answered this for a `table` (§0.11, written for gBASIC Studio's
' file tree by name) and for a form's subject, and for no other container --
' so Studio's notebook, which is one tab per open document, could not gain or
' lose a tab while the window was up. The record `qt.open` reads can hold any
' number of anything (§0.9), and every one of them was frozen the moment the
' window opened.
'
' THE LOAD-BEARING CHECK IS THAT A KEPT CHILD KEEPS WHAT WAS TYPED INTO IT.
' Measured in C++ first, because the whole verb rests on it: a QPlainTextEdit
' taken out of a QTabWidget and put back keeps its text AND its undo stack
' (`isUndoAvailable` 1), and so does one removed from a box layout and
' re-inserted. Every other check here -- the count, the order, the captions --
' passes on an implementation that REBUILDS everything, which is the naive
' version and the one worth being able to tell apart.
'
' AND "ABSENT" MEANS DELETED, NOT REMOVED FROM THE LAYOUT. Also measured: a
' child dropped from a box layout and not deleted stays PARENTED AND VISIBLE,
' floating at its old geometry -- so a reconcile that merely unhooked it would
' leave the window showing a widget the record no longer mentions, with
' nothing raised. §0.17's rule for a dismissed window, one level in.

' NOTE `plainText` AND NOT `text`: a QPlainTextEdit has no `text` property,
' which §0.14 already records, and the by-name route refuses an unknown
' property rather than storing it -- so the wrong spelling fails loudly here
' rather than leaving an editor mysteriously empty.
function doc_tab(id, caption, body)
    ' A TAB CARRIES A NAME HERE, which the declaration's grammar cannot spell
    ' (its one slot is the caption, §0.8) and the RECORD can. That is what a
    ' reconcile needs: the caption is the thing that CHANGES -- Studio's tab
    ' labels carry a `* ` unsaved marker -- so keying on it would rebuild the
    ' editor on the first keystroke, which is the cost the verb exists to
    ' remove.
    return { kind: "container", word: "tab", name: "t" + id, label: caption,
             items: [ { kind: "part", word: "source", name: "ed" + id,
                        options: { plainText: body } } ] }
end function

function notebook(tabs)
    return { kind: "window", name: "shelf",
             options: { title: "Book", width: 520, height: 320 },
             items: [ { kind: "container", word: "tabs", name: "pages",
                        items: tabs } ] }
end function

program main()
    g = { checks: 0, bad: 0 }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    win = notebook([ doc_tab("1", "one.bas", "x = 1"),
                     doc_tab("2", "two.bas", "y = 2") ])
    check("the built notebook passes qt.check", string(qt.check(win)), "true")

    qt.open(win)
    check("it opens with two tabs", string(qt.read(win, "pages", "count")), "2")
    check("and the captions are the record's",
          join(qt.tabs(win, "pages"), "|"), "one.bas|two.bas")

    ' ---- TYPE INTO THE FIRST ONE, which is what must survive -------------
    typed = qt.write(win, "ed1", "x = 1" + chr(10) + "print x")
    check("text written into the first editor",
          left(qt.read(win, "ed1"), 5), "x = 1")

    ' ---- GROW: a third document opens -----------------------------------
    n = qt.refresh(win, "pages", [ doc_tab("1", "one.bas", "IGNORED"),
                                  doc_tab("2", "two.bas", "y = 2"),
                                  doc_tab("3", "three.bas", "z = 3") ])
    check("the reconcile reports three children", string(n), "3")
    check("and the widget agrees", string(qt.read(win, "pages", "count")), "3")
    ' THE CHECK THE WHOLE VERB IS FOR. A rebuild would answer the record's
    ' "IGNORED" -- which is deliberately NOT what the editor holds -- and a
    ' reconcile answers what the person typed.
    check("a KEPT editor still holds what was typed, not the record",
          replace(qt.read(win, "ed1"), chr(10), "/"), "x = 1/print x")
    check("and the new one was built from the record",
          qt.read(win, "ed3"), "z = 3")

    ' ---- A CAPTION FOLLOWS ITS DATA, without rebuilding the page --------
    n = qt.refresh(win, "pages", [ doc_tab("1", "* one.bas", "IGNORED"),
                                  doc_tab("2", "two.bas", "y = 2"),
                                  doc_tab("3", "three.bas", "z = 3") ])
    check("the caption follows the record",
          join(qt.tabs(win, "pages"), "|"), "* one.bas|two.bas|three.bas")
    check("and the editor under it is STILL the same widget",
          replace(qt.read(win, "ed1"), chr(10), "/"), "x = 1/print x")

    ' ---- REORDER: the record's order is the tab order -------------------
    n = qt.refresh(win, "pages", [ doc_tab("3", "three.bas", "z = 3"),
                                  doc_tab("1", "* one.bas", "IGNORED"),
                                  doc_tab("2", "two.bas", "y = 2") ])
    check("the order follows the record",
          join(qt.tabs(win, "pages"), "|"), "three.bas|* one.bas|two.bas")
    check("and a moved child is still the same widget",
          replace(qt.read(win, "ed1"), chr(10), "/"), "x = 1/print x")

    ' ---- SHRINK: a document closes --------------------------------------
    ' THE TIER A GROW-ONLY SUITE WOULD MISS: a reconcile that appends rather
    ' than rewrites passes every check above.
    n = qt.refresh(win, "pages", [ doc_tab("1", "* one.bas", "IGNORED"),
                                  doc_tab("3", "three.bas", "z = 3") ])
    check("the reconcile reports two children", string(n), "2")
    check("and the widget agrees", string(qt.read(win, "pages", "count")), "2")
    check("the captions are the two that remain",
          join(qt.tabs(win, "pages"), "|"), "* one.bas|three.bas")
    check("the survivor still holds its text",
          replace(qt.read(win, "ed1"), chr(10), "/"), "x = 1/print x")

    ' AND THE ABSENT ONE IS GONE RATHER THAN HIDDEN, which is the measured
    ' trap: a widget merely unhooked from a layout stays visible.
    on error goto next
    x = qt.read(win, "ed2")
    check("a dropped child's name is gone from the window",
          error.message, "this window has no part named `ed2`")
    error.clear()
    on error stop

    qt.close(win)

    ' ---- A BOX, NOT A TAB SET -------------------------------------------
    ' THE OTHER HALF OF THE ADMITTED SET, and it was missing until a
    ' perturbation showed it: everything above is a QTabWidget, which orders by
    ' tab index, where a `column` orders by layout position and goes through
    ' removeWidget/insertWidget instead -- two different pieces of code, and
    ' only one of them was being asked anything.
    box = { kind: "window", name: "panel",
            options: { title: "Panel", width: 300, height: 240 },
            items: [ { kind: "container", word: "column", name: "stack",
                       items: [ { kind: "part", word: "field", name: "a",
                                  options: { text: "first" } },
                                { kind: "part", word: "field", name: "b",
                                  options: { text: "second" } } ] } ] }
    qt.open(box)
    typed2 = qt.write(box, "a", "TYPED")
    n = qt.refresh(box, "stack", [ { kind: "part", word: "field", name: "b",
                                     options: { text: "second" } },
                                   { kind: "part", word: "field", name: "a",
                                     options: { text: "IGNORED" } },
                                   { kind: "part", word: "field", name: "c",
                                     options: { text: "third" } } ])
    check("a column reconciles too", string(n), "3")
    check("and a kept field keeps what was typed", qt.read(box, "a"), "TYPED")
    check("and the new one came from the record", qt.read(box, "c"), "third")
    ' THE ORDER IS THE RECORD'S, asked of the CONTAINER rather than of any
    ' record of where things were put. `qt.at` cannot answer this -- it reads
    ' coordinates in a `cells` grid only -- which is why `qt.children` exists:
    ' a box's order was observable through nothing at all.
    check("the box's order is the record's",
          join(qt.children(box, "stack"), "|"), "b|a|c")

    ' ---- THE SAME NAME FOR A DIFFERENT WORD IS NOT THE SAME CHILD --------
    ' Without this the WORD half of the key is unasserted, which a
    ' perturbation proved: ignoring the word left every check above green,
    ' because nothing here had ever changed one.
    n = qt.refresh(box, "stack", [ { kind: "part", word: "check", name: "a",
                                     options: { label: "now a box" } } ])
    check("a name whose word changed is rebuilt", string(n), "1")
    check("and it is the widget the record now says",
          string(qt.read(box, "a", "checkable")), "true")
    qt.close(box)

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
