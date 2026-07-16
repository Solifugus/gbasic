' Regression: a signal handler that raises must NOT corrupt the outer program's
' error/line/stop state. The marshaller snapshots and restores that state around
' the re-entrant call, surfaces the handler error separately (stderr), and lets
' the outer program continue with clean state (exit 0, stdout below).
load gi
gi.require("Gio", "2.0")

c = gi.new("Gio.Cancellable")

function bad_handler(source)
    z = gi.get(source, "does-not-exist")
    print("handler should not reach here")
end function

gi.connect(c, "cancelled", bad_handler)
gi.call(c, "cancel")
print("outer survived")
y = 2 + 2
print(y)
