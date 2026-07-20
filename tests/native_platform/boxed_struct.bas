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
