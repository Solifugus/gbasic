' NAP-4 negative: gi.variant_strv requires every element to be a string; a number
' element is a clean error, not a silent coercion.
load gi
gi.require("GLib", "2.0")
v = gi.variant_strv(["ok", 5, "no"])
