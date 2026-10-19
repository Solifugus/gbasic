' THE WHEEL (§0.21) -- `on <part>.wheel`, `qt.wheel`, `qt.swallow`, and
' `qt.scroll` to drive it. Under QT_QPA_PLATFORM=offscreen: real QWheelEvents,
' no display.
'
' DERIVED FROM gBASIC STUDIO'S OWN SOURCE, which uses a
' Gtk.EventControllerScroll for exactly ONE thing: Ctrl + wheel zooms the
' editor. Its handler reads `dy`, calls one function and returns TRUE to mean
' handled -- and those three things are the whole requirement.
'
' MEASURED FIRST, AND TWO RESULTS SHRANK IT. (1) A synthetic QWheelEvent IS
' DELIVERED (a plain one moved the scrollbar 92 -> 95), which is better than
' GTK: Studio records that its scroll and key controllers are its only
' untested controls BECAUSE `Gtk.EventControllerScroll` has no emit, so this
' closes an exception it had to sanction. (2) THE ZOOM NEEDS NOTHING NEW --
' `font` is Q_PROPERTY 27 of QWidget and reads back as TEXT
' ("monospace,9,-1,5,400,..."), so a program splits it, changes the number and
' writes it back, which is asserted below rather than taken on trust. That is
' why there is no `qt.zoom`: `zoomIn`/`zoomOut` take a defaulted argument so
' `qt.invoke` cannot reach them, and nothing needs them to.
'
' AND WHY `qt.swallow` IS NOT OPTIONAL. Measured, Ctrl+wheel scrolls a
' QPlainTextEdit FASTER than a plain one (95 -> 111 against 92 -> 95), so a
' handler that zooms without claiming the event leaves the editor jumping
' about while it resizes -- and swallowing genuinely stops it (102 -> 102),
' while declining leaves plain scrolling intact (102 -> 105). Both halves are
' asserted, because "it swallowed" alone passes on a handler that swallows
' everything, which costs the editor its scrolling.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is an ordinary-looking
' run. A handler that never fires is a wheel that scrolls normally; one that
' swallows everything is an editor that will not scroll; a `dy` read with the
' wrong sign zooms the wrong way; and a modifier never reported makes every
' wheel a Ctrl+wheel. None of them raises anything.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "Wheel", width: 480, height: 360 )
    column
        source code( plainText: "one" )
        table grid to show rows
            field name( label: "Name" )
        end table
        field plain()
    end column

    on code.wheel
        ev = qt.wheel()
        g.seen = g.seen + 1
        g.dy = ev.dy
        g.ctrl = ev.ctrl
        g.shift = ev.shift
        ' STUDIO'S OWN RULE: only with Ctrl. Without the modifier this must do
        ' NOTHING and leave the event alone, or the editor stops scrolling.
        if ev.ctrl then
            ' THE ZOOM, IN ARITHMETIC. `font` reads back as text, so the size
            ' is a number in a list -- no verb needed, which is what the
            ' measurement said and what this asserts.
            f = split(qt.read(app, "code", "font"), ",")
            bigger = 2
            if ev.dy < 0 then
                bigger = 0 - 2
            end if
            qt.set(app, "code", "font", f[0] + "," + string(number(f[1]) + bigger))
            g.zoomed = g.zoomed + 1
            ' CLAIMED, so the view does not also scroll.
            qt.swallow()
        end if
    end on

    on grid.wheel
        g.grid_seen = g.grid_seen + 1
    end on
end window

' A WINDOW WITH NO WHEEL HANDLER -- the control. Without it "the handler fires"
' is satisfied by a backend that installs a filter on every widget, and
' "swallowing works" by one that swallows everywhere.
window plain_win( title: "Plain", width: 300, height: 200 )
    column
        source untouched( plainText: "two" )
    end column
end window

program main()
    rows = [{ name: "a" }, { name: "b" }, { name: "c" }]
    g = { seen: 0, dy: 0, ctrl: false, shift: false, zoomed: 0, grid_seen: 0,
          rows: rows }

    qt.open(app)

    ' ---- the event reaches the handler, with its direction and modifiers ----
    before = number(split(qt.read(app, "code", "font"), ",")[1])
    unclaimed = qt.scroll(app, "code", 1)
    check("a plain wheel reached the handler", g.seen, 1)
    ' THE OTHER HALF: a handler that declined must leave the event alone, or
    ' installing one costs the editor its scrolling. Measured, swallowing
    ' really does stop it (102 -> 102) and declining really does not
    ' (102 -> 105), so these two checks are a difference and not a pair of
    ' facts about one code path.
    check("and a declined wheel was NOT claimed", unclaimed, false)
    check("one notch up is a positive dy", g.dy > 0, true)
    check("and no modifier was reported", g.ctrl, false)
    ' STUDIO'S RULE HELD: a plain wheel changed nothing.
    check("a plain wheel did not zoom", g.zoomed, 0)
    check("so the font is unchanged",
          number(split(qt.read(app, "code", "font"), ",")[1]), before)

    qt.scroll(app, "code", -1)
    check("one notch down is a negative dy", g.dy < 0, true)
    check("still no zoom", g.zoomed, 0)

    ' ---- Ctrl + wheel, which is the whole of what Studio needs -------------
    ' `qt.scroll` ANSWERS WHETHER ANYTHING CLAIMED THE EVENT, which is what
    ' makes swallowing observable at all: a QPlainTextEdit has no scroll
    ' position a program can read (verticalScrollBar is a method, not a
    ' property), so without this the one thing `qt.swallow` does would be
    ' unassertable. Same shape as `qt.popup` answering whether it showed.
    claimed = qt.scroll(app, "code", 1, "ctrl")
    check("ctrl was reported", g.ctrl, true)
    check("and it zoomed", g.zoomed, 1)
    check("and the event was claimed", claimed, true)
    ' THE ZOOM IS ASSERTED ON THE FONT, not on the counter: a handler that ran
    ' and set nothing reads identically.
    check("the font grew by two points",
          number(split(qt.read(app, "code", "font"), ",")[1]), before + 2)
    qt.scroll(app, "code", -1, "ctrl")
    check("and shrank back", 
          number(split(qt.read(app, "code", "font"), ",")[1]), before)

    ' ---- a second modifier, so `ctrl` is read rather than assumed ----------
    qt.scroll(app, "code", 1, "shift")
    check("shift is reported", g.shift, true)
    check("and shift is not ctrl", g.ctrl, false)
    check("so it did not zoom", g.zoomed, 2)

    ' ---- a table answers too, through its own viewport ---------------------
    qt.scroll(app, "grid", -1)
    check("a table's wheel reached its handler", g.grid_seen, 1)

    ' ---- refusals, each beside its nearest legal neighbour -----------------
    on error goto next
    qt.wheel()
    check("qt.wheel outside a wheel handler is refused", error.message,
          "qt.wheel: no wheel event is being handled")
    error.clear()

    qt.swallow()
    check("and so is qt.swallow", error.message,
          "qt.swallow: no event is being handled")
    error.clear()

    qt.scroll(app, "nothing_here", 1)
    check("an unknown part is refused", error.message,
          "qt.scroll: this window has no `nothing_here`")
    error.clear()

    qt.scroll(app, "code", 1, "meta")
    check("an unknown modifier is refused by name", error.message,
          "qt.scroll: `meta` is not a modifier -- use ctrl, shift or alt")
    error.clear()

    qt.scroll(app, "code", 0)
    check("zero notches is refused", error.message,
          "qt.scroll: a wheel turns by at least one notch")
    error.clear()
    on error stop

    qt.close(app)

    ' ---- the control: no handler, so nothing is filtered -------------------
    ' Without this, "the handler fires" is satisfied by a backend that installs
    ' a filter on every widget there is, and "it swallows" by one that swallows
    ' everywhere -- which would cost every `source` and `table` in the language
    ' its ordinary scrolling, silently. A part in a window that declared NO
    ' wheel handler must leave every event unclaimed.
    qt.open(plain_win)
    check("a part with no handler claims nothing",
          qt.scroll(plain_win, "untouched", -3), false)
    qt.close(plain_win)

    ' AND THE SAME WITHIN ONE WINDOW: `plain` declared no wheel handler while
    ' `code` in the same window did, so a filter installed per WINDOW rather
    ' than per declared handler is caught here and nowhere else.
    qt.open(app)
    check("an undeclared part in a window that has one claims nothing",
          qt.scroll(app, "plain", 1), false)
    qt.close(app)
end program
