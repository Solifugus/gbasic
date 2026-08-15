' NAP-1: GI boxed/struct values (VALUE_GBOXED) + struct field get/set.
' Uses Gdk.RGBA — a display-free POD boxed type (four gdouble fields), so this
' runs headless. Boxed values are REFERENCE-semantic refcounted handles (like the
' gi gobject wrapper): reading a variable shares the same underlying box, so
' gi.struct_set mutates in place and is visible through every alias. This mirrors
' how gi.set mutates a shared gobject, and is required because gBASIC copies values
' on every identifier read.
load gi
gi.require("Gdk", "4.0")

c = gi.new_struct("Gdk.RGBA")
gi.struct_set(c, "red", 0.25)
gi.struct_set(c, "green", 0.5)
gi.struct_set(c, "blue", 0.75)
gi.struct_set(c, "alpha", 1)
print gi.struct_get(c, "red")
print gi.struct_get(c, "green")
print gi.struct_get(c, "blue")
print gi.struct_get(c, "alpha")
print type(c)

' Reference semantics: d aliases c's box, so mutating d is visible through c.
'
' The value reads back as 0.8999999761581421, not 0.9, and that is correct: a
' GdkRGBA component is a C `float`, so 0.9 is stored at single precision and
' widened back to a double on read. `print` showed 0.9 until PLAT-NUMFMT
' (2026-08-14) because it rendered six significant digits and rounded the
' difference away. The lossy round-trip was always happening; only its
' visibility changed.
d = c
gi.struct_set(d, "red", 0.9)
print gi.struct_get(c, "red")
print gi.struct_get(d, "red")

' Repeated construction to expose alloc/free lifetime bugs under valgrind: each
' new handle drops the previous t's refcount to zero and g_boxed_frees its box.
i = 0
while i < 1000
    t = gi.new_struct("Gdk.RGBA")
    gi.struct_set(t, "blue", i)
    i = i + 1
end while
print gi.struct_get(t, "blue")
