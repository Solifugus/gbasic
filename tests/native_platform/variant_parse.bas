' NAP-4 (WI-6): composite GVariants via gi.variant_parse (GVariant text format).
' A tuple "(is)" reads back as a gBASIC array; a dictionary "a{ss}" with string keys
' reads back as a record. This covers the composite forms reasonably representable in
' gBASIC without a per-signature builtin explosion.
load gi
gi.require("GLib", "2.0")

t = gi.variant_parse("(is)", "(42, 'hello')")
print gi.variant_type(t)
tv = gi.variant_get(t)
print count(tv)
print tv[0]
print tv[1]

d = gi.variant_parse("a{ss}", "{'one': 'x', 'two': 'y'}")
print gi.variant_type(d)
dv = gi.variant_get(d)
print dv.one
print dv.two
