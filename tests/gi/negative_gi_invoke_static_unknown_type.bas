' Over a three-part name "unknown" is ambiguous: a misspelled TYPE and a
' misspelled FUNCTION want different fixes, so the message says which half is
' wrong. This is the type half.
load gi
gi.require("Gio", "2.0")
print(gi.invoke("Gio.Nope.thing"))
