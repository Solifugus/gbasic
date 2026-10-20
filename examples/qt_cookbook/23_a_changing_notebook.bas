' A CHANGING NOTEBOOK -- a container whose set of children changes while the
' window is up, which is what `qt.refresh(win, name, items)` is for.
'
' THE DECLARATION CANNOT CONTAIN A LOOP, so a window whose shape depends on
' data is BUILT as a record (recipe 10) -- and until this verb existed that
' record was frozen the moment the window opened. One tab per open document is
' the obvious case: opening a file has to add a tab, closing one has to take it
' away, and neither should disturb what you have typed in the others.
'
' RECONCILED BY NAME AND WORD. A child whose name and word are unchanged is
' KEPT -- the same widget, so the text, the caret, the scroll position and the
' undo stack all survive -- a new one is BUILT, an absent one is DELETED, and a
' tab's caption is re-read from the record.
'
' WHICH IS WHY A TAB CARRIES A NAME HERE. A tab written in a `window` block
' spends its one slot on the caption, because that is what a person reads; a
' RECORD has no such limit, and the name is what a reconcile keeps the page by.
' Keying on the caption would not do: the caption is the thing that CHANGES,
' since an edited document's tab reads `* notes.bas`.
'
' Run it:
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt_cookbook/23_a_changing_notebook.bas

function page_for(doc)
    return { kind: "container", word: "tab", name: "p" + doc.id,
             label: doc.title,
             items: [ { kind: "part", word: "source", name: "ed" + doc.id,
                        options: { plainText: doc.text } } ] }
end function

function pages_for(docs)
    out = []
    for each d in docs
        out = append(out, page_for(d))
    end for
    return out
end function

function notebook_for(docs)
    return { kind: "window", name: "editor",
             options: { title: "Editor", width: 520, height: 300 },
             items: [ { kind: "container", word: "tabs", name: "book",
                        items: pages_for(docs) } ] }
end function

program main(args)
    open_docs = [ { id: 1, title: "notes.bas", text: "print 1" } ]
    editor = notebook_for(open_docs)

    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        return
    end if

    qt.open(editor)
    print "tabs at the start:   " + join(qt.tabs(editor, "book"), ", ")

    ' SOMEBODY TYPES. This is the state that has to survive everything below.
    x = qt.write(editor, "ed1", "print 1" + chr(10) + "print 2")

    ' A SECOND FILE IS OPENED: append to the list, rebuild the items, reconcile.
    open_docs = append(open_docs, { id: 2, title: "main.bas", text: "run()" })
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after opening one:   " + join(qt.tabs(editor, "book"), ", ")
    print "the reconcile placed " + string(n) + " pages"
    print("and the typing survived: " +
          string(qt.read(editor, "ed1") = "print 1" + chr(10) + "print 2"))

    ' THE FIRST FILE IS EDITED, so its caption gains a marker. Only the caption
    ' moves -- the page is the same widget, which is the whole point.
    open_docs[0].title = "* notes.bas"
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after an edit:       " + join(qt.tabs(editor, "book"), ", ")
    print("still the same page: " +
          string(qt.read(editor, "ed1") = "print 1" + chr(10) + "print 2"))

    ' AND ONE IS CLOSED. An absent child is DELETED, so its parts leave the
    ' window with it -- `qt.read` on a closed document's editor refuses, which
    ' is what stops a program reading off a screen nobody is looking at.
    open_docs = [ open_docs[0] ]
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after closing one:   " + join(qt.tabs(editor, "book"), ", ")
    print "children by name:    " + join(qt.children(editor, "book"), ", ")
    on error goto next
    gone = qt.read(editor, "ed2")
    if error then
        print "the closed one is gone: " + error.message
        error.clear()
    end if
    on error stop

    qt.close(editor)
end program
