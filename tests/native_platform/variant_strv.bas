' NAP-4 (WI-6): a string-array GVariant ("as"). gi.variant_strv builds it from a
' gBASIC string array; gi.variant_get converts it back to a gBASIC array (each
' element recursively marshalled); gi.variant_type reports the "as" signature.
load gi
gi.require("GLib", "2.0")

v = gi.variant_strv(["alpha", "beta", "gamma"])
print gi.variant_type(v)
a = gi.variant_get(v)
print count(a)
for each e in a
    print e
end for
print gi.variant_print(v)
