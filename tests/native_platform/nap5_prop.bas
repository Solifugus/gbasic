' NAP-5: GObject property get/set via `.property` sugar. Routes through the same
' machinery as gi.get/gi.set. Underscored gBASIC names reach hyphenated GObject
' properties because g_object_class_find_property canonicalizes `_`<->`-`.
load gi
gi.require("Gio", "2.0")

' string property: write then read back (application_id -> "application-id")
app = gi.new("Gio.Application")
app.application_id = "org.gbasic.Nap5"
print app.application_id

' boolean property on a headless SimpleAction
act = gi.new("Gio.SimpleAction", "name", "greet")
print act.enabled
act.enabled = false
print act.enabled
print act.name

' object-valued property (base_stream -> "base-stream")
base = gi.new("Gio.MemoryInputStream")
buffered = gi.new("Gio.BufferedInputStream", "base-stream", base)
got = buffered.base_stream
print gi.is_a(got, "Gio.MemoryInputStream")
