' Is the Gtk 4 introspection typelib present? Nothing is constructed; a
' "could not load namespace" failure is how the runner decides to skip the
' widget-initialization tier, the same probe run_native_platform.sh uses.
load gi
program main(args)
    gi.require("Gtk", "4.0")
end program
