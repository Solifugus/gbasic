' NAP-4 negative: gi.variant_parse surfaces a GVariant text-format parse failure as
' a clean runtime error (with no leaked GError).
load gi
gi.require("GLib", "2.0")
v = gi.variant_parse("(is)", "this is not valid")
