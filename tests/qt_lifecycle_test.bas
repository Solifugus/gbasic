' THE WINDOW LIFECYCLE (§0.17) -- under QT_QPA_PLATFORM=offscreen: real
' windows, a real QCloseEvent, no display.
'
' SELF-CHECKING NOT GOLDEN AND FORCED. Every defect here is an ORDINARY-LOOKING
' RUN: a window the person dismissed that goes on answering `qt.read` is a
' program reading values off a screen nobody is looking at, and it raises
' NOTHING -- so a golden would record the stale answer as expected and defend
' it. That is not a hypothetical: Qt does not set `WA_DeleteOnClose`, so the
' widget really is still there, still holding what it was showing, and
' measured, still readable. The close is bookkeeping precisely because Qt's own
' behaviour is the wrong answer here.
'
' THE TIER TO READ FIRST IS `plain`, the window with NO `closed` HANDLER. The
' bookkeeping has to be unconditional and the handler is the optional part, so
' a filter installed only where `on app.closed` appears would leave exactly the
' programs that declared no handler reading a dismissed window -- and nothing
' else in THIS file could see it, every other window here having a handler.
' Worse than a wrong answer: the registry would then hold an entry pointing at
' a window that had been deleted, which is how the perturbation for it reports
' exit 139 rather than a mismatch.
'
' HONEST OVERLAP RATHER THAN A SECOND INDEPENDENT CHECK: the growth fixture's
' window has no handler either, so that tier catches the same perturbation. The
' claim is about this file, and it is the `plain` window that makes it.
'
' AND THE HANDLER FIRES BEFORE THE REGISTRY FORGETS, which is what makes `on
' <win>.closed` load-bearing rather than a notification -- it is the one place
' a program can still see what the person typed. Asserted BOTH ways: the
' handler reads the live widget, and the same read AFTER the close refuses.
' Either half alone passes on an implementation that got the order backwards.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "Lifecycle", width: 320, height: 160 )
    column( margin: 8 )
        form to edit cust
            field who( label: "Customer" )
        end form
    end column

    ' THE WINDOW ADDRESSES ITSELF. `app` is the window's own name and the
    ' backend registers the root widget under it in the same by-name table
    ' every part uses, so this is the ordinary handler shape rather than a new
    ' one -- and a part called `app` is refused for exactly that reason.
    on app.closed
        g.fired = g.fired + 1
        ' STILL READABLE, STILL OPEN. The next thing that happens is the
        ' registry forgetting this window, so this is the last moment either
        ' is true.
        g.saved = qt.read(app, "who")
        g.still_open = qt.is_open(app)
        g.subject = qt.subject(app, "cust").who
        if g.mode = "reenter" then
            on error goto reentered
            again = qt.close(app)
            g.reentry = "ACCEPTED"
            return nothing
reentered:
            g.reentry = error.message
            error.clear()
        end if
    end on
end window

' NO `closed` HANDLER AT ALL, deliberately: see the header. Its only job is to
' be closed and then asked about.
window plain( title: "Plain", width: 200, height: 100 )
    column
        field note( label: "Note" )
    end column
end window

window second( title: "Second", width: 200, height: 100 )
    column
        field memo_of( label: "Memo" )
    end column
end window

program main(args)
    g = { fired: 0, saved: "nothing", still_open: false, subject: "nothing",
          mode: "plain", reentry: "nothing" }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    cust = { who: "Ada Lovelace" }

    print "-- a window is not open until it is"
    check("is_open before any open", qt.is_open(app), false)
    check("open answers true", qt.open(app), true)
    check("is_open after open", qt.is_open(app), true)

    print "-- the window itself is addressable, which nothing could do before"
    ' `qt.invoke`/`qt.read`/`qt.set` reached PARTS only, so there was no way to
    ' read or set a window's own title, raise it, or close it.
    check("its own title reads back", qt.read(app, "app", "windowTitle"),
          "Lifecycle")
    x = qt.set(app, "app", "windowTitle", "Renamed")
    check("and can be set", qt.read(app, "app", "windowTitle"), "Renamed")
    ' THE KIND COMES FROM THE QVariant, not from the caller -- the rule the
    ' by-name reader already follows, and it matters here because the string
    ' "false" is TRUE in this language.
    check("a boolean property arrives as a boolean",
          type(qt.read(app, "app", "visible")), "boolean")
    check("and the window is visible", qt.read(app, "app", "visible"), true)

    print "-- somebody types, then the window closes"
    y = qt.write(app, "who", "Grace Hopper")
    check("nothing has fired yet", g.fired, 0)
    check("close answers true", qt.close(app), true)

    print "-- the handler ran once, before the registry forgot"
    check("fired exactly once", g.fired, 1)
    check("it read the LIVE widget", g.saved, "Grace Hopper")
    check("qt.subject in the handler folds the typed value", g.subject,
          "Grace Hopper")
    check("and the window still reported open inside it", g.still_open, true)

    print "-- and afterwards the window is gone, not merely hidden"
    check("is_open after close", qt.is_open(app), false)
    on error goto read_refused
    stale = qt.read(app, "who")
    print "MISMATCH a closed window answered qt.read: " + string(stale)
    return
read_refused:
    check("reading a part refuses, naming the window",
          contains(error.message, "window `app` is not open"), true)
    error.clear()

    on error goto subject_refused
    s = qt.subject(app, "cust")
    print "MISMATCH a closed window answered qt.subject"
    return
subject_refused:
    ' THE REFUSAL IS THE DESIGN, not a gap: the capture would be the
    ' pre-close values, so answering would be a wrong answer rather than a
    ' stale one. `on app.closed` is where a program reads the form.
    check("qt.subject refuses too", contains(error.message, "is not open"),
          true)
    error.clear()

    on error goto close_refused
    again = qt.close(app)
    print "MISMATCH closing a closed window was accepted"
    return
close_refused:
    check("closing it again refuses",
          contains(error.message, "nothing to close"), true)
    error.clear()

    print "-- reopening builds a FRESH window, not the old one back"
    check("reopen answers true", qt.open(app), true)
    check("is_open again", qt.is_open(app), true)
    ' THE SHARP ONE: what was typed before the close is gone and the form has
    ' re-read its subject. A close that merely hid the window would answer
    ' "Grace Hopper" here, and every check above would still pass.
    check("the typed value did not survive", qt.read(app, "who"),
          "Ada Lovelace")
    check("the title is the declaration's again",
          qt.read(app, "app", "windowTitle"), "Lifecycle")
    z = qt.close(app)

    print "-- a window with NO `closed` handler is still forgotten"
    ' THE LOAD-BEARING TIER. The bookkeeping is unconditional; the handler is
    ' the optional part. Nothing else in this file can see the difference.
    check("plain opens", qt.open(plain), true)
    check("plain is open", qt.is_open(plain), true)
    check("plain closes", qt.close(plain), true)
    check("plain is NOT open", qt.is_open(plain), false)
    on error goto plain_refused
    p = qt.read(plain, "note")
    print "MISMATCH a handler-less closed window answered qt.read"
    return
plain_refused:
    check("and reading it refuses",
          contains(error.message, "window `plain` is not open"), true)
    error.clear()

    print "-- two windows are independent"
    a1 = qt.open(app)
    s1 = qt.open(second)
    check("both open", string(qt.is_open(app)) + " " + string(qt.is_open(second)),
          "true true")
    c1 = qt.close(second)
    check("closing one leaves the other",
          string(qt.is_open(app)) + " " + string(qt.is_open(second)),
          "true false")

    print "-- the window manager's own path, driven"
    ' THE X BUTTON CANNOT BE CLICKED HERE (offscreen has no window manager) and
    ' it reaches `QWidget::close()` through Qt's platform layer, which is the
    ' same thing an invoked `close` reaches -- so this drives the real event
    ' rather than the verb, the argument `qt.invoke(f, "click")` already makes
    ' for a button.
    g.fired = 0
    g.saved = "nothing"
    w2 = qt.write(app, "who", "Katherine Johnson")
    check("invoking `close` by name is accepted",
          qt.invoke(app, "app", "close"), true)
    check("it fired the handler too", g.fired, 1)
    check("with the live value", g.saved, "Katherine Johnson")
    check("and the window was forgotten", qt.is_open(app), false)

    print "-- a handler that closes its own window again"
    ' THE HANDLER RUNS WHILE THE WINDOW IS STILL REGISTERED, which is what
    ' makes it useful and also what lets it arrive back here. One guard, set
    ' before the handler runs, so the answer is a sentence rather than a double
    ' free.
    g.mode = "reenter"
    g.fired = 0
    r1 = qt.open(app)
    r2 = qt.close(app)
    check("the re-entrant close refuses",
          contains(g.reentry, "already closing"), true)
    check("the handler still fired exactly once", g.fired, 1)
    check("and the window is closed", qt.is_open(app), false)

    ' AND THE SAME THING ON THE OTHER PATH, which is the REALISTIC one: the
    ' person clicks X and the handler tries to tidy up by closing the window.
    ' A perturbation is what put this here -- with the backend's guard removed
    ' the case above stayed GREEN, because `qt.close` nulls the widget before
    ' closing and that alone stops a second entry. Only a close that did NOT
    ' come from `qt.close` reaches the guard, and without it the recursion
    ' deletes the window's nodes underneath the outer call.
    g.fired = 0
    g.reentry = "nothing"
    r3 = qt.open(app)
    r4 = qt.invoke(app, "app", "close")
    check("re-entry from an event-driven close refuses too",
          contains(g.reentry, "already closing"), true)
    check("and fired exactly once there as well", g.fired, 1)
    check("and that window is closed too", qt.is_open(app), false)
    g.mode = "plain"

    qt.stop()
end program
