' NAP-4 (WI-6): GVariant scalar construction, read-back, type, and print.
' A GVariant is a foreign refcounted value carried by the same reference-semantic
' handle as boxed values (type() reports "gvariant"); gi.variant_get converts it back
' to a gBASIC value, gi.variant_type gives its GVariant type string, gi.variant_print
' its textual form. This proves construct -> read-back -> pass-back-into-GLib.
load gi
gi.require("GLib", "2.0")

b = gi.variant_bool(true)
i = gi.variant_int32(42)
x = gi.variant_double(1.5)
s = gi.variant_string("hi")

print type(b)
print gi.variant_get(b)
print gi.variant_get(i)
print gi.variant_get(x)
print gi.variant_get(s)
print gi.variant_type(i)
print gi.variant_type(s)
print gi.variant_print(i)
print gi.variant_print(s)
