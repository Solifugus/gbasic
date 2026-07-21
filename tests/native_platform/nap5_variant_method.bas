' NAP-5: boxed/GVariant receiver method dispatch. NAP-4 wired GVariant as a boxed
' value but noted its methods needed receiver dispatch (this phase). A GVariant is
' introspected as the GLib.Variant struct; methods resolve via struct info and the
' instance pointer is the GVariant itself. This is the evidence the phase unlocks
' more than GObjects.
load gi
gi.require("Gio", "2.0")

v = gi.variant_bool(true)
print v.get_type_string()
print v.get_boolean()

n = gi.variant_int32(42)
print n.get_type_string()
print n.get_int32()
