' NAP-2 negative: OUT parameters are results, not caller-supplied arguments, so the
' arity check counts only IN (and INOUT) parameters. GLib.ascii_string_to_unsigned
' declares 5 args but only 4 are IN (str, base, min, max); the 5th is an OUT. Passing
' 3 must report "expects 4 argument(s)", proving the OUT arg is excluded from the count.
load gi
gi.require("GLib", "2.0")
x = gi.invoke("GLib.ascii_string_to_unsigned", "42", 10, 0)
