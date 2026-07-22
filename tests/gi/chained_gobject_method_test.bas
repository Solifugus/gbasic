' NAP dogfood: chained method receivers over GObject values. A gobject held in a
' record field or returned from a method can receive a method call directly.
load gi
gi.require("Gio", "2.0")

' field-access receiver: holder.cancellable.method()
holder = { cancellable: gi.new("Gio.Cancellable") }
print holder.cancellable.is_cancelled()
holder.cancellable.cancel()
print holder.cancellable.is_cancelled()

' call-result receiver: app.lookup_action("greet").get_name()
app = gi.new("Gio.Application", "application-id", "org.gbasic.ChainG")
act = gi.new("Gio.SimpleAction", "name", "greet")
app.add_action(act)
print app.lookup_action("greet").get_name()

' nested field then method on a gobject in a record
state = { app: app }
print state.app.get_application_id()
