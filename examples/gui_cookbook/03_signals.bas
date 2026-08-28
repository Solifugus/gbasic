' Wiring a widget to a handler. `gi.connect(widget, signal, handler)` takes a
' FUNCTION VALUE -- the bare name, not a call -- and returns a handler id you
' can pass to `gi.disconnect` later.
'
' Handlers cannot be triggered from gBASIC (the bridge exposes no signal-emit),
' so what a test can assert is the WIRING and anything the handler does when
' the loop drives it. Recipe 02 shows a handler actually running, via a
' timeout, which is the same mechanism a click uses.
load gi
load gtk

gtk.require()
gtk.init()

seen = { clicks: 0, last: "" }

function on_add(btn)
    seen.clicks = seen.clicks + 1
    seen.last = "add"
    return 0
end function

add = gtk.button("Add")
id = gi.connect(add, "clicked", on_add)

print "connected, handler id is a number : " + string(is_number(id))
print "id is non-zero                    : " + string(id != 0)

' `gtk.connect` is the same call under a friendlier name.
del = gtk.button("Delete")
id2 = gtk.connect(del, "clicked", on_add)
print "gtk.connect works the same        : " + string(is_number(id2))

' Disconnecting is by id, on the same widget.
gi.disconnect(add, id)
print "disconnected without error        : true"

' A handler is an ordinary function value: it can be stored and passed.
handlers = { add: on_add }
print "a handler stores in a record      : " + string(type(handlers.add))
