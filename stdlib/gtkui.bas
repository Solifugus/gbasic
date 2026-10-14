' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' gtkui.bas — a dynamic declarative widget-tree reconciler, over ANY backend.
'
' THE NAME IS HISTORICAL. This library knew GTK when it was written and no longer
' does: every GTK call moved into `stdlib/gtkui_gtk.bas` on 2026-10-14 and the
' caller now supplies a backend. It keeps the name because three in-tree callers
' and the library index use it, and a rename is a separate, louder change.
'
' You describe a UI as a tree of plain gBASIC records ("nodes"); gtkui builds the
' real GTK 4 widget tree, and on each `update` it mutates the EXISTING widgets in
' place where it can — changing only the properties that changed, adding/removing/
' reordering children, replacing a widget only when its type or identity changes.
' This is the dynamic tree mutation the old GTK 3 `gui` module never had.
'
' It is NOT a closed widget system. Every node maps to a real object you can
' reach: `gtkui.root(handle)` and `gtkui.lookup(handle, key)` hand back the actual
' widget, so with the GTK backend you drop straight to the `gi` bridge
' (`w.method(...)`, `w.prop = ...`, `gi.connect`) for anything gtkui does not
' model. Widgets are constructed generically from a type NAME, so any type the
' backend can make works as a node with no new code here — and a node may instead
' carry an already-created widget (`widget:`), letting you embed a manually-built
' GtkGrid, a SourceEditor view, or any other component the reconciler did not
' make.
'
' THE BACKEND IS AN ARGUMENT (docs/gui_addressability_design.md §0.3, decided
' 2026-09-29: "`gui`'s identity rules, `gtkui`'s mechanism, GTK behind a seam"):
'
'   load gtkui
'   load gtkui_gtk
'   h = gtkui.mount(gtkui_gtk.backend(), parent, desc)
'
' A backend is a record of twelve function values — create, set_prop, connect,
' disconnect, kind, attach, detach, reorder_after, parent_attach, parent_detach,
' describe, name. Nothing in this file knows a GTK type name, and it has NO
' `load` of its own, which is the half that makes the seam true rather than tidy:
' `load` is a declaration and cannot be conditional, so while this file carried
' `load gi` the reconciler could not run at all without GObject-Introspection,
' whatever backend it was given.
'
' With the GTK backend, GTK must be initialized (a running gtk.application, or
' gtk.init()) before widgets are built. No native/C code — everything here is
' gBASIC.
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
'   handle = gtkui.mount(b, parent, desc)  build desc under an existing container
'   handle = gtkui.update(handle, desc)  reconcile; RETURNS the new handle
'   gtkui.unmount(handle)                detach + disconnect everything
'   gtkui.root(handle)                   the root GTK widget (raw, for interop)
'   gtkui.lookup(handle, key)             the widget for a keyed node, or nothing
' ----------------------------------------------------------------------------
library gtkui


    ' NO DEPENDENCIES, AND THAT IS THE POINT OF THE SEAM. This library used to
    ' carry `load gi`, and `load` is a DECLARATION -- pre-registered, so it
    ' cannot be conditional -- which meant the reconciler could not run AT ALL on
    ' a machine without GObject-Introspection, whatever backend it was handed.
    ' Measured: `tests/run_gtkui.sh` skipped IN FULL without the libgirepository
    ' development files, including the tier that needs no widgets. The GTK calls
    ' live in `stdlib/gtkui_gtk.bas` now and the caller supplies the backend, so
    ' a backend that is not GTK pays nothing for GTK.
    '
    ' THE CONTAINER-KIND TABLE WENT WITH THEM. It mapped GTK type names
    ' ("Gtk.Box" -> "box") inside a reconciler whose subject is records, which was
    ' the clearest single piece of evidence that a seam belonged here. The four
    ' kinds -- "box", "single", "paned", "leaf" -- are the SEAM's vocabulary and
    ' stay here; which of its own types answer to each is the backend's business.

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
    function _apply_props(b, widget, oldprops, newprops)
        for each k in keys(newprops)
            nv = newprops[k]
            if not has(oldprops, k) then
                b.set_prop(widget, k, nv)
            else
                if oldprops[k] != nv then
                    b.set_prop(widget, k, nv)
                end if
            end if
        end for
    end function

    ' Connect the node's declared signals and RETURN a record of signal-name ->
    ' handler id, so the caller can store it on the rnode it owns (records are
    ' copy-on-write, so a helper cannot mutate the caller's rnode in place). The
    ' recorded ids let a later reconcile disconnect exactly what it connected, so
    ' handlers are never duplicated.
    function _connect_signals(b, widget, vnode)
        sigs = {}
        declared = gtkui._get(vnode, "signals", {})
        for each s in keys(declared)
            sigs[s] = b.connect(widget, s, declared[s])
        end for
        return sigs
    end function

    ' Reconcile signals on a reused widget: drop every previously-connected
    ' handler (by its recorded id) and connect exactly those in the new
    ' description. Returns the new id map. Result: one connection per declared
    ' signal, always the current handler, never doubled.
    function _reconcile_signals(b, widget, oldsigs, vnode)
        for each s in keys(oldsigs)
            b.disconnect(widget, oldsigs[s])
        end for
        return gtkui._connect_signals(b, widget, vnode)
    end function

    ' ---- child attach / detach / order (dispatched on container kind) ------

    ' Enforce the desired order of a box's children by walking the list and
    ' placing each child immediately after the previous one (nothing => first).
    function _box_reorder(b, container, rendered)
        prev = nothing
        for each r in rendered
            b.reorder_after(container, r.widget, prev)
            prev = r.widget
        end for
    end function

    ' ---- create / destroy ---------------------------------------------------

    ' Build a rendered node (rnode) from a description: create or adopt its
    ' widget, apply props, connect signals, and recursively create + attach
    ' children. Returns the rnode { type, key, widget, native, props, sigs,
    ' children }.
    function _create_node(b, v)
        r = {}
        r.key = gtkui._get(v, "key", nothing)
        r.sigs = {}
        r.children = []

        if has(v, "widget") then
            r.native = true
            r.type = nothing
            r.widget = v.widget
            r.props = {}
            r.sigs = gtkui._connect_signals(b, r.widget, v)
            return r
        end if

        r.native = false
        r.type = v.type
        r.widget = b.create(v.type)
        r.props = gtkui._get(v, "props", {})
        gtkui._apply_props(b, r.widget, {}, r.props)
        r.sigs = gtkui._connect_signals(b, r.widget, v)

        kids = gtkui._get(v, "children", [])
        kind = b.kind(r.type)
        for each cv in kids
            cr = gtkui._create_node(b, cv)
            r.children = append(r.children, cr)
            b.attach(r.widget, kind, cr.widget, cr.key)
        end for
        return r
    end function

    ' Recursively disconnect a subtree's signal handlers. GTK frees the widgets
    ' themselves by refcount once they are unparented by the caller.
    function _destroy_node(b, r)
        for each s in keys(r.sigs)
            b.disconnect(r.widget, r.sigs[s])
        end for
        r.sigs = {}
        for each c in r.children
            gtkui._destroy_node(b, c)
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

    function _reconcile_children(b, parent, newkids)
        container = parent.widget
        kind = b.kind(parent.type)
        oldkids = parent.children

        if kind = "leaf" then
            if count(newkids) > 0 then
                error "gtkui: type " + string(parent.type) + " cannot hold children"
            end if
            return []
        end if
        if kind = "single" then
            return gtkui._reconcile_single(b, container, oldkids, newkids)
        end if
        if kind = "paned" then
            return gtkui._reconcile_paned(b, container, oldkids, newkids)
        end if
        ' box (ordered multi-child)
        if gtkui._all_keyed(oldkids, newkids) then
            return gtkui._reconcile_box_keyed(b, container, oldkids, newkids)
        end if
        return gtkui._reconcile_box_positional(b, container, oldkids, newkids)
    end function

    function _reconcile_single(b, container, oldkids, newkids)
        if count(newkids) = 0 then
            if count(oldkids) > 0 then
                b.detach(container, "single", oldkids[0].widget, oldkids[0].key)
                gtkui._destroy_node(b, oldkids[0])
            end if
            return []
        end if
        nv = newkids[0]
        if count(oldkids) > 0 then
            oldr = oldkids[0]
            if gtkui._compatible(oldr, nv) then
                return [ gtkui._reconcile_node(b, oldr, nv) ]
            end if
            b.detach(container, "single", oldr.widget, oldr.key)
            gtkui._destroy_node(b, oldr)
        end if
        r = gtkui._create_node(b, nv)
        b.attach(container, "single", r.widget, r.key)
        return [r]
    end function

    ' Paned: children are addressed by key "start"/"end" (default start).
    function _reconcile_paned(b, container, oldkids, newkids)
        result = []
        for each nv in newkids
            slot = gtkui._get(nv, "key", "start")
            oldr = gtkui._paned_find(oldkids, slot)
            if oldr != nothing and gtkui._compatible(oldr, nv) then
                result = append(result, gtkui._reconcile_node(b, oldr, nv))
            else
                if oldr != nothing then
                    b.detach(container, "paned", oldr.widget, slot)
                    gtkui._destroy_node(b, oldr)
                end if
                r = gtkui._create_node(b, nv)
                b.attach(container, "paned", r.widget, slot)
                result = append(result, r)
            end if
        end for
        ' remove any old slot no longer described
        for each o in oldkids
            if gtkui._paned_still_present(newkids, o.key) = false then
                b.detach(container, "paned", o.widget, o.key)
                gtkui._destroy_node(b, o)
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

    function _reconcile_box_positional(b, container, oldkids, newkids)
        result = []
        oldcount = count(oldkids)
        i = 0
        for each nv in newkids
            if i < oldcount then
                oldr = oldkids[i]
                if gtkui._compatible(oldr, nv) then
                    result = append(result, gtkui._reconcile_node(b, oldr, nv))
                else
                    b.detach(container, "box", oldr.widget, oldr.key)
                    gtkui._destroy_node(b, oldr)
                    r = gtkui._create_node(b, nv)
                    b.attach(container, "box", r.widget, r.key)
                    result = append(result, r)
                end if
            else
                r = gtkui._create_node(b, nv)
                b.attach(container, "box", r.widget, r.key)
                result = append(result, r)
            end if
            i = i + 1
        end for
        ' drop trailing old children the new list no longer has
        j = count(newkids)
        while j < oldcount
            b.detach(container, "box", oldkids[j].widget, oldkids[j].key)
            gtkui._destroy_node(b, oldkids[j])
            j = j + 1
        end while
        gtkui._box_reorder(b, container, result)
        return result
    end function

    function _reconcile_box_keyed(b, container, oldkids, newkids)
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
                    result = append(result, gtkui._reconcile_node(b, oldr, nv))
                else
                    b.detach(container, "box", oldr.widget, oldr.key)
                    gtkui._destroy_node(b, oldr)
                    r = gtkui._create_node(b, nv)
                    b.attach(container, "box", r.widget, r.key)
                    result = append(result, r)
                end if
            else
                r = gtkui._create_node(b, nv)
                b.attach(container, "box", r.widget, r.key)
                result = append(result, r)
            end if
        end for
        ' remove old children whose key is gone
        for each o in oldkids
            if not has(used, o.key) then
                b.detach(container, "box", o.widget, o.key)
                gtkui._destroy_node(b, o)
            end if
        end for
        gtkui._box_reorder(b, container, result)
        return result
    end function

    ' ---- in-place update of a reused widget --------------------------------

    function _reconcile_node(b, old, newv)
        if old.native = false then
            newprops = gtkui._get(newv, "props", {})
            gtkui._apply_props(b, old.widget, old.props, newprops)
            old.props = newprops
        end if
        old.sigs = gtkui._reconcile_signals(b, old.widget, old.sigs, newv)
        if old.native = false then
            old.children = gtkui._reconcile_children(b, old, gtkui._get(newv, "children", []))
        end if
        return old
    end function

    ' ---- public API ---------------------------------------------------------
    '
    ' MOUNTING INTO A FOREIGN PARENT is the backend's job, not the seam's: that
    ' container was built by the caller and the reconciler never saw it made, so
    ' the only way to know how it takes a child is to ask the object. That is
    ' `b.parent_attach` / `b.parent_detach`, and it is the one pair that
    ' dispatches on the OBJECT rather than on a declared type.
    '
    ' THE HANDLE CARRIES THE BACKEND, which is why only `mount` grew an argument:
    ' `update`, `unmount`, `root` and `lookup` read it back out, so their call
    ' sites are untouched. Measured before choosing this shape -- of the 58
    ' `gtkui.*` call sites in the tree, 8 are `mount`.

    function mount(b, parent, desc)
        root = gtkui._create_node(b, desc)
        b.parent_attach(parent, root.widget)
        h = {}
        h.backend = b
        h.parent = parent
        h.node = root
        return h
    end function

    function update(h, desc)
        b = h.backend
        old = h.node
        if gtkui._compatible(old, desc) then
            node = gtkui._reconcile_node(b, old, desc)
        else
            b.parent_detach(h.parent, old.widget)
            gtkui._destroy_node(b, old)
            node = gtkui._create_node(b, desc)
            b.parent_attach(h.parent, node.widget)
        end if
        nh = {}
        nh.backend = b
        nh.parent = h.parent
        nh.node = node
        return nh
    end function

    function unmount(h)
        b = h.backend
        b.parent_detach(h.parent, h.node.widget)
        gtkui._destroy_node(b, h.node)
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
