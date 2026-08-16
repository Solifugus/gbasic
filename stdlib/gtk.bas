' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' gtk.bas — thin ergonomic conveniences for GTK 4 from gBASIC.
'
' This is NOT a new widget framework and NOT the declarative reconciler planned
' for a later phase. It only removes repetitive `gi.require`/`gi.new`/`gi.enum`
' boilerplate and gives common constructors consistent names. Every function
' returns the underlying GTK GObject, so callers drop straight down to the raw
' `gi` bridge (`obj.method(...)`, `obj.prop = ...`, `gi.call`, `gi.connect`) for
' anything not wrapped here. Do not hide GTK semantics behind a second object
' model — this wraps GTK, it does not replace it.
'
' Requires the GTK 4 introspection typelib at runtime (gir1.2-gtk-4.0); it is
' loaded through `gi`, not linked.
library gtk


    ' Dependencies, declared rather than assumed. A library that calls into
    ' another must load it: relying on the caller to have done so turns a
    ' missing load into a runtime failure deep inside a call, and it stops
    ' working entirely once these libraries live in separate projects.
    load gi
    ' Ensure the GTK 4 namespace is available. Safe to call repeatedly.
    function require()
        gi.require("Gtk", "4.0")
    end function

    ' Initialize GTK (needed before constructing widgets). Returns gi.invoke's
    ' result. A display is required for real widget use; construction of
    ' non-widget objects (buffers, models) does not need this.
    function init()
        gi.require("Gtk", "4.0")
        return gi.invoke("Gtk.init")
    end function

    ' A GtkApplication with the given reverse-DNS id (e.g. "org.example.App").
    function application(app_id)
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Application", "application-id", app_id)
    end function

    ' A GtkApplicationWindow bound to an application.
    function application_window(app)
        return gi.new("Gtk.ApplicationWindow", "application", app)
    end function

    ' A bare GtkWindow (no application).
    function window()
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Window")
    end function

    ' A GtkBox. `orientation` is "h"/"horizontal" or "v"/"vertical" (default
    ' vertical); `spacing` is inter-child pixels.
    function box(orientation, spacing)
        o = gi.enum("Gtk.Orientation.VERTICAL")
        if orientation = "h" or orientation = "horizontal" then
            o = gi.enum("Gtk.Orientation.HORIZONTAL")
        end if
        return gi.new("Gtk.Box", "orientation", o, "spacing", spacing)
    end function

    ' A GtkPaned split "h"/"horizontal" or "v"/"vertical" (default horizontal).
    function paned(orientation)
        o = gi.enum("Gtk.Orientation.HORIZONTAL")
        if orientation = "v" or orientation = "vertical" then
            o = gi.enum("Gtk.Orientation.VERTICAL")
        end if
        return gi.new("Gtk.Paned", "orientation", o)
    end function

    ' A GtkScrolledWindow. Pass a child widget (or `nothing` to set it later).
    function scrolled(child)
        s = gi.new("Gtk.ScrolledWindow")
        if child != nothing then
            s.set_child(child)
        end if
        return s
    end function

    ' A GtkStack (named-page container).
    function stack()
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Stack")
    end function

    ' A GtkNotebook (tabbed container).
    function notebook()
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Notebook")
    end function

    ' A GtkListBox (row list; sufficient for hundreds–thousands of rows).
    function listbox()
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.ListBox")
    end function

    ' A GtkButton with a text label.
    function button(label)
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Button", "label", label)
    end function

    ' A GtkLabel.
    function label(text)
        gi.require("Gtk", "4.0")
        return gi.new("Gtk.Label", "label", text)
    end function

    ' ---- small event/enum helpers ------------------------------------------

    ' Connect a signal handler; returns nothing. Thin alias over gi.connect so
    ' application code reads `gtk.connect(button, "clicked", handler)`. (Named
    ' `connect`, not `on`, because `on` is a reserved keyword — `on error` — and
    ' cannot be a function name.)
    function connect(widget, signal, handler)
        gi.connect(widget, signal, handler)
    end function

    ' Resolve any GTK enum/flags member to its integer, e.g.
    ' gtk.enum("Gtk.Orientation.VERTICAL"). Just `gi.enum` under a GTK-flavored
    ' name for locality.
    function enum(qualified_name)
        return gi.enum(qualified_name)
    end function

end library
