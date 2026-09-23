' A CONSTRUCTOR is refused and names gi.new. Constructors hand back a full
' reference and a GtkWidget's is floating; who sinks it is a question this path
' has never had to answer, and guessing is a leak or a double free rather than
' a wrong value.
load gi
gi.require("GLib", "2.0")
print(gi.invoke("GLib.DateTime.new_now_utc"))
