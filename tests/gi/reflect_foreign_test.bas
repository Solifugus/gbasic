' NAP-9: reflection over FOREIGN/live values. A gobject and a GVariant are
' reflectable (kind + GType name + category) but NOT serializable, and no raw
' pointer is exposed. A record embedding a foreign value is not serializable.
load gi
gi.require("Gio", "2.0")

app = gi.new("Gio.Application", "application-id", "org.gbasic.ReflForeign")
print reflect.kind(app)
print reflect.type(app)
print reflect.category(app)
print reflect.serializable(app)

v = gi.variant_bool(true)
print reflect.kind(v)
print reflect.type(v)
print reflect.category(v)
print reflect.serializable(v)

' a record containing a foreign value is not serializable (recursive predicate)
mixed = { title: "hi", widget: app }
print reflect.serializable(mixed)

info = reflect.inspect(app)
print info.kind + " " + info.type + " " + info.category + " " + info.serializable
