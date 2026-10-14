' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' gtkui_gtk.bas — the GTK 4 backend for the `gtkui` reconciler.
'
' `gtkui` describes a UI as a tree of records and reconciles it; THIS library is
' the half that knows what a widget is. Everything GTK-specific that used to sit
' inside the reconciler lives here: creating a widget from a type name, setting a
' property, connecting a signal, putting a child into a container, and the table
' that says which GTK types hold children and how.
'
' WHY THE SPLIT EXISTS (docs/gui_addressability_design.md §0.3, decided
' 2026-09-29: "`gui`'s identity rules, `gtkui`'s mechanism, GTK behind a seam").
' A second backend is foreseeable — an SVG window system is under development for
' RV-9 — and §8 calls backend independence "the quiet prize". It stops being a
' nice-to-have the moment a second backend is real.
'
' AND THE SPLIT IS WHAT MAKES IT TRUE RATHER THAN TIDY. `load` is a DECLARATION:
' it is pre-registered and cannot be conditional, so while the reconciler carried
' `load gi` it could not run at all on a machine without GObject-Introspection,
' whatever backend you handed it. Measured: `tests/run_gtkui.sh` skipped IN FULL
' without the libgirepository development files, including the tier that needs no
' widgets. The dependency lives here now, so the reconciler is dependency-free
' and a backend that is not GTK pays nothing for GTK.
'
' A BACKEND IS A RECORD OF FUNCTION VALUES, twelve of them, and that shape was
' measured before it was chosen: across `gtk`, `gtkui`, `sourceeditor` and
' `datagrid` the whole contact surface is 10 distinct `gi.*` functions over 72
' call sites and 19 constructed types, which is why §0.3 could call the seam "a
' small job, not a port".
'
'   backend = gtkui_gtk.backend()
'   h = gtkui.mount(backend, parent, desc)
'
' THE FOUR CONTAINER KINDS ARE THE SEAM'S VOCABULARY, NOT GTK'S: "box" (ordered,
' many children), "single" (one child), "paned" (two children by the keys "start"
' and "end") and "leaf" (none). A backend maps its own types onto those four, so
' the reconciler's diffing never learns a GTK type name.

library gtkui_gtk
    load gi

    ' ---- the type table ----------------------------------------------------
    '
    ' Which GTK types hold children, and how. This used to be `gtkui._kind` --
    ' a table of GTK type names inside a reconciler that claimed to be about
    ' records, which is the clearest single piece of evidence that a seam
    ' belonged here.
    function kind(t)
        if t = "Gtk.Box" then return "box"
        if t = "Gtk.ScrolledWindow" then return "single"
        if t = "Gtk.Window" then return "single"
        if t = "Gtk.ApplicationWindow" then return "single"
        if t = "Gtk.Frame" then return "single"
        if t = "Gtk.Paned" then return "paned"
        return "leaf"
    end function

    ' ---- widgets, properties, signals --------------------------------------

    function create(t)
        return gi.new(t)
    end function

    function set_prop(widget, name, value)
        gi.set(widget, name, value)
        return nothing
    end function

    function connect(widget, signal, handler)
        return gi.connect(widget, signal, handler)
    end function

    function disconnect(widget, id)
        gi.disconnect(widget, id)
        return nothing
    end function

    ' ---- children ----------------------------------------------------------

    function attach(container, kindname, child, childkey)
        if kindname = "box" then
            container.append(child)
            return nothing
        end if
        if kindname = "single" then
            container.set_child(child)
            return nothing
        end if
        if kindname = "paned" then
            if childkey = "end" then
                container.set_end_child(child)
            else
                container.set_start_child(child)
            end if
            return nothing
        end if
        error "gtkui_gtk: type has no child adapter (cannot hold children)"
    end function

    function detach(container, kindname, child, childkey)
        if kindname = "box" then
            container.remove(child)
            return nothing
        end if
        if kindname = "single" then
            container.set_child(nothing)
            return nothing
        end if
        if kindname = "paned" then
            if childkey = "end" then
                container.set_end_child(nothing)
            else
                container.set_start_child(nothing)
            end if
            return nothing
        end if
        error "gtkui_gtk: type has no child adapter (cannot hold children)"
    end function

    ' Place `child` immediately after `prev` (nothing => first).
    function reorder_after(container, child, prev)
        container.reorder_child_after(child, prev)
        return nothing
    end function

    ' ---- the foreign parent a tree is mounted INTO -------------------------
    '
    ' Dispatched by asking the OBJECT what it is, not by a declared type: the
    ' parent was built by the caller and the reconciler never saw it made.
    function parent_attach(parent, child)
        if gi.is_a(parent, "Gtk.Box") then
            parent.append(child)
            return nothing
        end if
        if gi.is_a(parent, "Gtk.ScrolledWindow") then
            parent.set_child(child)
            return nothing
        end if
        if gi.is_a(parent, "Gtk.Frame") then
            parent.set_child(child)
            return nothing
        end if
        if gi.is_a(parent, "Gtk.Window") then
            parent.set_child(child)
            return nothing
        end if
        error "gtkui.mount: unsupported parent container: " + gi.type_name(parent)
    end function

    function parent_detach(parent, child)
        if gi.is_a(parent, "Gtk.Box") then
            parent.remove(child)
            return nothing
        end if
        parent.set_child(nothing)
    end function

    function describe(widget)
        return gi.type_name(widget)
    end function

    ' ---- the backend record ------------------------------------------------

    function backend()
        b = {}
        b.name = "gtk"
        b.kind = kind
        b.create = create
        b.set_prop = set_prop
        b.connect = connect
        b.disconnect = disconnect
        b.attach = attach
        b.detach = detach
        b.reorder_after = reorder_after
        b.parent_attach = parent_attach
        b.parent_detach = parent_detach
        b.describe = describe
        return b
    end function
end library
