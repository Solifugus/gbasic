' ============================================================================
' dynamic_list.bas — a general declarative GTK4 application built on the reusable
' gtkui reconciler (stdlib/gtkui.bas) over the generic `gi` bridge.
'
' This is NOT gBASIC Studio and has no Studio concepts (no projects, execution
' boundaries, branches, agent, or persistence). It is the smallest proof that an
' ordinary gBASIC app can drive a DYNAMIC UI from application state: describe the
' whole UI as a record tree, and on every state change call gtkui.update — the
' reconciler mutates the existing widgets in place (reusing item rows, updating
' the header text) instead of tearing down and rebuilding the tree.
'
' Demonstrates: state change -> reconcile -> widget reuse (keyed rows survive
' add/remove/reverse with their identity), dynamic children, and the native
' escape hatch (a manually-created GtkEntry embedded as a node via `widget:`).
'
' Manual GUI demo; not part of the golden suite (parse-only in run_gui_parse.sh).
' Run with a display:  GBASIC_PATH=stdlib ./gbasic examples/native_ui/dynamic_list.bas
' ============================================================================
load gi
load gtk
load gtkui

' All mutable state lives in ONE global record: gBASIC handlers can persist a
' record-field write (state.x = ...) but not a bare scalar-global rebind, so the
' item list, the id counter, and the live reconciler handle all hang off `state`.
state = { items: [], seq: 0, handle: nothing, search: nothing }

' ---- handlers (mutate state, then reconcile) ------------------------------

function add_item()
    state.seq = state.seq + 1
    row = { id: "item-" + string(state.seq), text: "Task " + string(state.seq) }
    state.items = append(state.items, row)
    state.handle = gtkui.update(state.handle, render())
end function

function remove_last()
    n = count(state.items)
    if n > 0 then
        state.items = take_first(state.items, n - 1)
    end if
    state.handle = gtkui.update(state.handle, render())
end function

function reverse_items()
    state.items = reverse(state.items)
    state.handle = gtkui.update(state.handle, render())
end function

' ---- the declarative UI as a function of state ----------------------------

function render()
    rows = []
    for each it in state.items
        rows = append(rows, { type: "Gtk.Label", key: it.id, props: { label: it.text, xalign: 0.0 } })
    end for

    header = { type: "Gtk.Label", key: "hdr", props: { label: "Items: " + string(count(state.items)) } }

    controls = { type: "Gtk.Box", key: "controls", props: { orientation: gi.enum("Gtk.Orientation.HORIZONTAL"), spacing: 6 }, children: [
        { type: "Gtk.Button", key: "add", props: { label: "Add" }, signals: { clicked: add_item } },
        { type: "Gtk.Button", key: "del", props: { label: "Remove last" }, signals: { clicked: remove_last } },
        { type: "Gtk.Button", key: "rev", props: { label: "Reverse" }, signals: { clicked: reverse_items } }
    ] }

    list = { type: "Gtk.Box", key: "list", props: { orientation: gi.enum("Gtk.Orientation.VERTICAL"), spacing: 2 }, children: rows }

    ' state.search is a GtkEntry created outside the reconciler and embedded via
    ' the native escape hatch — gtkui parents it but leaves it otherwise alone.
    return { type: "Gtk.Box", props: { orientation: gi.enum("Gtk.Orientation.VERTICAL"), spacing: 8 }, children: [
        header,
        controls,
        { widget: state.search, key: "search" },
        list
    ] }
end function

' ---- application boot ------------------------------------------------------

function on_activate(a)
    win = gtk.application_window(a)
    win.title = "gBASIC gtkui — dynamic list"
    win.default_width = 360
    win.default_height = 420

    entry = gi.new("Gtk.Entry")
    entry.set_placeholder_text("a manually-created entry (native escape hatch)")
    state.search = entry

    content = gtk.box("v", 8)
    win.set_child(content)
    state.handle = gtkui.mount(content, render())
    win.present()
end function

app = gtk.application("org.gbasic.GtkuiDynamicList")
gi.connect(app, "activate", on_activate)
gi.call(app, "run", 0, nothing)
