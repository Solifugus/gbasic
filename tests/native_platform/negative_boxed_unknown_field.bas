load gi
gi.require("Gdk", "4.0")
c = gi.new_struct("Gdk.RGBA")
print gi.struct_get(c, "nofield")
