' THE CONTROL for the widget-before-init refusal, and it is what keeps that
' refusal from becoming "nothing in Gtk may be constructed".
'
' MEASURED, thirteen types one process each: StringList, CssProvider,
' TextBuffer, ListStore, EntryBuffer and SizeGroup all construct happily with
' no initialization, while Label, Button, Box, DropDown, Entry, Window and
' ApplicationWindow all segfaulted. The split is exactly "descends from
' GtkWidget", which is why the check tests ancestry rather than the namespace.
load gi
program main(args)
    gi.require("Gtk", "4.0")
    print gi.type_name(gi.new("Gtk.StringList"))
    print gi.type_name(gi.new("Gtk.TextBuffer"))
    print gi.type_name(gi.new("Gtk.SizeGroup"))
end program
