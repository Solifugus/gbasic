' ============================================================================
' gtk4_hello.bas — the first real GTK4 program written on the gi.* bridge.
' A window, two widgets (a label + a button), a "clicked" signal, and the
' main loop. Everything is done through the raw gi.* GObject-Introspection
' bridge; there is no GTK-specific gBASIC sugar yet.
'
' STATUS: verified to launch — on GTK4 4.22 with a display it opens the window
' and runs the event loop with no GTK criticals. The register->startup->gtk_init
' path and the parent-class method lookup (gi.call "register") are what a live run
' confirmed. The button/close handlers use the same signal path exercised by
' tests/gi/gi_signal_test.bas.
'
' RUN IT (needs GTK4 runtime + typelib + a display):
'   sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1        # Debian/Ubuntu
'   ./gbasic examples/gi/gtk4_hello.bas
'
' This example is deliberately NOT in the golden suite: it is a manual GUI demo
' (needs a display), like the examples/gui/ programs.
'
' Why it is shaped the way it is (v1 bridge limitations, all noted in PLAN.md):
'   * There is no way yet to call a namespace function such as gtk_init(). We
'     get GTK initialised for free by registering a Gtk.Application: registration
'     drives GApplication's "startup" phase, and GtkApplication's startup handler
'     calls gtk_init() for us.
'   * gi.new() cannot pass construct-only properties yet, so we cannot build a
'     Gtk.ApplicationWindow (its `application` is construct-only). Instead we make
'     a plain Gtk.Window and hand it to the app with add_window(), which ties the
'     window to the application all the same.
'   * GApplication.run() takes an argv string array the bridge can't marshal, so
'     we drive our own GLib main loop with gi.main()/gi.quit() instead.
' ============================================================================

load gi

' Make the Gtk 4.0 namespace resolvable (loads its typelib into the repository).
gi.require("Gio", "2.0")
gi.require("Gtk", "4.0")

' --- application: exists mainly to run gtk_init() via its startup phase, and to
' own the window so it stays alive while shown. -----------------------------
app = gi.new("Gtk.Application")
gi.set(app, "application-id", "org.gbasic.Gtk4Hello")

' Registering the primary instance emits "startup" -> gtk_init() runs here.
gi.call(app, "register", nothing)

' --- window ----------------------------------------------------------------
win = gi.new("Gtk.Window")
gi.call(app, "add_window", win)
gi.set(win, "title", "gBASIC on GTK4")
gi.set(win, "default-width", 360)
gi.set(win, "default-height", 160)

' --- widgets: a vertical box holding a label and a button ------------------
box = gi.new("Gtk.Box")
gi.set(box, "orientation", gi.enum("Gtk.Orientation.VERTICAL"))
gi.set(box, "spacing", 12)
gi.set(box, "margin-top", 16)
gi.set(box, "margin-bottom", 16)
gi.set(box, "margin-start", 16)
gi.set(box, "margin-end", 16)

label = gi.new("Gtk.Label")
gi.set(label, "label", "Hello from gBASIC — click the button.")

button = gi.new("Gtk.Button")
gi.set(button, "label", "Click me")

gi.call(box, "append", label)
gi.call(box, "append", button)
gi.call(win, "set_child", box)

' --- signals ---------------------------------------------------------------
' The handler mutates the GObject (the label), NOT a gBASIC variable: assigning
' to a gBASIC global inside a function would only shadow it locally, but the
' label is a live GObject shared by reference, so gi.set on it is visible.
function on_click(source)
    gi.set(label, "label", "Button was clicked!")
    print("button clicked")
end function

' Closing the window ends our main loop. "close-request" returns a boolean in
' GTK4 (TRUE stops the close); we leave it defaulted to false, so the window
' closes and we quit.
function on_close(source)
    print("window closed — quitting")
    gi.quit()
end function

gi.connect(button, "clicked", on_click)
gi.connect(win, "close-request", on_close)

' --- show + run ------------------------------------------------------------
gi.call(win, "present")
gi.main()
print("done")
