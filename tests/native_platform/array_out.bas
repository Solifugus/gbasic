' NAP-4 (WI-7): a zero-terminated C string array (GStrv) returned from GI, transfer
' full, converted to a gBASIC array. GLib.strsplit("a,b,c", ",", 0) -> ["a","b","c"];
' the native char** and its strings are freed after conversion (transfer EVERYTHING).
load gi
gi.require("GLib", "2.0")

parts = gi.invoke("GLib.strsplit", "a,b,c", ",", 0)
print count(parts)
for each p in parts
    print p
end for
