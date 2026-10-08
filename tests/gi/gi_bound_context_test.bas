' A BOUND HANDLER CARRIES ITS CONTEXT THROUGH `gi.connect` AND THE EVENT SOURCES.
'
' WHY IT MATTERS: GTK calls a handler with the arguments the SIGNAL defines and
' nothing else, so anything else a handler needs had to be SMUGGLED -- through a
' program-global registry, or inside an object the signal happens to hand back.
' `stdlib/datagrid.bas` does both: a `_DATAGRID` global for per-grid state, and a
' linear search through the grid's columns on every bind to work out which column
' the firing factory belongs to.
'
' `gi.connect` REFUSED a bound function until 0.6.0 rather than drop its context in
' silence -- the right refusal, and the gap this closes. It was found the hard way:
' the first version of `bound` accepted one here and the context arrived as
' `nothing` with exit 0, which is the failure `bound` exists not to introduce.
'
' TESTED ON Gio AND NOT Gtk, so it runs in the ordinary headless gate: a
' `Gio.Application` needs no display and the closure machinery is the same one GTK
' signals go through. The difference is the machine, not the mechanism -- the same
' argument the class-statics tier makes.
load gi
gi.require("Gio", "2.0")

seen = []

' TWO HANDLERS, ONE FUNCTION, DIFFERENT CONTEXTS -- which is the property the
' feature exists for and the one a registry cannot give without an id to look up.
function record_it(app, ctx)
    append(seen, ctx.label + ":" + string(ctx.n))
end function

' A PLAIN HANDLER MUST BE UNAFFECTED: it declares exactly the signal's parameters
' and no context is appended.
function plain_handler(app)
    append(seen, "plain")
end function

' --- signals -----------------------------------------------------------------
a1 = gi.new("Gio.Application", "application-id", "org.gbasic.BoundA")
gi.call(a1, "register", nothing)
gi.connect(a1, "activate", bound(record_it, { label: "first", n: 1 }))
gi.call(a1, "activate")

a2 = gi.new("Gio.Application", "application-id", "org.gbasic.BoundB")
gi.call(a2, "register", nothing)
gi.connect(a2, "activate", bound(record_it, { label: "second", n: 2 }))
gi.call(a2, "activate")

a3 = gi.new("Gio.Application", "application-id", "org.gbasic.BoundC")
gi.call(a3, "register", nothing)
gi.connect(a3, "activate", plain_handler)
gi.call(a3, "activate")

print("signals: " + join(seen, " "))

' --- the same object, two connections, two contexts --------------------------
' The context belongs to the CONNECTION, not to the object or the function, so one
' object may carry two handlers that differ only in what they were bound with.
pair = []
function note(app, ctx)
    append(pair, ctx.tag)
end function

a4 = gi.new("Gio.Application", "application-id", "org.gbasic.BoundD")
gi.call(a4, "register", nothing)
gi.connect(a4, "activate", bound(note, { tag: "x" }))
gi.connect(a4, "activate", bound(note, { tag: "y" }))
gi.call(a4, "activate")
print("two on one object: " + join(sort(pair), " "))

' --- the context is a COPY, not a borrow -------------------------------------
' A closure outlives the statement that created it -- that is what connecting a
' signal means -- so the context cannot be a reference into the caller's frame.
' Mutating the original afterwards must not change what the handler sees.
held = []
function hold(app, ctx)
    append(held, ctx.v)
end function

ctxval = { v: "before" }
a5 = gi.new("Gio.Application", "application-id", "org.gbasic.BoundE")
gi.call(a5, "register", nothing)
gi.connect(a5, "activate", bound(hold, ctxval))
ctxval.v = "after"
gi.call(a5, "activate")
print("context is a copy: " + join(held, " "))

' --- event sources ------------------------------------------------------------
' `gi.timeout` and `gi.idle` go through the SAME closure struct, which is why the
' filling is shared rather than written twice: while only one path had a context
' the two disagreed about what a bound handler receives.
log = []

function tick(ctx)
    append(log, "tick-" + ctx.label)
    if count(log) >= 3 then
        gi.quit()
        return false
    end if
    return true
end function

function once(ctx)
    append(log, "idle-" + ctx.label)
    return false
end function

function bare()
    append(log, "bare")
    return false
end function

gi.idle(bound(once, { label: "A" }))
gi.idle(bare)
gi.timeout(5, bound(tick, { label: "B" }))
gi.main()
print("sources: " + join(log, " "))
