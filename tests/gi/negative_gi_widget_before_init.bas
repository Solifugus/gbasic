' Constructing a Gtk WIDGET before the toolkit is initialized used to SEGFAULT
' (exit 139, core dumped) -- reported from the gBASIC Studio bench 2026-09-25.
' A crash is the worst diagnostic available: it takes buffered stdout with it,
' so a program with `print` tracing produced no output at all and the failure
' read as happening at line 1.
load gi
program main(args)
    gi.require("Gtk", "4.0")
    d = gi.new("Gtk.DropDown")
    print "UNREACHABLE"
end program
