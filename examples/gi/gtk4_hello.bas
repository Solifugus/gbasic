' ============================================================================
' gtk4_hello.bas — a GTK4 program in the CANONICAL idiom on the gi.* bridge.
' A window, two widgets (a label + a button), a "clicked" signal, and the
' application's own main loop. Everything goes through the raw gi.* bridge; there
' is no GTK-specific gBASIC sugar yet.
'
' This version is the manual acceptance test for the two bridge items that landed
' in Phase GI's follow-up — it uses the real GtkApplication idiom instead of the
' three v1 workarounds the earlier draft needed:
'
'   * gi.new now sets CONSTRUCT-TIME properties, so the window is a genuine
'     Gtk.ApplicationWindow built with its construct-only "application" property
'     (gi.new("Gtk.ApplicationWindow", "application", app)) — no more plain
'     Gtk.Window + add_window().
'   * The application drives its OWN loop: gi.call(app, "run", 0, nothing). The
'     argv array is passed as `nothing`, which the bridge marshals to a NULL
'     pointer (argc 0). "run" emits "startup" (which runs gtk_init for us) and
'     then "activate", so there is no separate register()/gi.main() dance.
'
' The window is created inside the "activate" handler — the GApplication contract:
' "activate" is where you build and show your primary window. When the last window
' closes, the application quits run() on its own, so there is no explicit gi.quit.
'
' RUN IT (needs GTK4 runtime + typelib + a display):
'   sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1        # Debian/Ubuntu
'   ./gbasic examples/gi/gtk4_hello.bas
'
' Manual GUI demo (needs a display); not part of the golden suite.
' ============================================================================

load gi
gi.require("Gio", "2.0")
gi.require("Gtk", "4.0")

' The click handler mutates the button that fired it (its `source`), so it needs
' no cross-scope reference to any widget built in on_activate.
function on_click(source)
    gi.set(source, "label", "Clicked!")
    print("button clicked")
end function

' "activate" hands us the GApplication; we build the primary window against it and
' present it. The window's "application" is construct-only — settable only here,
' through gi.new — which is exactly what the construct-property support enables.
function on_activate(app)
    win = gi.new("Gtk.ApplicationWindow", "application", app)
    gi.set(win, "title", "gBASIC on GTK4")
    gi.set(win, "default-width", 320)
    gi.set(win, "default-height", 160)

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
    gi.connect(button, "clicked", on_click)

    gi.call(box, "append", label)
    gi.call(box, "append", button)
    gi.call(win, "set_child", box)
    gi.call(win, "present")
end function

app = gi.new("Gtk.Application", "application-id", "org.gbasic.Gtk4Hello")
gi.connect(app, "activate", on_activate)

' run() emits startup (gtk_init) then activate, and blocks until the last window
' closes. argv is passed as nothing -> NULL; argc is 0.
gi.call(app, "run", 0, nothing)
print("done")
