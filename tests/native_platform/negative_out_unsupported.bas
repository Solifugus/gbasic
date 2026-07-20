' NAP-2 negative: an OUT parameter of a type NAP-2 does not marshal (a C array /
' GStrv). GLib.shell_parse_argv(cmd, OUT argcp gint, OUT argvp array) has a
' supported gint out followed by an unsupported array out. The call must fail
' cleanly BEFORE invoking (so no half-run side effects), freeing any out storage
' already prepared for the earlier args.
load gi
gi.require("GLib", "2.0")
x = gi.invoke("GLib.shell_parse_argv", "a b c")
