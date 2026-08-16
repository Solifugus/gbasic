' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' gtkui.bas — a dynamic declarative widget-tree reconciler for GTK 4.
'
' You describe a UI as a tree of plain gBASIC records ("nodes"); gtkui builds the
' real GTK 4 widget tree, and on each `update` it mutates the EXISTING widgets in
' place where it can — changing only the properties that changed, adding/removing/
' reordering children, replacing a widget only when its type or identity changes.
' This is the dynamic tree mutation the old GTK 3 `gui` module never had.
'
' It is NOT a closed widget system. Every node maps to a real GTK GObject you can
' reach: `gtkui.root(handle)` and `gtkui.lookup(handle, key)` hand back the actual
' widget, so you drop straight to the `gi` bridge (`w.method(...)`, `w.prop = ...`,
' `gi.connect`) for anything gtkui does not model. Widgets are constructed
' generically with `gi.new(type)`, so ANY introspected GTK widget works as a node
' with no new code here — and a node may instead carry an already-created widget
' (`widget:`), letting you embed a manually-built GtkGrid, a SourceEditor view, or
' any other component the reconciler did not make.
'
' Requires the GTK 4 typelib at runtime and `load gi` in the program (as gtk.bas
' does); GTK must be initialized (a running gtk.application, or gtk.init()) before
' widgets are built. No native/C code — everything here is gBASIC over `gi`.
'
' ----------------------------------------------------------------------------
' Node schema (a record; every field optional except one of type/widget):
'   type     : GI type name string, e.g. "Gtk.Button"      (built via gi.new)
'   widget   : a pre-created GObject — the native escape hatch (opaque: gtkui
'              does not manage its props/children, but will attach its signals)
'   key      : stable identity string within a sibling list
'   props    : record of property -> value (scalars/strings/bools/objects;
'              enums as ints via gi.enum(...))
'   signals  : record of signal-name -> handler function
'   children : array of child nodes
'
' Public API:
'   handle = gtkui.mount(parent, desc)   build desc under an existing container
'   handle = gtkui.update(handle, desc)  reconcile; RETURNS the new handle
'   gtkui.unmount(handle)                detach + disconnect everything
'   gtkui.root(handle)                   the root GTK widget (raw, for interop)
'   gtkui.lookup(handle, key)             the widget for a keyed node, or nothing
' ----------------------------------------------------------------------------
library gtkui


    ' Dependencies, declared rather than assumed. A library that calls into
    ' another must load it: relying on the caller to have done so turns a
    ' missing load into a runtime failure deep inside a call, and it stops
    ' working entirely once these libraries live in separate projects.
    load gi
    ' ---- container-kind table (extensible: add a type -> a branch) ----------

    ' Classify a GI type name by how it parents children. "box" = ordered
    ' multi-child; "single" = exactly one child via set_child; "paned" = two
    ' keyed slots ("start"/"end"); "leaf" = no child management.
    function _kind(t)
        if t = "Gtk.Box" then return "box"
        if t = "Gtk.ScrolledWindow" then return "single"
        if t = "Gtk.Window" then return "single"
        if t = "Gtk.ApplicationWindow" then return "single"
        if t = "Gtk.Frame" then return "single"
        if t = "Gtk.Paned" then return "paned"
        return "leaf"
    end function

    ' ---- small record helpers ----------------------------------------------

    function _get(rec, field, dflt)
        if has(rec, field) then
            return rec[field]
        end if
        return dflt
    end function

    ' ---- widget construction, properties, signals --------------------------

    ' Apply only the properties whose value changed from what was last applied.
    ' Unchanged properties are left untouched (state preservation); a property
    ' dropped from the new description is NOT reverted (there is no universal
    ' default) — set it explicitly to change it.
    function _apply_props(widget, oldprops, newprops)
        for each k in keys(newprops)
            nv = newprops[k]
            if not has(oldprops, k) then
                gi.set(widget, k, nv)
            else
                if oldprops[k] != nv then
                    gi.set(widget, k, nv)
                end if
            end if
        end for
    end function

    ' Connect the node's declared signals and RETURN a record of signal-name ->
    ' handler id, so the caller can store it on the rnode it owns (records are
    ' copy-on-write, so a helper cannot mutate the caller's rnode in place). The
    ' recorded ids let a later reconcile disconnect exactly what it connected, so
    ' handlers are never duplicated.
    function _connect_signals(widget, vnode)
        sigs = {}
        declared = gtkui._get(vnode, "signals", {})
        for each s in keys(declared)
            sigs[s] = gi.connect(widget, s, declared[s])
        end for
        return sigs
    end function

    ' Reconcile signals on a reused widget: drop every previously-connected
    ' handler (by its recorded id) and connect exactly those in the new
    ' description. Returns the new id map. Result: one connection per declared
    ' signal, always the current handler, never doubled.
    function _reconcile_signals(widget, oldsigs, vnode)
        for each s in keys(oldsigs)
            gi.disconnect(widget, oldsigs[s])
        end for
        return gtkui._connect_signals(widget, vnode)
    end function

    ' ---- child attach / detach / order (dispatched on container kind) ------

    function _attach(container, kind, child, childkey)
        if kind = "box" then
            container.append(child)
            return nothing
        end if
        if kind = "single" then
            container.set_child(child)
            return nothing
        end if
        if kind = "paned" then
            if childkey = "end" then
                container.set_end_child(child)
            else
                container.set_start_child(child)
            end if
            return nothing
        end if
        error "gtkui: type has no child adapter (cannot hold children)"
    end function

    function _detach(container, kind, child, childkey)
        if kind = "box" then
            container.remove(child)
            return nothing
        end if
        if kind = "single" then
            container.set_child(nothing)
            return nothing
        end if
        if kind = "paned" then
            if childkey = "end" then
                container.set_end_child(nothing)
            else
                container.set_start_child(nothing)
            end if
            return nothing
        end if
        error "gtkui: type has no child adapter (cannot hold children)"
    end function

    ' Enforce the desired order of a Box's children by walking the list and
    ' placing each child immediately after the previous one (nothing => first).
    function _box_reorder(container, rendered)
        prev = nothing
        for each r in rendered
            container.reorder_child_after(r.widget, prev)
            prev = r.widget
        end for
    end function

    ' ---- create / destroy ---------------------------------------------------

    ' Build a rendered node (rnode) from a description: create or adopt its
    ' widget, apply props, connect signals, and recursively create + attach
    ' children. Returns the rnode { type, key, widget, native, props, sigs,
    ' children }.
    function _create_node(v)
        r = {}
        r.key = gtkui._get(v, "key", nothing)
        r.sigs = {}
        r.children = []

        if has(v, "widget") then
            r.native = true
            r.type = nothing
            r.widget = v.widget
            r.props = {}
            r.sigs = gtkui._connect_signals(r.widget, v)
            return r
        end if

        r.native = false
        r.type = v.type
        r.widget = gi.new(v.type)
        r.props = gtkui._get(v, "props", {})
        gtkui._apply_props(r.widget, {}, r.props)
        r.sigs = gtkui._connect_signals(r.widget, v)

        kids = gtkui._get(v, "children", [])
        kind = gtkui._kind(r.type)
        for each cv in kids
            cr = gtkui._create_node(cv)
            r.children = append(r.children, cr)
            gtkui._attach(r.widget, kind, cr.widget, cr.key)
        end for
        return r
    end function

    ' Recursively disconnect a subtree's signal handlers. GTK frees the widgets
    ' themselves by refcount once they are unparented by the caller.
    function _destroy_node(r)
        for each s in keys(r.sigs)
            gi.disconnect(r.widget, r.sigs[s])
        end for
        r.sigs = {}
        for each c in r.children
            gtkui._destroy_node(c)
        end for
    end function

    ' ---- compatibility test (reuse vs replace) ------------------------------

    ' A reused widget must match native-ness and, for constructed widgets, type;
    ' for native nodes it must be the very same underlying widget object. A key
    ' change is handled by the child matcher (it pairs by key), not here.
    function _compatible(old, newv)
        newnative = has(newv, "widget")
        if old.native != newnative then
            return false
        end if
        if newnative then
            return old.widget = newv.widget
        end if
        return old.type = newv.type
    end function

    ' ---- child-list reconciliation -----------------------------------------

    ' Keyed matching applies only when every old and every new child carries a
    ' key; otherwise matching is positional (by index).
    function _all_keyed(olds, news)
        for each o in olds
            if o.key = nothing then
                return false
            end if
        end for
        for each n in news
            if not has(n, "key") then
                return false
            end if
        end for
        return true
    end function

    function _reconcile_children(parent, newkids)
        container = parent.widget
        kind = gtkui._kind(parent.type)
        oldkids = parent.children

        if kind = "leaf" then
            if count(newkids) > 0 then
                error "gtkui: type " + string(parent.type) + " cannot hold children"
            end if
            return []
        end if
        if kind = "single" then
            return gtkui._reconcile_single(container, oldkids, newkids)
        end if
        if kind = "paned" then
            return gtkui._reconcile_paned(container, oldkids, newkids)
        end if
        ' box (ordered multi-child)
        if gtkui._all_keyed(oldkids, newkids) then
            return gtkui._reconcile_box_keyed(container, oldkids, newkids)
        end if
        return gtkui._reconcile_box_positional(container, oldkids, newkids)
    end function

    function _reconcile_single(container, oldkids, newkids)
        if count(newkids) = 0 then
            if count(oldkids) > 0 then
                container.set_child(nothing)
                gtkui._destroy_node(oldkids[0])
            end if
            return []
        end if
        nv = newkids[0]
        if count(oldkids) > 0 then
            oldr = oldkids[0]
            if gtkui._compatible(oldr, nv) then
                return [ gtkui._reconcile_node(oldr, nv) ]
            end if
            container.set_child(nothing)
            gtkui._destroy_node(oldr)
        end if
        r = gtkui._create_node(nv)
        container.set_child(r.widget)
        return [r]
    end function

    ' Paned: children are addressed by key "start"/"end" (default start).
    function _reconcile_paned(container, oldkids, newkids)
        result = []
        for each nv in newkids
            slot = gtkui._get(nv, "key", "start")
            oldr = gtkui._paned_find(oldkids, slot)
            if oldr != nothing and gtkui._compatible(oldr, nv) then
                result = append(result, gtkui._reconcile_node(oldr, nv))
            else
                if oldr != nothing then
                    gtkui._detach(container, "paned", oldr.widget, slot)
                    gtkui._destroy_node(oldr)
                end if
                r = gtkui._create_node(nv)
                gtkui._attach(container, "paned", r.widget, slot)
                result = append(result, r)
            end if
        end for
        ' remove any old slot no longer described
        for each o in oldkids
            if gtkui._paned_still_present(newkids, o.key) = false then
                gtkui._detach(container, "paned", o.widget, o.key)
                gtkui._destroy_node(o)
            end if
        end for
        return result
    end function

    function _paned_find(oldkids, slot)
        for each o in oldkids
            if o.key = slot then
                return o
            end if
        end for
        return nothing
    end function

    function _paned_still_present(newkids, slot)
        for each nv in newkids
            if gtkui._get(nv, "key", "start") = slot then
                return true
            end if
        end for
        return false
    end function

    function _reconcile_box_positional(container, oldkids, newkids)
        result = []
        oldcount = count(oldkids)
        i = 0
        for each nv in newkids
            if i < oldcount then
                oldr = oldkids[i]
                if gtkui._compatible(oldr, nv) then
                    result = append(result, gtkui._reconcile_node(oldr, nv))
                else
                    container.remove(oldr.widget)
                    gtkui._destroy_node(oldr)
                    r = gtkui._create_node(nv)
                    container.append(r.widget)
                    result = append(result, r)
                end if
            else
                r = gtkui._create_node(nv)
                container.append(r.widget)
                result = append(result, r)
            end if
            i = i + 1
        end for
        ' drop trailing old children the new list no longer has
        j = count(newkids)
        while j < oldcount
            container.remove(oldkids[j].widget)
            gtkui._destroy_node(oldkids[j])
            j = j + 1
        end while
        gtkui._box_reorder(container, result)
        return result
    end function

    function _reconcile_box_keyed(container, oldkids, newkids)
        oldmap = {}
        for each o in oldkids
            oldmap[o.key] = o
        end for
        used = {}
        result = []
        for each nv in newkids
            k = nv.key
            if has(oldmap, k) then
                oldr = oldmap[k]
                used[k] = true
                if gtkui._compatible(oldr, nv) then
                    result = append(result, gtkui._reconcile_node(oldr, nv))
                else
                    container.remove(oldr.widget)
                    gtkui._destroy_node(oldr)
                    r = gtkui._create_node(nv)
                    container.append(r.widget)
                    result = append(result, r)
                end if
            else
                r = gtkui._create_node(nv)
                container.append(r.widget)
                result = append(result, r)
            end if
        end for
        ' remove old children whose key is gone
        for each o in oldkids
            if not has(used, o.key) then
                container.remove(o.widget)
                gtkui._destroy_node(o)
            end if
        end for
        gtkui._box_reorder(container, result)
        return result
    end function

    ' ---- in-place update of a reused widget --------------------------------

    function _reconcile_node(old, newv)
        if old.native = false then
            newprops = gtkui._get(newv, "props", {})
            gtkui._apply_props(old.widget, old.props, newprops)
            old.props = newprops
        end if
        old.sigs = gtkui._reconcile_signals(old.widget, old.sigs, newv)
        if old.native = false then
            old.children = gtkui._reconcile_children(old, gtkui._get(newv, "children", []))
        end if
        return old
    end function

    ' ---- mounting into an existing parent container ------------------------

    function _parent_attach(parent, child)
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

    function _parent_detach(parent, child)
        if gi.is_a(parent, "Gtk.Box") then
            parent.remove(child)
            return nothing
        end if
        parent.set_child(nothing)
    end function

    ' ---- public API ---------------------------------------------------------

    function mount(parent, desc)
        root = gtkui._create_node(desc)
        gtkui._parent_attach(parent, root.widget)
        h = {}
        h.parent = parent
        h.node = root
        return h
    end function

    function update(h, desc)
        old = h.node
        if gtkui._compatible(old, desc) then
            node = gtkui._reconcile_node(old, desc)
        else
            gtkui._parent_detach(h.parent, old.widget)
            gtkui._destroy_node(old)
            node = gtkui._create_node(desc)
            gtkui._parent_attach(h.parent, node.widget)
        end if
        nh = {}
        nh.parent = h.parent
        nh.node = node
        return nh
    end function

    function unmount(h)
        gtkui._parent_detach(h.parent, h.node.widget)
        gtkui._destroy_node(h.node)
    end function

    function root(h)
        return h.node.widget
    end function

    function lookup(h, key)
        return gtkui._find_node(h.node, key)
    end function

    function _find_node(r, key)
        if r.key = key then
            return r.widget
        end if
        for each c in r.children
            w = gtkui._find_node(c, key)
            if w != nothing then
                return w
            end if
        end for
        return nothing
    end function

end library
