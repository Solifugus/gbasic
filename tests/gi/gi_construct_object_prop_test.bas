' Construct-time property whose VALUE is itself a GObject (Item 1, note 3). This is
' the same conversion the Gtk.ApplicationWindow "application" example depends on,
' proven headlessly: Gio.BufferedInputStream's "base-stream" is a construct-only
' GInputStream property. We pass one wrapper in and read the same canonical wrapper
' back out (qdata identity), and confirm the returned object's type.
load gi
gi.require("Gio", "2.0")

base = gi.new("Gio.MemoryInputStream")
buffered = gi.new("Gio.BufferedInputStream", "base-stream", base)

got = gi.get(buffered, "base-stream")
print(gi.is_a(got, "Gio.InputStream"))
print(got = base)
