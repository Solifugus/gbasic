' ... and this is the function half: the type resolves, the function on it does
' not. Paired with the case above, so neither message can drift into the other.
load gi
gi.require("Gio", "2.0")
print(gi.invoke("Gio.File.nope"))
