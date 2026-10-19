' A WINDOW SHOWN MODALLY (§0.18) -- under QT_QPA_PLATFORM=offscreen: a real
' QDialog, a real nested modal loop, no display.
'
' WHAT §0.15 NAMED THIS FOR: `qt.confirm` offers two buttons and that is the
' whole of it, so anything richer -- a form to fill in, a list to choose from --
' is a window with a layout of its own, shown modally.
'
' SELF-CHECKING NOT GOLDEN AND FORCED. Every defect here is an
' ORDINARY-LOOKING RUN: a modal window that did not actually block returns
' before anybody answered and the program carries on with the subject
' unchanged, which looks exactly like somebody pressing Cancel; an accept
' reported as a reject is a form filled in and thrown away; and none of it
' raises.
'
' THE TIER TO READ FIRST IS PARITY. The claim that a modal window IS the same
' window is held by ONE SHARED OPEN PATH in the source, which no behavioural
' check can see -- so the same declaration is opened BOTH WAYS here and every
' observable is required to agree. Without it, "the modal window worked" is
' satisfied by a second open path that drifted.
'
' AND FOUR WAYS IT CAN END, measured in C++ before any of this was written:
'
'   qt.accept(win)   true    `closed` fires   (accept delivers NO close event)
'   qt.reject(win)   false   `closed` fires
'   qt.close(win)    false   `closed` fires
'   the X button      false   `closed` fires
'
' The parenthesis is the whole reason the bookkeeping does not hang off Qt's
' event: `done()` marks a dialog closed, so a later `close()` is a no-op, and a
' backend that waited for the event would leave an accepted window reporting
' itself open for ever.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' EVERY OBSERVABLE WORTH COMPARING, read through the ordinary verbs so the two
' openings are compared on what a program can actually see.
function observe()
    return join([ qt.read(edit, "who"),
                  qt.read(edit, "edit", "windowTitle"),
                  string(qt.read(edit, "save", "enabled")),
                  qt.heading(edit, "lines", 0),
                  qt.cell(edit, "lines", 0, "item"),
                  string(qt.menus(edit)),
                  string(qt.at(edit, "who")) ], "|")
end function

window edit( title: "Customer", width: 360, height: 260 )
    menu "File"
        item close_it( label: "Close" )
    end menu
    column( margin: 10, spacing: 8 )
        cells
            row
                field who( label: "Name" )
            end row
        end cells
        table lines to show rows
            field item(  label: "Item" )
            field money( label: "Amount" )
        end table
        row
            button save(   label: "Save" )
            button cancel( label: "Cancel" )
        end row
    end column

    on save.clicked
        x = qt.accept(edit)
    end on
    on cancel.clicked
        x = qt.reject(edit)
    end on
    on close_it.triggered
        x = qt.close(edit)
    end on
    on edit.closed
        ' THE ONE PLACE A DISMISSED FORM IS READ (§0.17), and it holds for a
        ' modal window with no exception -- which is the whole reason
        ' `qt.open_modal` closes the window before it returns.
        g.typed = qt.read(edit, "who")
        g.closes = g.closes + 1
    end on
end window

program main(args)
    ' `press` IS WHAT TO DRIVE AND `how` IS THE METHOD, two fields rather
    ' than one, because a button is clicked, a menu item is TRIGGERED and a
    ' window is CLOSED -- three different Qt methods, and one overloaded field
    ' would have to guess which from the name.
    g = { typed: "nothing", closes: 0, ticks: 0, press: "save", how: "click",
          normal: "", modal: "", served: "" }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    cust = { who: "Ada Lovelace" }
    rows = [ { item: "Widget", money: "10.00" },
             { item: "Gadget", money: "20.00" } ]

    ' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one -- which is
    ' itself the measurement that matters: a modal window opened from `main`
    ' leaves gBASIC's own sources running, so the program can answer its own
    ' window. §0.15's limit is the other case, a dialog raised from a handler.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks = 2 then
                ' READ EVERYTHING WHILE IT IS UP, which is the modal half of
                ' the parity comparison.
                g.modal = observe()
                y = qt.write(edit, "who", "Grace Hopper")
                z = qt.invoke(edit, g.press, g.how)
            end if
            if g.ticks = 12 then
                ' A BOUND INSIDE THE PROGRAM as well as outside it: an
                ' unanswered modal window is a HANG, and a hang is not a
                ' failure, so the fixture gives up rather than sitting there.
                print "MISMATCH the modal window was never answered"
                w = qt.close(edit)
            end if
        end while
    end watch

    print "-- the same window opened the ordinary way"
    check("open answers true", qt.open(edit), true)
    e = qt.refresh(edit, "lines")
    g.normal = observe()
    check("is_open", qt.is_open(edit), true)
    check("closes it", qt.close(edit), true)
    check("the handler fired once", g.closes, 1)

    print "-- and shown modally"
    g.ticks = 0
    g.typed = "nothing"
    check("open_modal answers true when accepted", qt.open_modal(edit), true)
    check("the handler read what was typed", g.typed, "Grace Hopper")
    check("the handler fired exactly once more", g.closes, 2)
    check("and it is closed afterwards", qt.is_open(edit), false)

    print "-- PARITY: the two openings are the same window"
    ' THE LOAD-BEARING CHECK. The two halves were read through the same
    ' `observe()` at the same point in each window's life, so anything that
    ' differs is the two open paths having drifted.
    check("every observable agrees", g.modal, g.normal)
    check("and it observed something", len(g.modal) > 20, true)

    print "-- rejected"
    g.ticks = 0
    g.press = "cancel"
    check("open_modal answers false", qt.open_modal(edit), false)
    check("the handler still fired", g.closes, 3)
    check("and still read the form", g.typed, "Grace Hopper")

    print "-- ended by a close from inside a handler"
    g.ticks = 0
    g.press = "close_it"
    g.how = "trigger"
    check("a close ends it as a refusal", qt.open_modal(edit), false)
    check("the handler fired once, not twice", g.closes, 4)
    check("and the window is closed", qt.is_open(edit), false)

    print "-- ended the way the window manager ends it"
    ' `close` INVOKED BY NAME ON THE WINDOW is the X button's own Qt path, the
    ' argument `qt.invoke(f, "click")` already makes for a button.
    g.ticks = 0
    g.press = "edit"
    g.how = "close"
    check("the window manager's path also rejects", qt.open_modal(edit), false)
    check("and fired the handler once", g.closes, 5)

    print "-- refusals"
    g.press = "save"
    g.how = "click"
    on error goto accept_not_open
    bad = qt.accept(edit)
    print "MISMATCH accepting a window that is not open was allowed"
    return
accept_not_open:
    check("accepting a window that is not open refuses",
          contains(error.message, "is not open"), true)
    error.clear()

    o = qt.open(edit)
    on error goto accept_not_modal
    bad2 = qt.accept(edit)
    print "MISMATCH accepting an ordinary window was allowed"
    return
accept_not_modal:
    ' THE NARROWNESS. `qt.accept` is for a window shown modally and says so,
    ' naming the verb that would have made it one -- the alternative is Qt's
    ' own "a QWidget emits no..." about a method, which names the wrong thing.
    check("accepting an ordinary window refuses, naming the remedy",
          contains(error.message, "not opened modally"), true)
    check("and names the verb that would have", contains(error.message,
          "qt.open_modal"), true)
    error.clear()
    k = qt.close(edit)

    cancelled = timer.cancel(beat)
end program
