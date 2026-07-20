load gi
gi.require("Pango", "1.0")
g = gi.new_struct("Pango.GlyphString")
print gi.struct_get(g, "glyphs")
