' Namespace-level free functions (Item 2): gi.invoke("Ns.function", args...).
' GLib.markup_escape_text is headless and deterministic; it exercises free-function
' resolution, a borrowed UTF8 arg, an int64 (gssize) length arg, and a transfer-full
' UTF8 return (freed after marshalling). length -1 means the text is nul-terminated.
load gi
gi.require("GLib", "2.0")

print(gi.invoke("GLib.markup_escape_text", "<hi> & 'bye'", -1))
print(gi.invoke("GLib.markup_escape_text", "plain", -1))
