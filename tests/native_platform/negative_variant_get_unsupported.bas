' NAP-4 negative: gi.variant_get maps dictionaries to records, which requires string
' keys; a dictionary with integer keys "a{is}" cannot become a record and is a clean
' error rather than a wrong result.
load gi
gi.require("GLib", "2.0")
d = gi.variant_parse("a{is}", "{1: 'a', 2: 'b'}")
r = gi.variant_get(d)
