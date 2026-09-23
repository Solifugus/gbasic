' An INSTANCE method reached through the static path must be refused, not
' called with a NULL receiver. This is the control that keeps "Type.method now
' resolves" from meaning "anything spelled with three parts is invoked".
load gi
gi.require("Gio", "2.0")
print(gi.invoke("Gio.File.get_path"))
