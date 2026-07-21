' NAP-5: chaining. A method with an object argument (add_action), a method that
' returns an object (lookup_action), a property read on that returned object, and
' the fully-chained `a.b(x).c` form (method result -> property) in one expression.
load gi
gi.require("Gio", "2.0")

app = gi.new("Gio.Application", "application-id", "org.gbasic.Chain")
act = gi.new("Gio.SimpleAction", "name", "greet")
app.add_action(act)

found = app.lookup_action("greet")
print found.name
print found.enabled

' chained: method-call result feeding directly into a property read
print app.lookup_action("greet").name
